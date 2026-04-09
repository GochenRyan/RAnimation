#include <RHIWrap/Helper.h>
#include <RHIWrap/NRIInterface.h>
#include <Model/RenderData.h>
#include <Renderer/SkinningPipeline.h>

using namespace RAnimation;

bool SkinningPipeline::Init(RRenderData& renderData,
                            nri::PipelineLayout& pipelineLayout,
                            nri::Pipeline* pipeline,
                            std::string vertexShaderFilename,
                            std::string fragmentShaderFilename)
{
    // Pipeline
    const nri::DeviceDesc& deviceDesc = renderData.NRI.GetDeviceDesc(*renderData.rdDevice);
    utils::ShaderCodeStorage shaderCodeStorage;
    {
        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;

        nri::VertexAttributeDesc vertexAttributeDesc[6] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
            vertexAttributeDesc[0].offset = offsetof(RVertex, position);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk = {0};

            vertexAttributeDesc[1].format = nri::Format::RGBA32_SFLOAT;
            vertexAttributeDesc[1].offset = offsetof(RVertex, color);
            vertexAttributeDesc[1].d3d = {"COLOR", 0};
            vertexAttributeDesc[1].vk = {1};

            vertexAttributeDesc[2].format = nri::Format::RGB32_SFLOAT;
            vertexAttributeDesc[2].offset = offsetof(RVertex, normal);
            vertexAttributeDesc[2].d3d = {"NORMAL", 0};
            vertexAttributeDesc[2].vk = {2};

            vertexAttributeDesc[3].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[3].offset = offsetof(RVertex, uv);
            vertexAttributeDesc[3].d3d = {"TEXCOORD", 0};
            vertexAttributeDesc[3].vk = {3};

            vertexAttributeDesc[4].format = nri::Format::RGBA32_UINT;
            vertexAttributeDesc[4].offset = offsetof(RVertex, boneNumber);
            vertexAttributeDesc[4].d3d = {"BLENDINDICES", 0};
            vertexAttributeDesc[4].vk = {4};

            vertexAttributeDesc[5].format = nri::Format::RGBA32_SFLOAT;
            vertexAttributeDesc[5].offset = offsetof(RVertex, boneWeight);
            vertexAttributeDesc[5].d3d = {"BLENDWEIGHT", 0};
            vertexAttributeDesc[5].vk = {5};
        }


        nri::VertexInputDesc vertexInputDesc = {};
        vertexInputDesc.attributes = vertexAttributeDesc;
        vertexInputDesc.attributeNum = (uint8_t) helper::GetCountOf(vertexAttributeDesc);
        vertexInputDesc.streams = &vertexStreamDesc;
        vertexInputDesc.streamNum = 1;

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::BACK;
        rasterizationDesc.frontCounterClockwise = true;
        rasterizationDesc.shadingRate = deviceDesc.tiers.shadingRate != 0;

        nri::MultisampleDesc multisampleDesc = {};
        multisampleDesc.sampleNum = 1;
        multisampleDesc.sampleLocations = deviceDesc.tiers.sampleLocations >= 2;

        uint32_t swapChainTextureNum;
        nri::Texture* const* swapChainTextures = renderData.NRI.GetSwapChainTextures(*renderData.rdSwapChain,
                                                                                     swapChainTextureNum);
        nri::Format swapChainFormat = renderData.NRI.GetTextureDesc(*swapChainTextures[0]).format;

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = swapChainFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDesc.blendEnabled = false;

        nri::DepthAttachmentDesc depthAttachmentDesc = {};
        depthAttachmentDesc.compareOp = nri::CompareOp::LESS_EQUAL;
        depthAttachmentDesc.write = true;
        depthAttachmentDesc.boundsTest = false;

        nri::StencilAttachmentDesc stencilAttachmentDesc = {};
        stencilAttachmentDesc.front.compareOp = nri::CompareOp::NONE;
        stencilAttachmentDesc.back.compareOp = nri::CompareOp::NONE;

        renderData.rdDepthFormat = nri::GetSupportedDepthFormat(renderData.NRI, *renderData.rdDevice, 24, true);

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colors = &colorAttachmentDesc;
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.depthStencilFormat = renderData.rdDepthFormat;
        outputMergerDesc.depth = depthAttachmentDesc;
        outputMergerDesc.stencil = stencilAttachmentDesc;

        nri::ShaderDesc shaderStages[] = {

            utils::LoadShader(deviceDesc.graphicsAPI, vertexShaderFilename, shaderCodeStorage),
            utils::LoadShader(deviceDesc.graphicsAPI, fragmentShaderFilename, shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = &pipelineLayout;
        graphicsPipelineDesc.vertexInput = &vertexInputDesc;
        graphicsPipelineDesc.inputAssembly = inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = rasterizationDesc;
        graphicsPipelineDesc.multisample = &multisampleDesc;
        graphicsPipelineDesc.outputMerger = outputMergerDesc;
        graphicsPipelineDesc.shaders = shaderStages;
        graphicsPipelineDesc.shaderNum = helper::GetCountOf(shaderStages);

        NRI_ABORT_ON_FAILURE(renderData.NRI.CreateGraphicsPipeline(*renderData.rdDevice, graphicsPipelineDesc, pipeline));
    }

    return true;
}

void SkinningPipeline::Cleanup(RRenderData& renderData, nri::Pipeline* pipeline)
{
    renderData.NRI.DestroyPipeline(pipeline);
}
