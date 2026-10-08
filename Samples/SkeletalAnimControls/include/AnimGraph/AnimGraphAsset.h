#pragma once
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <AnimGraph/GraphDesc.h>

namespace RAnimation
{
    // Editor placement, aligned with GraphDesc::Nodes of the editor-level (unfolded) description.
    struct GraphLayout
    {
        std::vector<glm::vec2> NodePos;
    };

    // Shared graph asset: a loaded .animgraph.usda kept in two forms. EditorDesc is the file as authored
    // (CpuOnly markers, layout) for the editor; RuntimeDesc has the markers folded for the baker. Many
    // instances share one asset and each bakes its own AnimGraphInstance.
    struct AnimGraphAsset
    {
        GraphDesc EditorDesc;
        GraphLayout Layout;
        GraphDesc RuntimeDesc;
        std::vector<int> EditorToRuntime; // editor node index -> runtime node index (-1 for folded markers)

        std::string Path; // empty for implicit graphs
        std::string Name;
        std::string Doc;
        bool bImplicit = false;
        uint64_t Revision = 0; // bumped on every edit so instances know to rebuild

        // Recomputes RuntimeDesc / EditorToRuntime from EditorDesc.
        void Finalize();

        static std::shared_ptr<AnimGraphAsset> LoadFromFile(const std::string& path, std::string& outError);
        static std::shared_ptr<AnimGraphAsset> MakeSingleClip(int clipIndex, float playRate, bool looping);
    };
} // namespace RAnimation
