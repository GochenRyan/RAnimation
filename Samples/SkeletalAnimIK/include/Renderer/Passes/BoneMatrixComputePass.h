#pragma once

#include <vector>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Compute pass: multiplies parent-chained TRS + bone offset to produce final bone matrices.
    // Uses two descriptor sets (set 0: TRS SR + Bone storage; set 1: parent index / bone offset / model root / bone node index).
    // PassRegistry auto-generates the TRS UAV->SR barrier (from DeclareAccess), and the
    // BoneMatrix UAV->VS-SR barrier is triggered by SkinnedMeshDrawPass's DeclareAccess.
    class BoneMatrixComputePass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "BoneMatrixComputePass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::Compute; }

        bool DeclareResources(ResourceContext& context) override;
        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void DeclareAccess(RegistryAccessBuilder& builder) const override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

    private:
        nri::PipelineLayout* mPipelineLayout = nullptr;
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges0;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges1;
        std::vector<nri::DescriptorSet*> mDescriptorSet0PerFrame;
        std::vector<nri::DescriptorSet*> mDescriptorSet1PerFrame;

        BufferHandle mTRSMatrixBuffer{};
        BufferHandle mBoneMatrixBuffer{};
        BufferHandle mNodeParentIndexBuffer{};
        BufferHandle mBoneOffsetMatrixBuffer{};
        BufferHandle mModelRootMatrixBuffer{};
        BufferHandle mBoneNodeIndexBuffer{};
        BufferViewHandle mTRSMatrixView{};
        BufferViewHandle mBoneMatrixStorageView{};
        BufferViewHandle mNodeParentIndexView{};
        BufferViewHandle mBoneOffsetMatrixView{};
        BufferViewHandle mModelRootMatrixView{};
        BufferViewHandle mBoneNodeIndexView{};
    };
} // namespace RAnimation
