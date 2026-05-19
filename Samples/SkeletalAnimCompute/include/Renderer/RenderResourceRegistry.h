#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <NRI.h>

#include <Renderer/RenderResourceBudget.h>
#include <RHIWrap/NRIInterface.h>

namespace RAnimation
{
    struct RRenderData;

    struct BufferHandle
    {
        uint32_t index = std::numeric_limits<uint32_t>::max();

        bool IsValid() const { return index != std::numeric_limits<uint32_t>::max(); }
        bool operator==(const BufferHandle& other) const { return index == other.index; }
        bool operator!=(const BufferHandle& other) const { return index != other.index; }
    };

    struct BufferViewHandle
    {
        uint32_t index = std::numeric_limits<uint32_t>::max();

        bool IsValid() const { return index != std::numeric_limits<uint32_t>::max(); }
        bool operator==(const BufferViewHandle& other) const { return index == other.index; }
        bool operator!=(const BufferViewHandle& other) const { return index != other.index; }
    };

    // Owns NRI buffers + per-queued-frame descriptors + per-queued-frame offsets.
    // Replaces the scattered nri::Descriptor*/offset fields previously held in QueuedFrame.
    class RenderResourceRegistry
    {
    public:
        // -- Declaration phase (call before CreateBuffers) --
        BufferHandle RegisterBuffer(const BufferDesc& desc);

        // viewStructureStride == 0 inherits structureStride from the buffer's BufferDesc.
        BufferViewHandle RegisterView(BufferHandle buffer,
                                      nri::BufferViewType viewType,
                                      uint32_t viewStructureStride = 0);

        // -- Shared resource pool (Phase E: pass-owned declarations) --
        // Idempotent registration keyed by name. First registration wins; subsequent calls with
        // the same name return the existing handle and validate desc/view equivalence (fail-loud
        // on mismatch). Returns invalid handle on validation failure.
        BufferHandle RegisterSharedBuffer(const char* name, const BufferDesc& desc);
        BufferViewHandle RegisterSharedView(const char* name,
                                            BufferHandle buffer,
                                            nri::BufferViewType viewType,
                                            uint32_t viewStructureStride = 0);

        // Lookup-only accessors. Return invalid handle if name was never registered.
        BufferHandle FindBuffer(const char* name) const;
        BufferViewHandle FindView(const char* name) const;

        // -- Creation phase --
        bool CreateBuffers(RRenderData& renderData);
        bool CreateViews(RRenderData& renderData);

        // -- Cleanup --
        void Cleanup(RRenderData& renderData);

        // -- Memory allocation helper --
        std::vector<nri::Buffer*> CollectBuffersByMemoryLocation(nri::MemoryLocation memoryLocation) const;

        // -- Accessors (post-CreateBuffers) --
        nri::Buffer* GetBuffer(BufferHandle h) const;
        const BufferDesc& GetBufferDesc(BufferHandle h) const;
        uint64_t GetAlignedSizePerFrame(BufferHandle h) const;
        uint64_t GetOffsetForFrame(BufferHandle h, uint32_t frameIndex) const;

        // -- View accessors (post-CreateViews) --
        nri::Descriptor* GetView(BufferViewHandle h, uint32_t frameIndex) const;
        BufferHandle GetViewBuffer(BufferViewHandle h) const;

    private:
        struct BufferEntry
        {
            BufferDesc desc;
            nri::Buffer* buffer = nullptr;
            uint64_t alignedSizePerFrame = 0;
            uint32_t queuedFrameNum = 1;
        };

        struct ViewEntry
        {
            BufferHandle buffer;
            nri::BufferViewType viewType = nri::BufferViewType::SHADER_RESOURCE;
            uint32_t structureStride = 0;
            std::vector<nri::Descriptor*> perFrameDescriptors;
        };

        std::vector<BufferEntry> mBuffers;
        std::vector<ViewEntry> mViews;
        std::unordered_map<std::string, BufferHandle> mNamedBuffers;
        std::unordered_map<std::string, BufferViewHandle> mNamedViews;
    };
} // namespace RAnimation
