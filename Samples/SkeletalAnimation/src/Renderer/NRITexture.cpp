#include <fmt/base.h>
#include <fmt/color.h>

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

    if (!UploadToGPU(renderData, texData))
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: could not load texture '{}'\n", __FUNCTION__, texData.texture.name);
        return false;
    }

    return true;
}

bool NRITexture::UploadToGPU(RRenderData& renderData, RTextureData& texData)
{
    // Prepare subresources
    std::vector<nri::TextureSubresourceUploadDesc> subresources(texData.texture.GetMipNum());
    for (uint32_t mip = 0; mip < texData.texture.GetMipNum(); mip++)
        texData.texture.GetSubresource(subresources[mip], mip);

    // Upload
    nri::TextureUploadDesc textureUpload = {};
    textureUpload.texture = texData.nriTexture;
    textureUpload.subresources = subresources.data();
    textureUpload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE};

    NRI_ABORT_ON_FAILURE(renderData.NRI.UploadData(*renderData.rdGraphicsQueue, &textureUpload, 1, nullptr, 0));

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
    renderData.NRI.DestroyDescriptor(texData.descriptor);
    renderData.NRI.DestroyTexture(texData.nriTexture);
    renderData.NRI.FreeMemory(texData.memory);
}