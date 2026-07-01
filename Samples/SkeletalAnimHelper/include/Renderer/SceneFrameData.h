#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ModelAndInstanceData;

namespace RAnimation
{
    class ModelInstance;

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

        // -- Selection / picking (see Renderer::Draw) --
        // Per-model-group base pick ID; a draw group's instance i emits pick ID base + i + 1.
        // Pick ID 0 is the "null object" (nothing). drawOrderInstances[pickID - 1] resolves an ID
        // back to its instance. selectedPickID is the currently selected instance's pick ID (0 = none).
        std::unordered_map<std::string, uint32_t> pickBaseByModel;
        std::vector<std::shared_ptr<ModelInstance>> drawOrderInstances;
        uint32_t selectedPickID = 0;
    };
} // namespace RAnimation
