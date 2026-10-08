#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <AnimGraph/HybridWork.h>
#include <Model/RenderData.h>
#include <Renderer/IRenderPass.h>
#include <Renderer/SceneFrameData.h>
#include <Tools/Timer.h>

namespace RAnimation
{
    // The single pose-evaluation pass: TRS texture + upload slots -> local matrices (TRSMatrixBuffer).
    // Shader: hybrid_eval.cs.hlsl. All tiers share the kernel and differ only in Record's dispatch strategy:
    //   CPU / B: one Dispatch(N,1,1) per model group; A: Dispatch(1,1,1) per instance; C: Dispatch(T,1,1) per model.
    // Upload only packs (Works / Terms / slots / skeleton tables / crowd instances) and checks capacity; the
    // evaluation itself happens in AnimationSystem. Replaces AnimationTransformComputePass and takes over its
    // skeleton-table upload and animatedDispatches publishing.
    class HybridEvalComputePass : public IRenderPass
    {
    public:
        static constexpr uint32_t kMaxTrsTextureModels = 64;

        const char* GetName() const override { return "HybridEvalComputePass"; }
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
        struct PushConstants
        {
            uint32_t workBase = 0;
            uint32_t nodeCount = 0;
            uint32_t flags = 0;
            uint32_t _pad = 0;
        };

        bool createFallbackSet1(FrameContext& context);
        void publishEmpty(FrameContext& context);

        nri::PipelineLayout* mPipelineLayout = nullptr;
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges0;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges1;
        std::vector<nri::DescriptorSet*> mDescriptorSets;

        // Fallback set 1 (1x1 neutral texture + one clip record) bound when a model has no usable texture.
        nri::Texture* mFallbackTexture = nullptr;
        nri::Memory* mFallbackTextureMemory = nullptr;
        nri::Descriptor* mFallbackTextureView = nullptr;
        nri::Buffer* mFallbackClipTable = nullptr;
        nri::Memory* mFallbackClipTableMemory = nullptr;
        nri::Descriptor* mFallbackClipTableView = nullptr;
        nri::DescriptorSet* mFallbackSet1 = nullptr;

        // Owned: Works / Terms / CrowdInstance (HOST_UPLOAD) + TRSMatrix (DEVICE, written here).
        BufferHandle mWorkBuffer{};
        BufferHandle mTermBuffer{};
        BufferHandle mCrowdInstanceBuffer{};
        BufferHandle mSlotBuffer{}; // == NodeTransformBuffer
        BufferHandle mTRSMatrixBuffer{};
        BufferViewHandle mWorkView{};
        BufferViewHandle mTermView{};
        BufferViewHandle mSlotView{};
        BufferViewHandle mTRSMatrixStorageView{};

        // Skeleton tables, uploaded here and consumed by BoneMatrixComputePass.
        BufferHandle mNodeParentIndexBuffer{};
        BufferHandle mBoneNodeIndexBuffer{};
        BufferHandle mBoneOffsetMatrixBuffer{};
        BufferHandle mModelRootMatrixBuffer{};

        // Per-frame CPU-side scratch, filled in Upload(), consumed in Record() and by sibling passes.
        std::vector<HybridWork> mWorks;
        std::vector<HybridTerm> mTerms;
        std::vector<RNodeTransformData> mSlotData;
        std::vector<CrowdInstanceData> mCrowdInstances;
        std::vector<int32_t> mNodeParentIndices;
        std::vector<uint32_t> mBoneNodeIndices;
        std::vector<glm::mat4> mBoneOffsetMatrices;
        std::vector<glm::mat4> mModelRootMatrices;
        std::vector<AnimatedDispatch> mAnimatedDispatches;
        std::vector<AnimatedGroup> mAnimatedGroups;
        uint32_t mTrsCursor = 0;

        Timer mUploadTimer{};
    };
} // namespace RAnimation
