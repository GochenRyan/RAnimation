#pragma once

#include <string>
#include <vector>

#include <memory>

#include <AnimGraph/Pose.h>
#include <Model/AnimClip.h>
#include <Model/UsdModelLoader.h>
#include <TrsTexture/TrsTexture.h>
#include <TrsTexture/TrsTextureFile.h>

// Adapters between the USD loader output and the TrsTexture library. Compiled into both the sample and
// TrsTextureBaker so the fingerprint and the bake inputs are computed by the very same code.

namespace RAnimation
{
    // Everything the fingerprint hashes, taken straight from the loader output.
    TrsSourceView BuildTrsSourceView(const UsdLoadedModel& loaded);

    // Bind pose in node order (node 0 = synthetic root), decomposed exactly like Node::SetLocalTransform.
    std::vector<Transform> BuildBindPose(const UsdLoadedModel& loaded);

    // Runtime AnimClip objects from the loader output (the same construction Model::LoadModel performs).
    std::vector<std::shared_ptr<AnimClip>> BuildAnimClips(const UsdLoadedModel& loaded);

    // Clip descriptors for BoneTrsTexture::Bake.
    std::vector<TrsBakeClipDesc> BuildBakeClipDescs(const UsdLoadedModel& loaded);

    // Where the baked texture for an asset lives: <asset dir>/baked/<AssetStem>.trstex, where the stem is the
    // asset file name without its ".asset.usda" / ".usda" suffix.
    std::string TrsTexturePathForAsset(const std::string& assetUsdPath);
} // namespace RAnimation
