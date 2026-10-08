#include <AnimGraph/GraphBaker.h>

#include <algorithm>
#include <functional>
#include <set>

#include <fmt/format.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <Model/Model.h>

namespace RAnimation
{
    namespace
    {
        // Pose input pins per node type (the editor registry in GraphModel.h mirrors this).
        std::vector<std::string> PoseInputPins(const NodeDesc& d)
        {
            if (d.Type == "Root")
            {
                return {"Result"};
            }
            if (d.Type == "TwoWayBlend")
            {
                return {"A", "B"};
            }
            if (d.Type == "BlendSpace1D")
            {
                size_t count = d.SamplePositions.size();
                for (const auto& [pin, src] : d.Inputs)
                {
                    if (pin.rfind("Sample", 0) == 0)
                    {
                        const size_t idx = static_cast<size_t>(std::atoi(pin.c_str() + 6));
                        count = std::max(count, idx + 1);
                    }
                }
                std::vector<std::string> pins;
                for (size_t i = 0; i < count; ++i)
                {
                    pins.push_back("Sample" + std::to_string(i));
                }
                return pins;
            }
            if (d.Type == "SaveCachedPose" || d.Type == "CpuOnly")
            {
                return {"Source"};
            }
            return {};
        }

        bool HasPoseOutput(const NodeDesc& d)
        {
            return d.Type != "Root" && d.Type != "SaveCachedPose";
        }
    } // namespace

    std::map<std::string, int> GraphBaker::CollectSaveNodes(const GraphDesc& desc)
    {
        std::map<std::string, int> byName;
        for (size_t i = 0; i < desc.Nodes.size(); ++i)
        {
            if (desc.Nodes[i].Type == "SaveCachedPose" && !desc.Nodes[i].CacheName.empty())
            {
                byName.emplace(desc.Nodes[i].CacheName, static_cast<int>(i));
            }
        }
        return byName;
    }

    std::vector<std::string> GraphBaker::Validate(const GraphDesc& desc, int clipCount)
    {
        std::vector<std::string> errors;
        const int n = static_cast<int>(desc.Nodes.size());
        if (n == 0)
        {
            errors.push_back("graph is empty");
            return errors;
        }

        int rootCount = 0;
        std::set<std::string> cacheNames;
        for (int i = 0; i < n; ++i)
        {
            const NodeDesc& d = desc.Nodes[static_cast<size_t>(i)];
            const std::string where = fmt::format("node {} ({})", i, d.Type);
            if (!IsKnownNodeType(d.Type))
            {
                errors.push_back(where + ": unknown node type");
                continue;
            }
            if (d.Type == "CpuOnly")
            {
                errors.push_back(where + ": CpuOnly markers must be folded before baking (FoldCpuOnly)");
                continue;
            }
            if (d.Type == "Root")
            {
                ++rootCount;
            }

            const std::vector<std::string> pins = PoseInputPins(d);
            for (const auto& [pin, src] : d.Inputs)
            {
                if (std::find(pins.begin(), pins.end(), pin) == pins.end())
                {
                    errors.push_back(fmt::format("{}: no input pin '{}'", where, pin));
                    continue;
                }
                if (src < -1 || src >= n)
                {
                    errors.push_back(fmt::format("{}: pin '{}' references node {} (out of range)", where, pin, src));
                    continue;
                }
                if (src == i)
                {
                    errors.push_back(fmt::format("{}: pin '{}' is connected to itself", where, pin));
                    continue;
                }
                if (src >= 0 && !HasPoseOutput(desc.Nodes[static_cast<size_t>(src)]))
                {
                    errors.push_back(fmt::format("{}: pin '{}' source node {} has no pose output", where, pin, src));
                }
            }

            auto connected = [&](const char* pin) { return d.FindInput(pin) >= 0; };
            if (d.Type == "Root" && !connected("Result"))
            {
                errors.push_back(where + ": Result is not connected");
            }
            if (d.Type == "TwoWayBlend" && (!connected("A") || !connected("B")))
            {
                errors.push_back(where + ": both A and B must be connected");
            }
            if (d.Type == "BlendSpace1D")
            {
                bool any = false;
                for (const std::string& pin : pins)
                {
                    any = any || d.FindInput(pin) >= 0;
                }
                if (!any)
                {
                    errors.push_back(where + ": needs at least one connected sample");
                }
                for (size_t k = 1; k < d.SamplePositions.size(); ++k)
                {
                    if (d.SamplePositions[k] < d.SamplePositions[k - 1])
                    {
                        errors.push_back(where + ": sample positions must be ascending");
                        break;
                    }
                }
            }
            if (d.Type == "ClipPlayer" && clipCount >= 0 && (d.ClipIndex < 0 || d.ClipIndex >= clipCount))
            {
                errors.push_back(fmt::format("{}: clip index {} out of range (model has {} clips)", where, d.ClipIndex, clipCount));
            }
            if (d.Type == "SaveCachedPose")
            {
                if (d.CacheName.empty())
                {
                    errors.push_back(where + ": cache name is empty");
                }
                else if (!cacheNames.insert(d.CacheName).second)
                {
                    errors.push_back(fmt::format("{}: duplicate cache name '{}'", where, d.CacheName));
                }
                if (!connected("Source"))
                {
                    errors.push_back(where + ": Source is not connected");
                }
            }
        }

        const std::map<std::string, int> saveByName = CollectSaveNodes(desc);
        for (int i = 0; i < n; ++i)
        {
            const NodeDesc& d = desc.Nodes[static_cast<size_t>(i)];
            if (d.Type == "UseCachedPose" && saveByName.find(d.CacheName) == saveByName.end())
            {
                errors.push_back(fmt::format("node {} (UseCachedPose): no SaveCachedPose named '{}'", i, d.CacheName));
            }
        }

        if (rootCount != 1)
        {
            errors.push_back(fmt::format("graph must have exactly one Root (found {})", rootCount));
        }
        if (desc.OutputNode < 0 || desc.OutputNode >= n || desc.Nodes[static_cast<size_t>(desc.OutputNode)].Type != "Root")
        {
            errors.push_back("OutputNode must point at the Root node");
        }

        if (errors.empty())
        {
            const std::string cycle = DetectCycles(desc, saveByName);
            if (!cycle.empty())
            {
                errors.push_back(cycle);
            }
        }
        return errors;
    }

    std::vector<int> GraphBaker::DependenciesOf(const GraphDesc& desc, int idx, const std::map<std::string, int>& saveByName)
    {
        std::vector<int> deps;
        const NodeDesc& d = desc.Nodes[static_cast<size_t>(idx)];
        for (const auto& [pin, src] : d.Inputs)
        {
            if (src >= 0 && src < static_cast<int>(desc.Nodes.size()))
            {
                deps.push_back(src);
            }
        }
        if (d.Type == "UseCachedPose")
        {
            const auto it = saveByName.find(d.CacheName);
            if (it != saveByName.end())
            {
                deps.push_back(it->second); // virtual edge: the cache must be produced first
            }
        }
        return deps;
    }

    std::string GraphBaker::DetectCycles(const GraphDesc& desc, const std::map<std::string, int>& saveByName)
    {
        // Three-colour DFS: 0 = unvisited, 1 = on the stack (grey), 2 = done (black). Grey -> grey is a cycle.
        const int n = static_cast<int>(desc.Nodes.size());
        std::vector<int> state(static_cast<size_t>(n), 0);
        std::string cycle;
        std::function<bool(int)> visit = [&](int idx) -> bool
        {
            state[static_cast<size_t>(idx)] = 1;
            for (int dep : DependenciesOf(desc, idx, saveByName))
            {
                if (state[static_cast<size_t>(dep)] == 1)
                {
                    cycle = fmt::format("cycle: node {} ({}) depends on node {} ({}) which is still being evaluated",
                                        idx,
                                        desc.Nodes[static_cast<size_t>(idx)].Type,
                                        dep,
                                        desc.Nodes[static_cast<size_t>(dep)].Type);
                    return false;
                }
                if (state[static_cast<size_t>(dep)] == 0 && !visit(dep))
                {
                    return false;
                }
            }
            state[static_cast<size_t>(idx)] = 2;
            return true;
        };
        for (int i = 0; i < n; ++i)
        {
            if (state[static_cast<size_t>(i)] == 0 && !visit(i))
            {
                break;
            }
        }
        return cycle;
    }

    std::vector<int> GraphBaker::TopologicalOrder(const GraphDesc& desc, const std::map<std::string, int>& saveByName)
    {
        // Post-order DFS: dependencies enter the order before their dependents.
        const int n = static_cast<int>(desc.Nodes.size());
        std::vector<int> state(static_cast<size_t>(n), 0);
        std::vector<int> order;
        std::function<void(int)> visit = [&](int idx)
        {
            if (state[static_cast<size_t>(idx)] != 0)
            {
                return;
            }
            state[static_cast<size_t>(idx)] = 1;
            for (int dep : DependenciesOf(desc, idx, saveByName))
            {
                visit(dep);
            }
            state[static_cast<size_t>(idx)] = 2;
            order.push_back(idx);
        };
        for (int i = 0; i < n; ++i)
        {
            visit(i);
        }
        return order;
    }

    std::unique_ptr<AnimNode> GraphBaker::Create(const NodeDesc& d)
    {
        if (d.Type == "Root")
        {
            return std::make_unique<Root>();
        }
        if (d.Type == "ClipPlayer")
        {
            auto node = std::make_unique<ClipPlayer>();
            node->ClipIndex = d.ClipIndex;
            node->PlayRate = d.PlayRate;
            node->bLooping = d.bLooping;
            return node;
        }
        if (d.Type == "TwoWayBlend")
        {
            auto node = std::make_unique<TwoWayBlend>();
            node->Alpha = d.Param;
            return node;
        }
        if (d.Type == "BlendSpace1D")
        {
            auto node = std::make_unique<BlendSpace1D>();
            const std::vector<std::string> pins = PoseInputPins(d);
            node->Samples.resize(pins.size());
            for (size_t i = 0; i < pins.size(); ++i)
            {
                // Default positions: evenly spread over [0, 1] when not authored.
                node->Samples[i].Position = i < d.SamplePositions.size()
                                                    ? d.SamplePositions[i]
                                                    : (pins.size() > 1 ? static_cast<float>(i) / static_cast<float>(pins.size() - 1) : 0.0f);
            }
            node->Input = d.Param;
            return node;
        }
        if (d.Type == "SaveCachedPose")
        {
            auto node = std::make_unique<SaveCachedPose>();
            node->CacheName = d.CacheName;
            return node;
        }
        if (d.Type == "UseCachedPose")
        {
            auto node = std::make_unique<UseCachedPose>();
            node->CacheName = d.CacheName;
            return node;
        }
        return nullptr;
    }

    void GraphBaker::ResolveLinks(const NodeDesc& d, AnimNode* node, BakedGraph& graph)
    {
        std::vector<PoseLink*> links;
        node->GatherLinks(links);
        const std::vector<std::string> pins = PoseInputPins(d);
        for (size_t p = 0; p < pins.size() && p < links.size(); ++p)
        {
            const int src = d.FindInput(pins[p]);
            links[p]->LinkID = src;
            links[p]->LinkedNode = graph.Get(src);
        }
    }

    std::unique_ptr<BakedGraph> GraphBaker::Bake(const GraphDesc& desc, const Model& model, std::string& outError)
    {
        const std::vector<std::string> errors = Validate(desc, static_cast<int>(model.GetAnimClips().size()));
        if (!errors.empty())
        {
            outError.clear();
            for (const std::string& e : errors)
            {
                outError += (outError.empty() ? "" : "; ") + e;
            }
            return nullptr;
        }

        const std::map<std::string, int> saveByName = CollectSaveNodes(desc);
        auto graph = std::make_unique<BakedGraph>();
        graph->Storage.reserve(desc.Nodes.size());
        for (size_t i = 0; i < desc.Nodes.size(); ++i)
        {
            std::unique_ptr<AnimNode> node = Create(desc.Nodes[i]);
            if (!node)
            {
                outError = fmt::format("node {}: cannot instantiate type '{}'", i, desc.Nodes[i].Type);
                return nullptr;
            }
            node->NodeId = static_cast<int>(i);
            node->bForceCpu = desc.Nodes[i].bForceCpu;
            graph->Storage.emplace_back(std::move(node));
        }

        for (size_t i = 0; i < desc.Nodes.size(); ++i)
        {
            ResolveLinks(desc.Nodes[i], graph->Storage[i].get(), *graph);
            if (auto* use = dynamic_cast<UseCachedPose*>(graph->Storage[i].get()))
            {
                const auto it = saveByName.find(use->CacheName);
                use->Source = it != saveByName.end() ? dynamic_cast<SaveCachedPose*>(graph->Get(it->second)) : nullptr;
            }
        }

        graph->RootNodeId = desc.OutputNode;
        graph->TopoOrder = TopologicalOrder(desc, saveByName);
        for (int id : graph->TopoOrder)
        {
            if (auto* save = dynamic_cast<SaveCachedPose*>(graph->Get(id)))
            {
                graph->SavedPoseNodes.push_back(save);
            }
        }
        return graph;
    }
} // namespace RAnimation
