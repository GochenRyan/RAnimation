#pragma once

#include <vector>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Post-process selection outline. Samples the R32_UINT pick-ID target (written by the scene
    // MRT pass and transitioned to SHADER_RESOURCE by the Renderer) and draws a colored edge where
    // the selected instance's silhouette meets the rest of the scene. Runs in the PostProcess phase
    // inside its own CmdBeginRendering(color only, LOAD) with alpha blending over the scene color.
    class OutlinePass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "OutlinePass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::PostProcess; }

        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

        // Re-point the descriptor set at the pick-ID SRV. Called by the Renderer after the pick-ID
        // texture (and its views) are (re)created, since the SRV descriptor changes on resize.
        void SetIdTextureSRV(RRenderData& renderData, nri::Descriptor* idTextureSRV);

    private:
        nri::PipelineLayout* mPipelineLayout = nullptr;
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mTextureRanges;
        nri::DescriptorSet* mDescriptorSet = nullptr;
    };
} // namespace RAnimation
