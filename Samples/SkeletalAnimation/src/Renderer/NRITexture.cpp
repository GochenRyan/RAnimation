#include <Model/RenderData.h>

#include <RHIWrap/NRIInterface.h>
#include <Renderer/NRITexture.h>

using namespace RAnimation;

bool NRITexture::LoadTexture(RRenderData& renderData, RTextureData& texData)
{
    // Texture
    nri::TextureDesc textureDesc = {};
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    textureDesc.usage = nri::TextureUsageBits::SHADER_RESOURCE;
    textureDesc.format = texData.texture.GetFormat();
    textureDesc.width = texData.texture.GetWidth();
    textureDesc.height = texData.texture.GetHeight();
    textureDesc.mipNum = texData.texture.GetMipNum();
    textureDesc.layerNum = texData.texture.GetArraySize();

    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateTexture(*renderData.rdDevice, textureDesc, texData.nriTexture));

    // Memory
    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &texData.nriTexture;
    NRI_ABORT_ON_FAILURE(
            renderData.NRI.AllocateAndBindMemory(*renderData.rdDevice, resourceGroupDesc, &texData.memory));

    // Descriptor
    nri::Texture2DViewDesc texture2DViewDesc = {texData.nriTexture,
                                                nri::Texture2DViewType::SHADER_RESOURCE,
                                                texData.texture.GetFormat()};
    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateTexture2DView(texture2DViewDesc, texData.descriptor));

    // In NRI, there is no independent DescriptorSetLayout interface. Instead, the ranges of all sets and
    // bindings are described directly when creating the PipelineLayout.
    return true;
}

void NRITexture::Cleanup(RRenderData& renderData, RTextureData& texData)
{
}