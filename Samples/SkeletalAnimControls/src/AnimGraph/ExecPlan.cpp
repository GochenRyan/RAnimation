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
                     std::vector<HybridSample>& out,
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
                out.push_back({true, slot, weight, 0.0f});
                return true;
            }

            if (auto* root = dynamic_cast<Root*>(node))
            {
                if (!root->Result.IsLinked())
                {
                    outError = "Root.Result is not connected";
                    return false;
                }
                return Flatten(instance, root->Result.LinkID, weight, out, outError, depth + 1);
            }
            if (auto* clip = dynamic_cast<ClipPlayer*>(node))
            {
                out.push_back({false, clip->ClipIndex, weight, clip->Time});
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
                return Flatten(instance, blend->A.LinkID, weight * (1.0f - alpha), out, outError, depth + 1) &&
                       Flatten(instance, blend->B.LinkID, weight * alpha, out, outError, depth + 1);
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
                    ok = Flatten(instance, l.LinkID, weight * (1.0f - frac), out, outError, depth + 1);
                }
                if (ok && high != low && h.IsLinked())
                {
                    ok = Flatten(instance, h.LinkID, weight * frac, out, outError, depth + 1);
                }
                return ok;
            }
            outError = fmt::format("node {} ({}) cannot be flattened for the GPU", nodeId, node->TypeName());
            return false;
        }
    } // namespace

    bool BuildHybridSamples(const AnimGraphInstance& instance, std::vector<HybridSample>& out, std::string& outError)
    {
        out.clear();
        outError.clear();
        const BakedGraph* baked = instance.Baked();
        if (baked == nullptr || baked->RootNodeId < 0)
        {
            outError = "graph is not baked";
            return false;
        }
        return Flatten(instance, baked->RootNodeId, 1.0f, out, outError, 0);
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
                if (m.bExternal == s.bExternal && m.Index == s.Index && (m.bExternal || m.TimeSec == s.TimeSec))
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
        float total = 0.0f;
        for (const HybridSample& m : merged)
        {
            total += m.Weight;
        }
        if (total > 0.0f)
        {
            for (HybridSample& m : merged)
            {
                m.Weight /= total;
            }
        }
        samples.swap(merged);
    }

    bool CheckHybridLimits(const ExecPlan& plan,
                           const std::vector<HybridSample>& work,
                           int maxTerms,
                           int maxSlots,
                           std::string& outError)
    {
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
