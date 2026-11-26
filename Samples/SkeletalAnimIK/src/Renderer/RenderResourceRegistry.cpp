#include <Renderer/RenderResourceRegistry.h>

#include <limits>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/RenderData.h>
#include <RHIWrap/Helper.h>

namespace RAnimation
{
    namespace
    {
        bool HasUsage(nri::BufferUsageBits usage, nri::BufferUsageBits bit)
        {
            return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0;
        }

        bool CheckedMultiply(uint64_t lhs, uint64_t rhs, uint64_t& result)
        {
            if (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs)
            {
                return false;
            }

            result = lhs * rhs;
            return true;
        }

        uint64_t GetBufferAlignment(const RRenderData& renderData, const BufferDesc& bufferDesc)
        {
            const nri::DeviceDesc& deviceDesc = renderData.NRI.GetDeviceDesc(*renderData.rdDevice);
            if (HasUsage(bufferDesc.usage, nri::BufferUsageBits::CONSTANT_BUFFER))
            {
                return deviceDesc.memoryAlignment.constantBufferOffset;
            }

            return deviceDesc.memoryAlignment.bufferShaderResourceOffset;
        }

        uint64_t ComputeAlignedSizePerFrame(const RRenderData& renderData, const BufferDesc& bufferDesc)
        {
            return helper::Align<uint64_t>(bufferDesc.GetUnalignedSize(), GetBufferAlignment(renderData, bufferDesc));
        }

        bool ValidateBufferDesc(const RRenderData& renderData, const BufferDesc& bufferDesc)
        {
            if (bufferDesc.elementSize == 0 || bufferDesc.elementCount == 0)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: buffer '{}' has invalid element size/count ({}, {})\n",
                           bufferDesc.name,
                           bufferDesc.elementSize,
                           bufferDesc.elementCount);
                return false;
            }

            uint64_t unalignedSize = 0;
            if (!CheckedMultiply(bufferDesc.elementSize, bufferDesc.elementCount, unalignedSize))
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: buffer '{}' size overflows uint64_t\n",
                           bufferDesc.name);
                return false;
            }

            const nri::DeviceDesc& deviceDesc = renderData.NRI.GetDeviceDesc(*renderData.rdDevice);
            const uint64_t sizePerFrame = ComputeAlignedSizePerFrame(renderData, bufferDesc);
            const uint64_t frameNum = bufferDesc.perQueuedFrame ? renderData.GetQueuedFrameNum() : 1;
            uint64_t totalSize = 0;
            if (!CheckedMultiply(sizePerFrame, frameNum, totalSize))
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: buffer '{}' total size overflows uint64_t\n",
                           bufferDesc.name);
                return false;
            }

            if (deviceDesc.memory.bufferMaxSize > 0 && totalSize > deviceDesc.memory.bufferMaxSize)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: buffer '{}' allocation size {} exceeds device "
                           "bufferMaxSize {}\n",
                           bufferDesc.name,
                           totalSize,
                           deviceDesc.memory.bufferMaxSize);
                return false;
            }

            if (HasUsage(bufferDesc.usage, nri::BufferUsageBits::CONSTANT_BUFFER) &&
                deviceDesc.memory.constantBufferMaxRange > 0 &&
                sizePerFrame > deviceDesc.memory.constantBufferMaxRange)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: buffer '{}' per-frame size {} exceeds "
                           "constantBufferMaxRange {}\n",
                           bufferDesc.name,
                           sizePerFrame,
                           deviceDesc.memory.constantBufferMaxRange);
                return false;
            }

            if (HasUsage(bufferDesc.usage, nri::BufferUsageBits::SHADER_RESOURCE_STORAGE) &&
                deviceDesc.memory.storageBufferMaxRange > 0 &&
                sizePerFrame > deviceDesc.memory.storageBufferMaxRange)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: buffer '{}' per-frame size {} exceeds "
                           "storageBufferMaxRange {}\n",
                           bufferDesc.name,
                           sizePerFrame,
                           deviceDesc.memory.storageBufferMaxRange);
                return false;
            }

            return true;
        }
    } // namespace

    BufferHandle RenderResourceRegistry::RegisterBuffer(const BufferDesc& desc)
    {
        BufferEntry entry = {};
        entry.desc = desc;
        mBuffers.push_back(std::move(entry));

        BufferHandle handle = {};
        handle.index = static_cast<uint32_t>(mBuffers.size() - 1);
        return handle;
    }

    BufferViewHandle RenderResourceRegistry::RegisterView(BufferHandle buffer,
                                                          nri::BufferViewType viewType,
                                                          uint32_t viewStructureStride)
    {
        ViewEntry entry = {};
        entry.buffer = buffer;
        entry.viewType = viewType;
        entry.structureStride = viewStructureStride;
        mViews.push_back(std::move(entry));

        BufferViewHandle handle = {};
        handle.index = static_cast<uint32_t>(mViews.size() - 1);
        return handle;
    }

    BufferHandle RenderResourceRegistry::RegisterSharedBuffer(const char* name, const BufferDesc& desc)
    {
        auto it = mNamedBuffers.find(name);
        if (it != mNamedBuffers.end())
        {
            const BufferDesc& existing = mBuffers[it->second.index].desc;
            const bool equivalent = existing.elementSize == desc.elementSize &&
                                    existing.elementCount == desc.elementCount &&
                                    existing.structureStride == desc.structureStride &&
                                    existing.usage == desc.usage &&
                                    existing.memoryLocation == desc.memoryLocation &&
                                    existing.perQueuedFrame == desc.perQueuedFrame;
            if (!equivalent)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: shared buffer '{}' re-registered with a "
                           "different BufferDesc (elementSize {} vs {}, elementCount {} vs {}, "
                           "structureStride {} vs {}, usage {:#x} vs {:#x}, memoryLocation {} vs {}, "
                           "perQueuedFrame {} vs {})\n",
                           name,
                           existing.elementSize,
                           desc.elementSize,
                           existing.elementCount,
                           desc.elementCount,
                           existing.structureStride,
                           desc.structureStride,
                           static_cast<uint32_t>(existing.usage),
                           static_cast<uint32_t>(desc.usage),
                           static_cast<uint32_t>(existing.memoryLocation),
                           static_cast<uint32_t>(desc.memoryLocation),
                           existing.perQueuedFrame,
                           desc.perQueuedFrame);
                return BufferHandle{};
            }
            return it->second;
        }

        BufferHandle handle = RegisterBuffer(desc);
        mNamedBuffers.emplace(name, handle);
        return handle;
    }

    BufferViewHandle RenderResourceRegistry::RegisterSharedView(const char* name,
                                                                BufferHandle buffer,
                                                                nri::BufferViewType viewType,
                                                                uint32_t viewStructureStride)
    {
        auto it = mNamedViews.find(name);
        if (it != mNamedViews.end())
        {
            const ViewEntry& existing = mViews[it->second.index];
            const bool equivalent = existing.buffer == buffer && existing.viewType == viewType &&
                                    existing.structureStride == viewStructureStride;
            if (!equivalent)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: shared view '{}' re-registered with a "
                           "different buffer/type/stride (buffer {} vs {}, viewType {} vs {}, "
                           "structureStride {} vs {})\n",
                           name,
                           existing.buffer.index,
                           buffer.index,
                           static_cast<uint32_t>(existing.viewType),
                           static_cast<uint32_t>(viewType),
                           existing.structureStride,
                           viewStructureStride);
                return BufferViewHandle{};
            }
            return it->second;
        }

        BufferViewHandle handle = RegisterView(buffer, viewType, viewStructureStride);
        mNamedViews.emplace(name, handle);
        return handle;
    }

    BufferHandle RenderResourceRegistry::FindBuffer(const char* name) const
    {
        auto it = mNamedBuffers.find(name);
        return it != mNamedBuffers.end() ? it->second : BufferHandle{};
    }

    BufferViewHandle RenderResourceRegistry::FindView(const char* name) const
    {
        auto it = mNamedViews.find(name);
        return it != mNamedViews.end() ? it->second : BufferViewHandle{};
    }

    bool RenderResourceRegistry::CreateBuffers(RRenderData& renderData)
    {
        for (BufferEntry& entry : mBuffers)
        {
            if (!ValidateBufferDesc(renderData, entry.desc))
            {
                return false;
            }

            entry.alignedSizePerFrame = ComputeAlignedSizePerFrame(renderData, entry.desc);
            entry.queuedFrameNum = entry.desc.perQueuedFrame ? renderData.GetQueuedFrameNum() : 1;

            nri::BufferDesc nriBufferDesc = {};
            nriBufferDesc.size = entry.alignedSizePerFrame * entry.queuedFrameNum;
            nriBufferDesc.structureStride = entry.desc.structureStride;
            nriBufferDesc.usage = entry.desc.usage;

            NRI_ABORT_ON_FAILURE(renderData.NRI.CreateBuffer(*renderData.rdDevice, nriBufferDesc, entry.buffer));
        }

        return true;
    }

    bool RenderResourceRegistry::CreateViews(RRenderData& renderData)
    {
        for (ViewEntry& view : mViews)
        {
            if (!view.buffer.IsValid() || view.buffer.index >= mBuffers.size())
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "RenderResourceRegistry error: view references invalid buffer handle\n");
                return false;
            }

            const BufferEntry& bufferEntry = mBuffers[view.buffer.index];
            const uint32_t frameNum = bufferEntry.queuedFrameNum;
            view.perFrameDescriptors.assign(frameNum, nullptr);

            const uint32_t structureStride =
                    view.structureStride != 0 ? view.structureStride : bufferEntry.desc.structureStride;

            for (uint32_t frameIndex = 0; frameIndex < frameNum; ++frameIndex)
            {
                nri::BufferViewDesc viewDesc = {};
                viewDesc.buffer = bufferEntry.buffer;
                viewDesc.viewType = view.viewType;
                viewDesc.offset = bufferEntry.alignedSizePerFrame * frameIndex;
                viewDesc.size = bufferEntry.alignedSizePerFrame;
                viewDesc.structureStride = structureStride;

                NRI_ABORT_ON_FAILURE(
                        renderData.NRI.CreateBufferView(viewDesc, view.perFrameDescriptors[frameIndex]));
            }
        }

        return true;
    }

    void RenderResourceRegistry::Cleanup(RRenderData& renderData)
    {
        mNamedViews.clear();
        mNamedBuffers.clear();

        if (renderData.rdDevice == nullptr)
        {
            mViews.clear();
            mBuffers.clear();
            return;
        }

        for (ViewEntry& view : mViews)
        {
            for (nri::Descriptor* descriptor : view.perFrameDescriptors)
            {
                if (descriptor != nullptr)
                {
                    renderData.NRI.DestroyDescriptor(descriptor);
                }
            }
            view.perFrameDescriptors.clear();
        }
        mViews.clear();

        for (BufferEntry& entry : mBuffers)
        {
            if (entry.buffer != nullptr)
            {
                renderData.NRI.DestroyBuffer(entry.buffer);
                entry.buffer = nullptr;
            }
        }
        mBuffers.clear();
    }

    std::vector<nri::Buffer*> RenderResourceRegistry::CollectBuffersByMemoryLocation(
            nri::MemoryLocation memoryLocation) const
    {
        std::vector<nri::Buffer*> result;
        result.reserve(mBuffers.size());
        for (const BufferEntry& entry : mBuffers)
        {
            if (entry.desc.memoryLocation == memoryLocation && entry.buffer != nullptr)
            {
                result.push_back(entry.buffer);
            }
        }
        return result;
    }

    nri::Buffer* RenderResourceRegistry::GetBuffer(BufferHandle h) const
    {
        return mBuffers[h.index].buffer;
    }

    const BufferDesc& RenderResourceRegistry::GetBufferDesc(BufferHandle h) const
    {
        return mBuffers[h.index].desc;
    }

    uint64_t RenderResourceRegistry::GetAlignedSizePerFrame(BufferHandle h) const
    {
        return mBuffers[h.index].alignedSizePerFrame;
    }

    uint64_t RenderResourceRegistry::GetOffsetForFrame(BufferHandle h, uint32_t frameIndex) const
    {
        const BufferEntry& entry = mBuffers[h.index];
        if (!entry.desc.perQueuedFrame)
        {
            return 0;
        }
        return entry.alignedSizePerFrame * frameIndex;
    }

    nri::Descriptor* RenderResourceRegistry::GetView(BufferViewHandle h, uint32_t frameIndex) const
    {
        const ViewEntry& view = mViews[h.index];
        if (view.perFrameDescriptors.size() == 1)
        {
            return view.perFrameDescriptors[0];
        }
        return view.perFrameDescriptors[frameIndex];
    }

    BufferHandle RenderResourceRegistry::GetViewBuffer(BufferViewHandle h) const
    {
        return mViews[h.index].buffer;
    }
} // namespace RAnimation
