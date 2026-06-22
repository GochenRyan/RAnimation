#include <Renderer/Passes/ImguiPass.h>

#include <imgui.h>

#include <Model/RenderData.h>

namespace RAnimation
{
    bool ImguiPass::CreatePipeline(RenderContext& context)
    {
        // ImGui pipeline is owned by the NRI Imgui extension (constructed during ImguiPass::CreateDescriptors? no,
        // owned by UserInterface::Init which calls CreateImgui()). Nothing to do here.
        (void)context;
        return true;
    }

    DescriptorPoolRequirements ImguiPass::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        // NRI Imgui manages its own descriptor sets internally; no Renderer-side pool allocation needed.
        (void)queuedFrameNum;
        return {};
    }

    bool ImguiPass::CreateDescriptors(FrameContext& context)
    {
        (void)context;
        return true;
    }

    bool ImguiPass::HasDrawData()
    {
        ImDrawData* drawData = ImGui::GetDrawData();
        return drawData != nullptr && drawData->CmdListsCount > 0;
    }

    void ImguiPass::RecordCopyImguiData(CommandContext& context)
    {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData == nullptr || drawData->CmdListsCount <= 0)
        {
            return;
        }

        nri::CopyImguiDataDesc copyImguiDataDesc = {};
        copyImguiDataDesc.drawLists = drawData->CmdLists.Data;
        copyImguiDataDesc.drawListNum = static_cast<uint32_t>(drawData->CmdLists.Size);
        copyImguiDataDesc.textures = drawData->Textures != nullptr ? drawData->Textures->Data : nullptr;
        copyImguiDataDesc.textureNum = drawData->Textures != nullptr ? static_cast<uint32_t>(drawData->Textures->Size)
                                                                     : 0;
        context.NRI.CmdCopyImguiData(context.commandBuffer,
                                     *context.renderData.rdStreamer,
                                     *context.renderData.rdImgui,
                                     copyImguiDataDesc);
    }

    void ImguiPass::Record(CommandContext& context)
    {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData == nullptr || drawData->CmdListsCount <= 0)
        {
            return;
        }

        const QueuedFrame& queuedFrame = context.renderData.rdQueuedFrames[context.frameIndex];

        nri::DrawImguiDesc drawImguiDesc = {};
        drawImguiDesc.drawLists = drawData->CmdLists.Data;
        drawImguiDesc.drawListNum = static_cast<uint32_t>(drawData->CmdLists.Size);
        drawImguiDesc.displaySize = {static_cast<uint16_t>(drawData->DisplaySize.x),
                                     static_cast<uint16_t>(drawData->DisplaySize.y)};
        drawImguiDesc.attachmentFormat =
                context.renderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].attachmentFormat;
        drawImguiDesc.linearColor = true;
        drawImguiDesc.hdrScale = 1.0f;
        context.NRI.CmdDrawImgui(context.commandBuffer, *context.renderData.rdImgui, drawImguiDesc);
    }

    void ImguiPass::Cleanup(RRenderData& renderData)
    {
        // No NRI resources owned (UserInterface manages rdImgui).
        (void)renderData;
    }
} // namespace RAnimation
