#include <Renderer/NRITrsTexture.h>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <Model/Model.h>
#include <Model/RenderData.h>
#include <RHIWrap/Helper.h>
#include <TrsTexture/TrsTexture.h>

using namespace RAnimation;

bool NRITrsTexture::Load(RRenderData& renderData, Model& model)
{
    TrsTextureGpu& gpu = model.TrsTexGpu();
    Release(renderData, gpu);

    if (!model.IsTrsTexReady())
    {
        return false;
    }
    const BoneTrsTexture& cpu = model.GetTrsTexCpu();

    if (renderData.rdHybridEvalPipelineLayout == nullptr || renderData.rdDescriptorPool == nullptr)
    {
        model.SetTrsTexStatus("GPU not initialised before model load");
        return false;
    }
    if (renderData.rdTrsTextureSetFreeList.empty())
    {
        model.SetTrsTexStatus("no free TRS descriptor set left (kMaxTrsTextureModels reached for this session)");
        return false;
    }

    // ---- texture --------------------------------------------------------------------------------------
    nri::TextureDesc textureDesc = {};
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    textureDesc.usage = nri::TextureUsageBits::SHADER_RESOURCE;
    textureDesc.format = nri::Format::RGBA16_SFLOAT;
    textureDesc.width = static_cast<nri::Dim_t>(cpu.Width());
    textureDesc.height = static_cast<nri::Dim_t>(cpu.Height());
    textureDesc.mipNum = 1;
    textureDesc.layerNum = 1;
    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateTexture(*renderData.rdDevice, textureDesc, gpu.texture));

    nri::ResourceGroupDesc textureGroup = {};
    textureGroup.memoryLocation = nri::MemoryLocation::DEVICE;
    textureGroup.textureNum = 1;
    textureGroup.textures = &gpu.texture;
    NRI_ABORT_ON_FAILURE(renderData.NRI.AllocateAndBindMemory(*renderData.rdDevice, textureGroup, &gpu.textureMemory));

    nri::TextureSubresourceUploadDesc subresource = {};
    subresource.slices = cpu.Texels().data();
    subresource.sliceNum = 1;
    subresource.rowPitch = cpu.Width() * 8u; // 4 x half
    subresource.slicePitch = subresource.rowPitch * cpu.Height();

    nri::TextureUploadDesc textureUpload = {};
    textureUpload.texture = gpu.texture;
    textureUpload.subresources = &subresource;
    textureUpload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE};

    // ---- clip table -----------------------------------------------------------------------------------
    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = cpu.Clips().size() * sizeof(TrsTexClipRecord);
    bufferDesc.structureStride = sizeof(TrsTexClipRecord);
    bufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;
    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateBuffer(*renderData.rdDevice, bufferDesc, gpu.clipTable));

    nri::ResourceGroupDesc bufferGroup = {};
    bufferGroup.memoryLocation = nri::MemoryLocation::DEVICE;
    bufferGroup.bufferNum = 1;
    bufferGroup.buffers = &gpu.clipTable;
    NRI_ABORT_ON_FAILURE(renderData.NRI.AllocateAndBindMemory(*renderData.rdDevice, bufferGroup, &gpu.clipTableMemory));

    nri::BufferUploadDesc bufferUpload = {cpu.Clips().data(), gpu.clipTable, {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::COMPUTE_SHADER}};

    // Blocking upload; the caller (Renderer::LoadModel) brackets model loads with QueueWaitIdle.
    NRI_ABORT_ON_FAILURE(renderData.NRI.UploadData(*renderData.rdGraphicsQueue, &textureUpload, 1, &bufferUpload, 1));

    // ---- views + descriptor set -----------------------------------------------------------------------
    nri::Texture2DViewDesc textureViewDesc = {gpu.texture, nri::Texture2DViewType::SHADER_RESOURCE, nri::Format::RGBA16_SFLOAT};
    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateTexture2DView(textureViewDesc, gpu.textureView));

    nri::BufferViewDesc clipViewDesc = {};
    clipViewDesc.buffer = gpu.clipTable;
    clipViewDesc.viewType = nri::BufferViewType::SHADER_RESOURCE;
    clipViewDesc.offset = 0;
    clipViewDesc.size = bufferDesc.size;
    clipViewDesc.structureStride = sizeof(TrsTexClipRecord);
    NRI_ABORT_ON_FAILURE(renderData.NRI.CreateBufferView(clipViewDesc, gpu.clipTableView));

    gpu.set1 = renderData.rdTrsTextureSetFreeList.back();
    renderData.rdTrsTextureSetFreeList.pop_back();

    nri::Descriptor* descriptors[] = {gpu.textureView, gpu.clipTableView};
    nri::UpdateDescriptorRangeDesc ranges[] = {
            {gpu.set1, 0, 0, &descriptors[0], 1},
            {gpu.set1, 1, 0, &descriptors[1], 1},
    };
    renderData.NRI.UpdateDescriptorRanges(ranges, helper::GetCountOf(ranges));

    fmt::print("NRITrsTexture: uploaded {}x{} RGBA16F ({} KB) for '{}'\n",
               cpu.Width(),
               cpu.Height(),
               cpu.TexelBytes() / 1024,
               model.GetModelFileName());
    return true;
}

void NRITrsTexture::Release(RRenderData& renderData, TrsTextureGpu& gpu)
{
    if (gpu.textureView != nullptr)
    {
        renderData.NRI.DestroyDescriptor(gpu.textureView);
    }
    if (gpu.clipTableView != nullptr)
    {
        renderData.NRI.DestroyDescriptor(gpu.clipTableView);
    }
    if (gpu.texture != nullptr)
    {
        renderData.NRI.DestroyTexture(gpu.texture);
    }
    if (gpu.clipTable != nullptr)
    {
        renderData.NRI.DestroyBuffer(gpu.clipTable);
    }
    if (gpu.textureMemory != nullptr)
    {
        renderData.NRI.FreeMemory(gpu.textureMemory);
    }
    if (gpu.clipTableMemory != nullptr)
    {
        renderData.NRI.FreeMemory(gpu.clipTableMemory);
    }
    if (gpu.set1 != nullptr)
    {
        renderData.rdTrsTextureSetFreeList.push_back(gpu.set1);
    }
    gpu = TrsTextureGpu{};
}
