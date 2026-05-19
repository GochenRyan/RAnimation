#pragma once

#include <cstdint>
#include <vector>

#include <NRI.h>

#include <Renderer/RenderResourceRegistry.h>

struct NRIInterface;

namespace RAnimation
{
    struct RRenderData;
    struct RenderResourceBudget;
    struct SceneFrameData;

    // -- Pass execution phase --
    // Compute passes run before swapchain/depth transitions.
    // Scene passes run inside CmdBeginRendering(color + depth).
    // UI passes run inside CmdBeginRendering(color only) after the scene.
    enum class RenderPassPhase : uint8_t
    {
        Compute,
        Scene,
        UI,
    };

    // -- Contexts --
    struct ResourceContext
    {
        RenderResourceRegistry& registry;
        const RenderResourceBudget& budget;
    };

    struct RenderContext
    {
        RRenderData& renderData;
        NRIInterface& NRI;
        nri::Device& device;
        RenderResourceRegistry& registry;
        const RenderResourceBudget& budget;
        nri::Format swapChainFormat = nri::Format::UNKNOWN;
        nri::Format depthFormat = nri::Format::UNKNOWN;
    };

    struct FrameContext
    {
        RRenderData& renderData;
        NRIInterface& NRI;
        RenderResourceRegistry& registry;
        nri::DescriptorPool& descriptorPool;
        uint32_t queuedFrameNum = 1;
    };

    struct CommandContext
    {
        RRenderData& renderData;
        NRIInterface& NRI;
        RenderResourceRegistry& registry;
        nri::CommandBuffer& commandBuffer;
        nri::DescriptorPool& descriptorPool;
        uint32_t frameIndex = 0;
        const SceneFrameData* sceneFrame = nullptr;
    };

    // -- Resource access declaration (Phase C: auto-barrier generation) --

    struct ResourceAccess
    {
        BufferHandle handle;
        nri::AccessBits access = nri::AccessBits::NONE;
        nri::StageBits stage = nri::StageBits::NONE;
    };

    // Passes call builder.Use(handle, access, stage) inside DeclareAccess to declare what state
    // each buffer must be in at the start of their Record() call. The PassRegistry compares the
    // declared state against the last-known state and inserts barriers automatically.
    //
    // Only declare for DEVICE buffers that need explicit synchronization. HOST_UPLOAD buffers
    // (camera/world/etc.) get implicit host-visible coherence and don't need barriers.
    class RegistryAccessBuilder
    {
    public:
        void Use(BufferHandle handle, nri::AccessBits access, nri::StageBits stage)
        {
            if (!handle.IsValid())
            {
                return;
            }
            mAccesses.push_back({handle, access, stage});
        }

        const std::vector<ResourceAccess>& Accesses() const { return mAccesses; }

    private:
        std::vector<ResourceAccess> mAccesses;
    };

    // -- Descriptor pool sizing requirements --
    struct DescriptorPoolRequirements
    {
        uint32_t descriptorSetMaxNum = 0;
        uint32_t constantBufferMaxNum = 0;
        uint32_t structuredBufferMaxNum = 0;
        uint32_t storageStructuredBufferMaxNum = 0;
        uint32_t textureMaxNum = 0;
        uint32_t samplerMaxNum = 0;

        DescriptorPoolRequirements& operator+=(const DescriptorPoolRequirements& other)
        {
            descriptorSetMaxNum += other.descriptorSetMaxNum;
            constantBufferMaxNum += other.constantBufferMaxNum;
            structuredBufferMaxNum += other.structuredBufferMaxNum;
            storageStructuredBufferMaxNum += other.storageStructuredBufferMaxNum;
            textureMaxNum += other.textureMaxNum;
            samplerMaxNum += other.samplerMaxNum;
            return *this;
        }
    };

    // -- IRenderPass --
    // 5-stage lifecycle:
    //   DeclareResources -> CreatePipeline -> GetDescriptorPoolRequirements -> CreateDescriptors -> Record -> Cleanup
    class IRenderPass
    {
    public:
        virtual ~IRenderPass() = default;

        virtual const char* GetName() const = 0;
        virtual RenderPassPhase GetPhase() const = 0;

        // Register the pass's buffers and views with the shared registry pool. Pass uses
        // RegisterSharedBuffer/View(name, ...) — first registration wins; subsequent calls with
        // the same name return the existing handle. Pass caches returned handles as private
        // members for later use in DeclareAccess / CreateDescriptors / Record.
        virtual bool DeclareResources(ResourceContext& context)
        {
            (void)context;
            return true;
        }

        // Optional: declare buffer accesses for auto-barrier generation. Default no-op.
        // Called once per Record() invocation by the PassRegistry. The pass uses its own private
        // BufferHandle members (cached during DeclareResources) to reference buffers.
        virtual void DeclareAccess(RegistryAccessBuilder& builder) const
        {
            (void)builder;
        }

        virtual bool CreatePipeline(RenderContext& context) = 0;

        virtual DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const = 0;

        virtual bool CreateDescriptors(FrameContext& context) = 0;

        virtual void Record(CommandContext& context) = 0;

        virtual void Cleanup(RRenderData& renderData) = 0;
    };
} // namespace RAnimation
