#pragma once

#include <vector>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Compute pass: multiplies parent-chained TRS + bone offset to produce final bone matrices.
    // Uses two descriptor sets (set 0: TRS SR + Bone storage; set 1: parent index / bone offset / model root / bone node index).
    // Inserts the TRS UAV->SR barrier before its dispatches, and the BoneMatrix UAV->VS-SR barrier after.
    class BoneMatrixComputePass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "BoneMatrixComputePass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::Compute; }

        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

    private:
        nri::PipelineLayout* mPipelineLayout = nullptr;
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges0;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges1;
        std::vector<nri::DescriptorSet*> mDescriptorSet0PerFrame;
        std::vector<nri::DescriptorSet*> mDescriptorSet1PerFrame;
    };
} // namespace RAnimation
