#pragma once
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Model/AnimChannel.h> // AnimBehaviour
#include <Model/RenderData.h>  // RMesh / RVertex

// USD-free interface for the OpenUSD asset loader. ALL pxr/USD (and boost/TBB) headers stay inside
// UsdModelLoader.cpp so they never leak into the rest of the sample (compile-time isolation, pimpl-style).
// The loader returns plain CPU data; Model owns the GPU upload and the runtime Node/Bone/AnimClip objects.

namespace RAnimation
{
    // One joint of the skeleton, flattened. Emitted in parent-before-child order so Model can rebuild
    // the Node tree by walking the list once. parentIndex is -1 for the root.
    struct UsdNodeData
    {
        std::string name;
        int parentIndex = -1;
        glm::mat4 localTransform = glm::mat4(1.0f);
    };

    // A skeleton joint exposed as a Bone. boneId is the index of this entry in UsdLoadedModel::bones,
    // and matches the bone indices baked into RVertex::boneNumber.
    struct UsdBoneData
    {
        std::string name;
        glm::mat4 inverseBindMatrix = glm::mat4(1.0f);
    };

    // Per-joint keyframes for one clip, already sampled to clip ticks/frames (index-aligned timings/values).
    struct UsdAnimChannelData
    {
        std::string nodeName;
        std::vector<float> translationTimings;
        std::vector<glm::vec3> translations;
        std::vector<float> rotationTimings;
        std::vector<glm::quat> rotations;
        std::vector<float> scaleTimings;
        std::vector<glm::vec3> scalings;
        AnimBehaviour preState = AnimBehaviour::Default;
        AnimBehaviour postState = AnimBehaviour::Default;
    };

    struct UsdAnimClipData
    {
        std::string name;
        float duration = 0.0f;       // in ticks/frames
        float ticksPerSecond = 0.0f; // frames per second
        std::vector<UsdAnimChannelData> channels;
    };

    // A texture file the material references. name is the key used in RMesh::textures; absolutePath is
    // what Model passes to the texture loader. Empty when the asset uses vertex/display colors only.
    struct UsdTextureRef
    {
        std::string name;
        std::string absolutePath;
    };

    struct UsdLoadedModel
    {
        std::vector<UsdNodeData> nodes;
        std::vector<UsdBoneData> bones;
        std::vector<RMesh> meshes;
        std::vector<UsdAnimClipData> animClips;
        std::vector<UsdTextureRef> textures;
        glm::mat4 rootTransform = glm::mat4(1.0f);
    };

    // Loads a UsdSkel asset (typically a *.asset.usda that composes skeleton/skinning/geometry/material
    // layers) plus its sibling animation clips into CPU data. Returns false on failure (message printed).
    bool LoadUsdModel(const std::string& assetUsdPath, UsdLoadedModel& out);
} // namespace RAnimation
