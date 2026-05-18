#pragma once

#include <cstdint>

#include <NRI.h>

struct NRIInterface;

namespace RAnimation
{
    struct RRenderData;
    class RenderResourceRegistry;
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

        // Optional: register additional registry buffers/views. Default no-op.
        virtual bool DeclareResources(RenderResourceRegistry& registry)
        {
            (void)registry;
            return true;
        }

        virtual bool CreatePipeline(RenderContext& context) = 0;

        virtual DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const = 0;

        virtual bool CreateDescriptors(FrameContext& context) = 0;

        virtual void Record(CommandContext& context) = 0;

        virtual void Cleanup(RRenderData& renderData) = 0;
    };
} // namespace RAnimation
