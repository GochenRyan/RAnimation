#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <ml.h>

#include <Model/MeshData.h>
#include <Tools/Camera.h>

#include <Renderer/PassRegistry.h>
#include <Renderer/RenderResourceBudget.h>
#include <Renderer/RenderResourceRegistry.h>
#include <RHIWrap/NRIInterface.h>
#include <RHIWrap/Utils.h>

struct SDL_Window;

namespace RAnimation
{
    class ModelInstance;

    // One slot of the GPU pick-ID readback ring. A 1x1 region of the R32_UINT ID target is copied
    // into readbackBuffer after the scene pass; the result is read back queuedFrameNum frames later
    // (once its command buffer fence has signalled) and resolved against the draw-order snapshot.
    struct PickReadbackSlot
    {
        nri::Buffer* readbackBuffer = nullptr;
        nri::Memory* readbackMemory = nullptr;
        bool hasResult = false;
        std::vector<std::shared_ptr<ModelInstance>> snapshot;
    };

    // Pending viewport click awaiting a GPU ID readback (set by UserInterface, consumed by Renderer).
    struct PendingPick
    {
        int x = 0;
        int y = 0;
        bool requested = false;
    };

   /* data format to be uploaded to compute shader */
    struct RNodeTransformData
    {
        glm::vec4 translation = glm::vec4(0.0f);
        glm::vec4 scale = glm::vec4(1.0f);
        glm::vec4 rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // this is a quaternion
    };

    /* tier C: one crowd member (mirrors CrowdInstance in hybrid_common.hlsli) */
    struct CrowdInstanceData
    {
        glm::mat4 world = glm::mat4(1.0f);
        uint32_t trackIndex = 0;
        uint32_t _pad[3] = {0, 0, 0};
    };
    static_assert(sizeof(CrowdInstanceData) == 80, "CrowdInstanceData must match the HLSL layout");

    /* per-frame numbers published by HybridEvalComputePass for the UI */
    struct HybridEvalStats
    {
        uint32_t dispatches = 0;
        uint32_t threadGroups = 0;
        uint32_t works = 0;
        uint32_t terms = 0;
        uint32_t slotTransforms = 0;
        uint32_t crowdInstances = 0;
        size_t slotBytes = 0;
        size_t uploadBytes = 0;
        size_t trsTextureBytes = 0;
    };

    struct RUploadMatrices
    {
        glm::mat4 viewMatrix{};
        glm::mat4 projectionMatrix{};
    };

    struct RTextureData
    {
        nri::Texture* nriTexture = nullptr;
        utils::Texture texture{};
        nri::Memory* memory = nullptr;
        nri::Descriptor* descriptor = nullptr;
        nri::DescriptorSet* descriptorSet = nullptr;

        RTextureData() = default;
        ~RTextureData() = default;

        RTextureData(const RTextureData&) = delete;
        RTextureData& operator=(const RTextureData&) = delete;

        RTextureData(RTextureData&&) noexcept = default;
        RTextureData& operator=(RTextureData&&) noexcept = default;
    };

    struct QueuedFrame
    {
        nri::CommandAllocator* commandAllocator = nullptr;
        nri::CommandBuffer* commandBuffer = nullptr;
        uint32_t swapChainTextureIndex = 0;
    };

    struct RRenderData
    {
        uint2 rdOutputResolution = {1920, 1080};

        unsigned int rdTriangleCount = 0;
        unsigned int rdMatricesSize = 0;
        RenderResourceBudget rdResourceBudget{};
        RenderResourceBudgetUsage rdResourceBudgetUsage{};

        float rdFrameTime = 0.0f;
        float rdMatrixGenerateTime = 0.0f;
        float rdUploadToVBOTime = 0.0f;
        float rdUploadToUBOTime = 0.0f;
        float rdUIGenerateTime = 0.0f;
        float rdUIDrawTime = 0.0f;

        int rdMoveForward = 0;
        int rdMoveRight = 0;
        int rdMoveUp = 0;

        // The camera rig: one camera of each fixed type + the active slot. The active camera's view/
        // projection is uploaded each frame (see StaticMeshDrawPass::Upload).
        CameraRig rdCameraRig{};

        uint32_t rdRngState = 0;
        uint32_t rdAdapterIndex = 0;
        float rdMouseSensitivity = 1.0f;
        uint8_t rdHalfTimeLimitReached = 2;
        bool rdVsync = false;
        bool rdDebugAPI = false;
        bool rdDebugNRI = false;
        bool rdAlwaysActive = false;
        bool rdResizable = false;
        bool rdIsAsyncMode = false;
        bool rdHasComputeQueue = false;

        /* Nri specific stuff */
        nri::Window rdNRIWindow = {};
        nri::AllocationCallbacks rdAllocationCallbacks = {};
        SDL_Window* rdSDLWindow = nullptr;

        NRIInterface NRI = {};
        nri::Device* rdDevice = nullptr;
        nri::SwapChain* rdSwapChain = nullptr;
        nri::Imgui* rdImgui = nullptr;

        std::vector<SwapChainTexture> rdSwapChainTextures;

        nri::Queue* rdGraphicsQueue = nullptr;
        nri::Queue* rdComputeQueue = nullptr;

        nri::Format rdDepthFormat = nri::Format::UNKNOWN;
        nri::Texture* rdDepthTexture = nullptr;
        nri::Descriptor* rdDepthAttachment = nullptr;
        nri::Memory* rdDepthMemory = nullptr;

        // Picking / selection-outline render target (R32_UINT). Swapchain-sized; recreated on resize
        // alongside the depth attachment. rdPickIdColorAttachment is the MRT slot-1 view; rdPickIdShaderResource
        // is the SRV the OutlinePass samples.
        nri::Texture* rdPickIdTexture = nullptr;
        nri::Memory* rdPickIdMemory = nullptr;
        nri::Descriptor* rdPickIdColorAttachment = nullptr;
        nri::Descriptor* rdPickIdShaderResource = nullptr;

        std::vector<PickReadbackSlot> rdPickReadbackSlots;
        PendingPick rdPendingPick{};

        nri::Descriptor* anisotropicSampler = nullptr;

        // Shared by StaticMeshDrawPass / SkinnedMeshDrawPass / NRITexture material descriptor allocation.
        // Owned by StaticMeshDrawPass (created in CreatePipeline, destroyed in Cleanup).
        nri::PipelineLayout* rdMaterialPipelineLayout = nullptr;

        // Owned by HybridEvalComputePass (created in CreatePipeline, destroyed in Cleanup). Published so
        // NRITrsTexture::Load can fill a per-model set 1 at model-load time. The free list holds the
        // pre-allocated set 1s (NRI cannot free single sets); Load pops, Model::Cleanup pushes back.
        nri::PipelineLayout* rdHybridEvalPipelineLayout = nullptr;
        std::vector<nri::DescriptorSet*> rdTrsTextureSetFreeList;
        HybridEvalStats rdHybridStats{};

        std::vector<QueuedFrame> rdQueuedFrames;
        uint32_t queuedFrameIndex = 0;

        nri::DescriptorPool* rdDescriptorPool = nullptr;

        nri::Fence* rdFrameFence = nullptr;

        std::vector<nri::Texture*> rdTextures;

        nri::Streamer* rdStreamer = nullptr;

        std::vector<nri::Memory*> rdMemoryAllocations;

        RenderResourceRegistry rdResourceRegistry{};
        PassRegistry rdPassRegistry{};

        uint64_t rdFrameIndex = 0;

        uint8_t GetQueuedFrameNum() const { return rdVsync ? 2 : 3; }

        uint8_t GetOptimalSwapChainTextureNum() const { return GetQueuedFrameNum() + 1; }

        QueuedFrame& GetCurrentQueueFrame() { return rdQueuedFrames[queuedFrameIndex]; }
        const QueuedFrame& GetCurrentQueueFrame() const { return rdQueuedFrames[queuedFrameIndex]; }
    };
} // namespace RAnimation
