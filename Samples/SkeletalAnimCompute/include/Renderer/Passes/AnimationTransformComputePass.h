#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <Model/RenderData.h>
#include <Renderer/IRenderPass.h>
#include <Renderer/SceneFrameData.h>
#include <Tools/Timer.h>

namespace RAnimation
{
    // Compute pass: applies per-node TRS to produce TRS matrices.
    // Owns NodeTransformBuffer + TRSMatrixBuffer registrations.
    // Reads NodeTransformBuffer (SR), writes TRSMatrixBuffer (SRS).
    // PassRegistry auto-generates the begin-of-compute barrier from DeclareAccess().
    class AnimationTransformComputePass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "AnimationTransformComputePass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::Compute; }

        bool DeclareResources(ResourceContext& context) override;
        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void DeclareAccess(RegistryAccessBuilder& builder) const override;
        void Upload(FrameContext& context) override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

    private:
        nri::PipelineLayout* mPipelineLayout = nullptr;
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges;
        std::vector<nri::DescriptorSet*> mDescriptorSets;

        // Owned (for compute): NodeTransform (SR read), TRSMatrix (SRS write).
        BufferHandle mNodeTransformBuffer{};
        BufferHandle mTRSMatrixBuffer{};
        BufferViewHandle mNodeTransformView{};
        BufferViewHandle mTRSMatrixStorageView{};

        // Registered for Upload (shared with BoneMatrixComputePass which reads them on GPU).
        BufferHandle mNodeParentIndexBuffer{};
        BufferHandle mBoneNodeIndexBuffer{};
        BufferHandle mBoneOffsetMatrixBuffer{};
        BufferHandle mModelRootMatrixBuffer{};

        // Per-frame CPU-side scratch state populated by Upload(), consumed during Record() and by
        // sibling passes via SceneFrameData::animatedDispatches.
        std::vector<RNodeTransformData> mNodeTransformData;
        std::vector<int32_t> mNodeParentIndices;
        std::vector<uint32_t> mBoneNodeIndices;
        std::vector<glm::mat4> mBoneOffsetMatrices;
        std::vector<glm::mat4> mModelRootMatrices;
        std::vector<AnimatedDispatch> mAnimatedDispatches;

        Timer mMatrixGenerateTimer{};
        Timer mUploadToUBOTimer{};
    };
} // namespace RAnimation
