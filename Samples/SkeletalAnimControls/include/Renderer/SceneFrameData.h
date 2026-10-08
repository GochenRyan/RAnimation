#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <AnimGraph/EvalMode.h>

struct ModelAndInstanceData;

namespace RAnimation
{
    class Model;
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

    // CPU-side description of one animated model group as packed by HybridEvalComputePass::Upload.
    // gpu is the root-constant POD BoneMatrixComputePass consumes unchanged; in tier C gpu.instanceCount is
    // the number of TRACKS while drawInstanceCount is the number of crowd members drawn.
    struct AnimatedGroup
    {
        AnimatedDispatch gpu;
        uint32_t workBase = 0;
        uint32_t workCount = 0;
        uint32_t drawInstanceCount = 0;
        uint32_t crowdInstanceBase = 0;
        EvalMode mode = EvalMode::Cpu;
        bool isTracks = false;
        Model* model = nullptr;
    };

    // Per-frame state filled by Renderer::Draw and HybridEvalComputePass::Upload, consumed during Record().
    struct SceneFrameData
    {
        const std::vector<AnimatedDispatch>* animatedDispatches = nullptr;
        const std::vector<AnimatedGroup>* animatedGroups = nullptr;
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
