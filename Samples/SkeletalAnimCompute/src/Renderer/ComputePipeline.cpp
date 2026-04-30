#include <RHIWrap/Helper.h>
#include <RHIWrap/NRIInterface.h>
#include <Model/RenderData.h>
#include <Renderer/ComputePipeline.h>

using namespace RAnimation;

bool ComputePipeline::Init(RRenderData& renderData,
                           nri::PipelineLayout& pipelineLayout,
                           nri::Pipeline*& pipeline,
                           std::string computeShaderFilename)
{
    const nri::DeviceDesc& deviceDesc = renderData.NRI.GetDeviceDesc(*renderData.rdDevice);
    utils::ShaderCodeStorage shaderCodeStorage;
    nri::ShaderDesc shaderStages = utils::LoadShader(deviceDesc.graphicsAPI, computeShaderFilename, shaderCodeStorage);
    nri::ComputePipelineDesc computePipelineDesc = {};
    computePipelineDesc.pipelineLayout = &pipelineLayout;
    computePipelineDesc.shader = shaderStages;

    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateComputePipeline(*renderData.rdDevice, computePipelineDesc, pipeline));
    return true;
}

void RAnimation::ComputePipeline::Cleanup(RRenderData& renderData, nri::Pipeline* pipeline)
{
    renderData.NRI.DestroyPipeline(pipeline);
}