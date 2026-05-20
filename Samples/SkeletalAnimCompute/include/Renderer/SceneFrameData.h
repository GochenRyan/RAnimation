#pragma once

#include <cstdint>
#include <vector>

struct ModelAndInstanceData;

namespace RAnimation
{
    struct AnimatedDispatch
    {
        uint32_t nodeTransformOffset = 0;
        uint32_t boneMatrixOffset = 0;
        uint32_t modelRootOffset = 0;
        uint32_t numberOfNodes = 0;
        uint32_t numberOfBones = 0;
        uint32_t instanceCount = 0;
    };

    // Per-frame state computed by Renderer::updateModelBuffer() and consumed by passes during Record().
    struct SceneFrameData
    {
        const std::vector<AnimatedDispatch>* animatedDispatches = nullptr;
        ModelAndInstanceData* modelInstData = nullptr;
        size_t uploadedBoneOffsetMatrixCount = 0;
        bool hasSceneGeometry = false;
    };
} // namespace RAnimation
