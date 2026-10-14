#include <AnimGraph/ExecPlan.h>

#include <algorithm>
#include <functional>

#include <fmt/format.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/AnimNode.h>
#include <AnimGraph/GraphBaker.h>
#include <AnimGraph/HybridWork.h>

namespace RAnimation
{
    ExecPlan AnalyzeExecPlan(const GraphDesc& desc)
    {
        ExecPlan plan;
        const int n = static_cast<int>(desc.Nodes.size());
        plan.Sides.assign(static_cast<size_t>(n), NodeSide::Gpu);
        if (n == 0 || desc.OutputNode < 0 || desc.OutputNode >= n)
        {
            plan.Summary = "empty graph";
            return plan;
        }

        // 1) Any node that cannot run on the GPU (type or bForceCpu) is CPU.
        for (int i = 0; i < n; ++i)
        {
            const NodeDesc& d = desc.Nodes[static_cast<size_t>(i)];
            if (!IsGpuCapableNodeType(d.Type) || d.bForceCpu)
            {
                plan.Sides[static_cast<size_t>(i)] = NodeSide::Cpu;
            }
        }
        // Root is always GPU (an all-CPU graph becomes a single slot term feeding Root).
        plan.Sides[static_cast<size_t>(desc.OutputNode)] = NodeSide::Gpu;

        // 2) CPU propagates upstream: the CPU evaluates a CPU node's whole subtree.
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (int i = 0; i < n; ++i)
            {
                if (plan.Sides[static_cast<size_t>(i)] != NodeSide::Cpu)
                {
                    continue;
                }
                for (const auto& [pin, src] : desc.Nodes[static_cast<size_t>(i)].Inputs)
                {
                    if (src >= 0 && src < n && plan.Sides[static_cast<size_t>(src)] == NodeSide::Gpu && src != desc.OutputNode)
                    {
                        plan.Sides[static_cast<size_t>(src)] = NodeSide::Cpu;
                        changed = true;
                    }
                }
            }
        }

        // 3) Seams = GPU consumer <- CPU producer, numbered in a deterministic traversal from Root. One slot
        //    per CPU producer even if several GPU nodes consume it.
        std::vector<int> visited(static_cast<size_t>(n), 0);
        std::function<void(int)> walk = [&](int id)
        {
            if (visited[static_cast<size_t>(id)]++)
            {
                return;
            }
            const NodeDesc& d = desc.Nodes[static_cast<size_t>(id)];
            for (const auto& [pin, src] : d.Inputs)
            {
                if (src < 0 || src >= n)
                {
                    continue;
                }
                if (plan.Sides[static_cast<size_t>(src)] == NodeSide::Cpu)
                {
                    int slot = plan.SlotOf(src);
                    if (slot < 0)
                    {
                        slot = static_cast<int>(plan.SlotSourceNodeIds.size());
                        plan.SlotSourceNodeIds.push_back(src);
                    }
                    plan.Seams.push_back({src, id, pin, slot});
                }
                else
                {
                    walk(src);
                }
            }
        };
        walk(desc.OutputNode);
        plan.NumUploadSlots = static_cast<int>(plan.SlotSourceNodeIds.size());

        for (int i = 0; i < n; ++i)
        {
            (plan.Sides[static_cast<size_t>(i)] == NodeSide::Cpu ? plan.CpuNodeIds : plan.GpuNodeIds).push_back(i);
        }
        // Root alone on the GPU with one CPU slot == everything was computed on the CPU.
        plan.bAllCpu = plan.GpuNodeIds.size() == 1 && plan.NumUploadSlots == 1;
        plan.bAllGpu = plan.CpuNodeIds.empty();
        plan.Summary = fmt::format("{} GPU node(s), {} CPU node(s), {} upload slot(s){}",
                                   plan.GpuNodeIds.size(),
                                   plan.CpuNodeIds.size(),
                                   plan.NumUploadSlots,
                                   plan.bAllGpu ? " [all GPU]" : (plan.bAllCpu ? " [all CPU]" : ""));
        return plan;
    }

    namespace
    {
        bool Flatten(const AnimGraphInstance& instance,
                     int nodeId,
                     float weight,
                     int maskRow,
                     std::vector<HybridSample>& out,
                     std::vector<float>& maskRows,
                     std::string& outError,
                     int depth)
        {
            const ExecPlan& plan = instance.Plan();
            AnimNode* node = instance.GetNode(nodeId);
            if (node == nullptr)
            {
                outError = fmt::format("node {} missing from the baked graph", nodeId);
                return false;
            }
            if (depth > 64)
            {
                outError = "graph too deep while flattening";
                return false;
            }
            if (weight <= TwoWayBlend::kSmallestRelevantWeight)
            {
                return true;
            }

            if (plan.Sides[static_cast<size_t>(nodeId)] == NodeSide::Cpu)
            {
                const int slot = plan.SlotOf(nodeId);
                if (slot < 0)
                {
                    outError = fmt::format("CPU node {} has no upload slot", nodeId);
                    return false;
                }
                out.push_back({true, slot, weight, 0.0f, maskRow});
                return true;
            }

            if (auto* root = dynamic_cast<Root*>(node))
            {
                if (!root->Result.IsLinked())
                {
                    outError = "Root.Result is not connected";
                    return false;
                }
                return Flatten(instance, root->Result.LinkID, weight, maskRow, out, maskRows, outError, depth + 1);
            }
            if (auto* clip = dynamic_cast<ClipPlayer*>(node))
            {
                out.push_back({false, clip->ClipIndex, weight, clip->Time, maskRow});
                return true;
            }
            if (auto* blend = dynamic_cast<TwoWayBlend*>(node))
            {
                const float alpha = std::clamp(blend->Alpha, 0.0f, 1.0f);
                if (!blend->A.IsLinked() || !blend->B.IsLinked())
                {
                    outError = fmt::format("TwoWayBlend {} has an unconnected input", nodeId);
                    return false;
                }
                return Flatten(instance, blend->A.LinkID, weight * (1.0f - alpha), maskRow, out, maskRows, outError, depth + 1) &&
                       Flatten(instance, blend->B.LinkID, weight * alpha, maskRow, out, maskRows, outError, depth + 1);
            }
            if (auto* space = dynamic_cast<BlendSpace1D*>(node))
            {
                int low, high;
                float frac;
                space->Resolve(low, high, frac);
                if (low < 0)
                {
                    outError = fmt::format("BlendSpace1D {} has no samples", nodeId);
                    return false;
                }
                const PoseLink& l = space->Samples[static_cast<size_t>(low)].Link;
                const PoseLink& h = space->Samples[static_cast<size_t>(high)].Link;
                bool ok = true;
                if (l.IsLinked())
                {
                    ok = Flatten(instance, l.LinkID, weight * (1.0f - frac), maskRow, out, maskRows, outError, depth + 1);
                }
                if (ok && high != low && h.IsLinked())
                {
                    ok = Flatten(instance, h.LinkID, weight * frac, maskRow, out, maskRows, outError, depth + 1);
                }
                return ok;
            }
            if (auto* slot = dynamic_cast<AnimSlot*>(node))
            {
                if (!slot->Base.IsLinked())
                {
                    outError = fmt::format("AnimSlot {} has no Base input", nodeId);
                    return false;
                }
                const float montageWeight = slot->MontageWeight();
                const float baseWeight = 1.0f - montageWeight;
                if (baseWeight > TwoWayBlend::kSmallestRelevantWeight &&
                    !Flatten(instance, slot->Base.LinkID, weight * baseWeight, maskRow, out, maskRows, outError, depth + 1))
                {
                    return false;
                }
                for (const MontagePlayback& p : slot->Playing)
                {
                    if (p.Weight() > TwoWayBlend::kSmallestRelevantWeight && p.Desc().clipIndex >= 0)
                    {
                        out.push_back({false, p.Desc().clipIndex, weight * p.Weight(), p.Time(), maskRow});
                    }
                }
                return true;
            }
            if (auto* space2 = dynamic_cast<BlendSpace2D*>(node))
            {
                GridWeight weights[4];
                const int count = space2->Resolve(weights);
                if (count == 0)
                {
                    outError = fmt::format("BlendSpace2D {} has no samples", nodeId);
                    return false;
                }
                for (int k = 0; k < count; ++k)
                {
                    const PoseLink& link = space2->Samples[static_cast<size_t>(weights[k].index)].Link;
                    if (!link.IsLinked())
                    {
                        continue;
                    }
                    if (!Flatten(instance, link.LinkID, weight * weights[k].weight, maskRow, out, maskRows, outError, depth + 1))
                    {
                        return false;
                    }
                }
                return true;
            }
            if (auto* select = dynamic_cast<CrossfadeSelect*>(node))
            {
                const auto& layers = select->Tracker().Layers();
                if (layers.empty())
                {
                    // Before the first Update: the authored input alone.
                    const int idx = select->ActiveIndex;
                    if (idx < 0 || static_cast<size_t>(idx) >= select->Inputs.size() || !select->Inputs[static_cast<size_t>(idx)].IsLinked())
                    {
                        outError = fmt::format("CrossfadeSelect {} has no connected input {}", nodeId, idx);
                        return false;
                    }
                    return Flatten(instance, select->Inputs[static_cast<size_t>(idx)].LinkID, weight, maskRow, out, maskRows, outError, depth + 1);
                }
                for (const auto& l : layers)
                {
                    if (l.index < 0 || static_cast<size_t>(l.index) >= select->Inputs.size())
                    {
                        continue;
                    }
                    const PoseLink& link = select->Inputs[static_cast<size_t>(l.index)];
                    if (!link.IsLinked())
                    {
                        outError = fmt::format("CrossfadeSelect {} input {} is not connected", nodeId, l.index);
                        return false;
                    }
                    if (!Flatten(instance, link.LinkID, weight * l.weight, maskRow, out, maskRows, outError, depth + 1))
                    {
                        return false;
                    }
                }
                return true;
            }
            if (auto* layered = dynamic_cast<LayeredBlend*>(node))
            {
                if (!layered->Base.IsLinked())
                {
                    outError = fmt::format("LayeredBlend {} has no Base input", nodeId);
                    return false;
                }
                const float alpha = std::clamp(layered->Alpha, 0.0f, 1.0f);
                const uint32_t nodeCount = instance.NodeCount();
                if (alpha <= TwoWayBlend::kSmallestRelevantWeight || !layered->Layer.IsLinked() ||
                    layered->Mask.size() != nodeCount)
                {
                    // Layer off: the base shows through unchanged, no new rows.
                    return Flatten(instance, layered->Base.LinkID, weight, maskRow, out, maskRows, outError, depth + 1);
                }
                // Two new factor rows: the parent's factor times (1 - alpha * mask) for the base path and
                // times alpha * mask for the layer path. Scalar weights pass through unchanged.
                const int baseRow = static_cast<int>(maskRows.size() / nodeCount);
                const int layerRow = baseRow + 1;
                maskRows.resize(maskRows.size() + 2u * nodeCount);
                for (uint32_t n = 0; n < nodeCount; ++n)
                {
                    const float parent = maskRow >= 0 ? maskRows[static_cast<size_t>(maskRow) * nodeCount + n] : 1.0f;
                    const float m = alpha * layered->Mask[n];
                    maskRows[static_cast<size_t>(baseRow) * nodeCount + n] = parent * (1.0f - m);
                    maskRows[static_cast<size_t>(layerRow) * nodeCount + n] = parent * m;
                }
                return Flatten(instance, layered->Base.LinkID, weight, baseRow, out, maskRows, outError, depth + 1) &&
                       Flatten(instance, layered->Layer.LinkID, weight, layerRow, out, maskRows, outError, depth + 1);
            }
            outError = fmt::format("node {} ({}) cannot be flattened for the GPU", nodeId, node->TypeName());
            return false;
        }
    } // namespace

    bool BuildHybridSamples(const AnimGraphInstance& instance,
                            std::vector<HybridSample>& out,
                            std::vector<float>& outMaskRows,
                            std::string& outError)
    {
        out.clear();
        outMaskRows.clear();
        outError.clear();
        const BakedGraph* baked = instance.Baked();
        if (baked == nullptr || baked->RootNodeId < 0)
        {
            outError = "graph is not baked";
            return false;
        }
        return Flatten(instance, baked->RootNodeId, 1.0f, -1, out, outMaskRows, outError, 0);
    }

    void NormalizeHybrid(std::vector<HybridSample>& samples)
    {
        // merge same source (same kind + index + time), drop zero weights, renormalise
        std::vector<HybridSample> merged;
        for (const HybridSample& s : samples)
        {
            if (s.Weight <= TwoWayBlend::kSmallestRelevantWeight)
            {
                continue;
            }
            bool found = false;
            for (HybridSample& m : merged)
            {
                if (m.bExternal == s.bExternal && m.Index == s.Index && m.MaskRow == s.MaskRow &&
                    (m.bExternal || m.TimeSec == s.TimeSec))
                {
                    m.Weight += s.Weight;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                merged.push_back(s);
            }
        }
        samples.swap(merged);
    }

    bool CheckHybridLimits(const ExecPlan& plan,
                           const std::vector<HybridSample>& work,
                           int maxTerms,
                           int maxSlots,
                           std::string& outError,
                           int maskRowCount,
                           int maxMaskRows)
    {
        if (maxMaskRows > 0 && maskRowCount > maxMaskRows)
        {
            outError = fmt::format("graph needs {} mask rows; at most {} per instance are supported", maskRowCount, maxMaskRows);
            return false;
        }
        if (static_cast<int>(work.size()) > maxTerms)
        {
            outError = fmt::format("graph flattens to {} GPU terms; the kernel accepts at most {} per Work", work.size(), maxTerms);
            return false;
        }
        if (plan.NumUploadSlots > maxSlots)
        {
            outError = fmt::format("graph needs {} upload slots; at most {} per instance are supported", plan.NumUploadSlots, maxSlots);
            return false;
        }
        if (work.empty())
        {
            outError = "graph flattens to no GPU terms (nothing reaches Root with a non-zero weight)";
            return false;
        }
        return true;
    }
} // namespace RAnimation
