#pragma once

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Records ImGui draw commands using the NRI Imgui extension.
    // Phase = UI: runs inside CmdBeginRendering(color only) wrapping in Renderer.
    // Also performs the pre-pass CmdCopyImguiData via a separate hook called by Renderer
    // before the main begin-rendering barrier (since it needs to happen before scene render targets).
    class ImguiPass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "ImguiPass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::UI; }

        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

        // Called by Renderer before the begin-rendering barrier in order to upload ImGui draw lists
        // to the streamer. Done outside the IRenderPass phase grouping because it must run early.
        static void RecordCopyImguiData(CommandContext& context);

        // Whether ImGui currently has any draw lists.
        static bool HasDrawData();
    };
} // namespace RAnimation
