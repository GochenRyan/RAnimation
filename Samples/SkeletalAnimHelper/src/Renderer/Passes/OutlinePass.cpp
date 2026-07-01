#include <Renderer/Passes/OutlinePass.h>

#include <Model/RenderData.h>
#include <Renderer/SceneFrameData.h>
#include <RHIWrap/Helper.h>

namespace RAnimation
{
    namespace
    {
        struct PushConstants
        {
            uint32_t selectedPickID = 0;
            uint32_t resolutionX = 0;
            uint32_t resolutionY = 0;
            float outlineR = 1.0f;
            float outlineG = 0.6f;
            float outlineB = 0.1f;
        };
    } // namespace

    bool OutlinePass::CreatePipeline(RenderContext& context)
    {
        mTextureRanges = {
                {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER},
        };

        nri::DescriptorSetDesc setDescs[] = {
                {0, mTextureRanges.data(), static_cast<uint32_t>(mTextureRanges.size())},
        };

        nri::RootConstantDesc rootConstantDesc = {};
        rootConstantDesc.registerIndex = 0;
        rootConstantDesc.shaderStages = nri::StageBits::FRAGMENT_SHADER;
        rootConstantDesc.size = sizeof(PushConstants);

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootConstants = &rootConstantDesc;
        layoutDesc.rootRegisterSpace = 1;
        layoutDesc.descriptorSetNum = helper::GetCountOf(setDescs);
        layoutDesc.descriptorSets = setDescs;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        NRI_ABORT_ON_FAILURE(context.NRI.CreatePipelineLayout(context.device, layoutDesc, mPipelineLayout));

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::NONE;

        nri::MultisampleDesc multisampleDesc = {};
        multisampleDesc.sampleNum = 1;

        // Alpha-blend the outline over the existing scene color.
        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = context.swapChainFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDesc.blendEnabled = true;
        colorAttachmentDesc.colorBlend = {nri::BlendFactor::SRC_ALPHA,
                                          nri::BlendFactor::ONE_MINUS_SRC_ALPHA,
                                          nri::BlendOp::ADD};
        colorAttachmentDesc.alphaBlend = {nri::BlendFactor::ONE, nri::BlendFactor::ZERO, nri::BlendOp::ADD};

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colors = &colorAttachmentDesc;
        outputMergerDesc.colorNum = 1;

        const nri::DeviceDesc& deviceDesc = context.NRI.GetDeviceDesc(context.device);
        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shaderStages[] = {
                utils::LoadShader(deviceDesc.graphicsAPI,
                                  SHADER_SRC_DIR "/SkeletalAnimHelper/outline.vs",
                                  shaderCodeStorage),
                utils::LoadShader(deviceDesc.graphicsAPI,
                                  SHADER_SRC_DIR "/SkeletalAnimHelper/outline.fs",
                                  shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = mPipelineLayout;
        graphicsPipelineDesc.vertexInput = nullptr;
        graphicsPipelineDesc.inputAssembly = inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = rasterizationDesc;
        graphicsPipelineDesc.multisample = &multisampleDesc;
        graphicsPipelineDesc.outputMerger = outputMergerDesc;
        graphicsPipelineDesc.shaders = shaderStages;
        graphicsPipelineDesc.shaderNum = helper::GetCountOf(shaderStages);

        NRI_ABORT_ON_FAILURE(context.NRI.CreateGraphicsPipeline(context.device, graphicsPipelineDesc, mPipeline));
        return true;
    }

    DescriptorPoolRequirements OutlinePass::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        (void)queuedFrameNum;
        // Single set: the pick-ID texture is one (non-per-frame) shared resource.
        DescriptorPoolRequirements req = {};
        req.descriptorSetMaxNum = 1;
        req.textureMaxNum = 1;
        return req;
    }

    bool OutlinePass::CreateDescriptors(FrameContext& context)
    {
        NRI_ABORT_ON_FAILURE(
                context.NRI.AllocateDescriptorSets(context.descriptorPool, *mPipelineLayout, 0, &mDescriptorSet, 1, 0));
        // Bound later by Renderer via SetIdTextureSRV once the pick-ID views exist.
        return true;
    }

    void OutlinePass::SetIdTextureSRV(RRenderData& renderData, nri::Descriptor* idTextureSRV)
    {
        if (mDescriptorSet == nullptr || idTextureSRV == nullptr)
        {
            return;
        }

        nri::Descriptor* descriptors[] = {idTextureSRV};
        nri::UpdateDescriptorRangeDesc range = {mDescriptorSet, 0, 0, descriptors, 1};
        renderData.NRI.UpdateDescriptorRanges(&range, 1);
    }

    void OutlinePass::Record(CommandContext& context)
    {
        if (context.sceneFrame == nullptr || context.sceneFrame->selectedPickID == 0)
        {
            return;
        }

        PushConstants pushConstants = {};
        pushConstants.selectedPickID = context.sceneFrame->selectedPickID;
        pushConstants.resolutionX = context.renderData.rdOutputResolution.x;
        pushConstants.resolutionY = context.renderData.rdOutputResolution.y;

        context.NRI.CmdSetPipelineLayout(context.commandBuffer, nri::BindPoint::GRAPHICS, *mPipelineLayout);
        context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
        context.NRI.CmdSetRootConstants(context.commandBuffer,
                                        {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::GRAPHICS});
        context.NRI.CmdSetDescriptorSet(context.commandBuffer, {0, mDescriptorSet, nri::BindPoint::GRAPHICS});
        context.NRI.CmdDraw(context.commandBuffer, {3, 1, 0, 0});
    }

    void OutlinePass::Cleanup(RRenderData& renderData)
    {
        if (mPipeline != nullptr)
        {
            renderData.NRI.DestroyPipeline(mPipeline);
            mPipeline = nullptr;
        }
        if (mPipelineLayout != nullptr)
        {
            renderData.NRI.DestroyPipelineLayout(mPipelineLayout);
            mPipelineLayout = nullptr;
        }
        mTextureRanges.clear();
        mDescriptorSet = nullptr;
    }
} // namespace RAnimation
