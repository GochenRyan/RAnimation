#pragma once

#include <vector>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Compute pass: applies per-node TRS to produce TRS matrices.
    // Reads NodeTransformBuffer (SR), writes TRSMatrixBuffer (SRS).
    // PassRegistry auto-generates the begin-of-compute barrier from DeclareAccess().
    class AnimationTransformComputePass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "AnimationTransformComputePass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::Compute; }

        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void DeclareAccess(const RRenderData& renderData, RegistryAccessBuilder& builder) const override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

    private:
        nri::PipelineLayout* mPipelineLayout = nullptr;
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mDescriptorRanges;
        std::vector<nri::DescriptorSet*> mDescriptorSets;
    };
} // namespace RAnimation
