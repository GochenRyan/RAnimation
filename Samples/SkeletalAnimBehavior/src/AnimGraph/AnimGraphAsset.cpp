#include <AnimGraph/AnimGraphAsset.h>

#include <filesystem>

#include <AnimGraph/AnimGraphUsdIo.h>

namespace RAnimation
{
    void AnimGraphAsset::Finalize()
    {
        RuntimeDesc = EditorDesc;
        EditorToRuntime = FoldCpuOnly(RuntimeDesc);
        ++Revision;
    }

    std::shared_ptr<AnimGraphAsset> AnimGraphAsset::LoadFromFile(const std::string& path, std::string& outError)
    {
        auto asset = std::make_shared<AnimGraphAsset>();
        if (!LoadAnimGraphUsd(path, asset->EditorDesc, asset->Layout, asset->Doc, outError))
        {
            return nullptr;
        }
        asset->Path = path;
        std::string stem = std::filesystem::path(path).filename().generic_string();
        const std::string suffix = ".animgraph.usda";
        if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            stem.erase(stem.size() - suffix.size());
        }
        asset->Name = stem;
        asset->Finalize();
        return asset;
    }

    std::shared_ptr<AnimGraphAsset> AnimGraphAsset::MakeSingleClip(int clipIndex, float playRate, bool looping)
    {
        auto asset = std::make_shared<AnimGraphAsset>();
        asset->EditorDesc = MakeSingleClipGraphDesc(clipIndex, playRate, looping);
        asset->Layout.NodePos = {glm::vec2(80.0f, 120.0f), glm::vec2(420.0f, 120.0f)};
        asset->Name = "(single clip)";
        asset->Doc = "implicit graph: ClipPlayer -> Root";
        asset->bImplicit = true;
        asset->Finalize();
        return asset;
    }
} // namespace RAnimation
