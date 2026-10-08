#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <AnimGraph/AnimNode.h>
#include <AnimGraph/GraphDesc.h>

namespace RAnimation
{
    class Model;

    // Baked graph: owning flat node array indexed by GraphDesc index (= LinkID).
    struct BakedGraph
    {
        std::vector<std::unique_ptr<AnimNode>> Storage;
        std::vector<SaveCachedPose*> SavedPoseNodes; // dependency order (evaluated first)
        std::vector<int> TopoOrder;                  // dependencies before dependents
        int RootNodeId = -1;

        AnimNode* Get(int id) const
        {
            return (id >= 0 && static_cast<size_t>(id) < Storage.size()) ? Storage[static_cast<size_t>(id)].get()
                                                                          : nullptr;
        }
    };

    // GraphDesc -> executable node array: validate, detect cycles, topological order, instantiate, resolve links.
    class GraphBaker
    {
    public:
        // Structural + semantic validation (types, pins, root, clip ranges, cache names). Empty = ok.
        static std::vector<std::string> Validate(const GraphDesc& desc, int clipCount);

        static std::unique_ptr<BakedGraph> Bake(const GraphDesc& desc, const Model& model, std::string& outError);

        // Dependencies of node idx: explicit pose inputs + UseCachedPose's virtual edge to its SaveCachedPose.
        static std::vector<int> DependenciesOf(const GraphDesc& desc, int idx, const std::map<std::string, int>& saveByName);
        // Three-colour DFS; returns a description of the first cycle found, or empty.
        static std::string DetectCycles(const GraphDesc& desc, const std::map<std::string, int>& saveByName);
        static std::vector<int> TopologicalOrder(const GraphDesc& desc, const std::map<std::string, int>& saveByName);

    private:
        static std::unique_ptr<AnimNode> Create(const NodeDesc& d);
        static void ResolveLinks(const NodeDesc& d, AnimNode* node, BakedGraph& graph);
        static std::map<std::string, int> CollectSaveNodes(const GraphDesc& desc);
    };
} // namespace RAnimation
