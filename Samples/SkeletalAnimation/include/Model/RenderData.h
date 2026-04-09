#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <assimp/material.h>
#include <glm/glm.hpp>
#include <ml.h>

#include <RHIWrap/NRIInterface.h>
#include <RHIWrap/Utils.h>

struct SDL_Window;

namespace RAnimation
{
    struct RVertex
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        glm::vec3 normal = glm::vec3(0.0f);
        glm::vec2 uv = glm::vec2(0.0f);
        glm::uvec4 boneNumber = glm::uvec4(0);
        glm::vec4 boneWeight = glm::vec4(0.0f);
    };

    struct RMesh
    {
        std::vector<RVertex> vertices{};
        std::vector<uint32_t> indices{};
        std::unordered_map<aiTextureType, std::string> textures{};
        bool usesPBRColors = false;
    };

    struct RUploadMatrices
    {
        glm::mat4 viewMatrix{};
        glm::mat4 projectionMatrix{};
    };

    struct RPushConstants
    {
        int modelStride;
        int worldPosOffset;
    };

    struct RTextureData
    {
        nri::Texture* nriTexture = nullptr;
        utils::Texture texture{};
        nri::Memory* memory = nullptr;
        nri::Descriptor* descriptor = nullptr;
        nri::DescriptorSet* descriptorSet = nullptr;
    };

    struct QueuedFrame
    {
        nri::CommandAllocator* commandAllocator = nullptr;
        nri::CommandBuffer* commandBuffer = nullptr;
        nri::Descriptor* cameraBufferView = nullptr;
        nri::Descriptor* modelBufferView = nullptr;
        nri::Descriptor* boneBufferView = nullptr;
        nri::DescriptorSet* staticDescriptorSet = nullptr;
        nri::DescriptorSet* skinnedDescriptorSet = nullptr;
        uint64_t cameraBufferOffset = 0;
        uint64_t modelBufferOffset = 0;
        uint64_t boneBufferOffset = 0;
        uint32_t swapChainTextureIndex = 0;
    };

    struct RRenderData
    {
        uint2 rdOutputResolution = {1920, 1080};

        unsigned int rdTriangleCount = 0;
        unsigned int rdMatricesSize = 0;

        int rdFieldOfView = 60;

        float rdFrameTime = 0.0f;
        float rdMatrixGenerateTime = 0.0f;
        float rdUploadToVBOTime = 0.0f;
        float rdUploadToUBOTime = 0.0f;
        float rdUIGenerateTime = 0.0f;
        float rdUIDrawTime = 0.0f;

        int rdMoveForward = 0;
        int rdMoveRight = 0;
        int rdMoveUp = 0;

        float rdViewAzimuth = 330.0f;
        float rdViewElevation = -20.0f;
        glm::vec3 rdCameraWorldPosition = glm::vec3(2.0f, 5.0f, 7.0f);

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

        nri::Descriptor* anisotropicSampler = nullptr;

        nri::PipelineLayout* rdPipelineLayout = nullptr;
        nri::PipelineLayout* rdSkinningPipelineLayout = nullptr;

        nri::Pipeline* rdPipeline = nullptr;
        nri::Pipeline* rdSkinningPipeline = nullptr;

        std::vector<QueuedFrame> rdQueuedFrames;
        uint32_t queuedFrameIndex = 0;

        nri::DescriptorPool* rdDescriptorPool = nullptr;
        std::vector<nri::DescriptorRangeDesc> rdTextureDescriptorRanges;
        std::vector<nri::DescriptorRangeDesc> rdBufferDescriptorRanges;
        std::vector<nri::DescriptorSetDesc> rdDescriptorSetDescs;
        std::vector<nri::DescriptorSet*> rdDescriptorSets;
        std::vector<nri::Descriptor*> rdDescriptors;

        nri::Fence* rdFrameFence = nullptr;

        std::vector<nri::Texture*> rdTextures;

        nri::Streamer* rdStreamer = nullptr;

        std::vector<nri::Memory*> rdMemoryAllocations;

        std::vector<nri::Buffer*> rdBuffers;
        uint64_t rdFrameIndex = 0;

        uint8_t GetQueuedFrameNum() const { return rdVsync ? 2 : 3; }

        uint8_t GetOptimalSwapChainTextureNum() const { return GetQueuedFrameNum() + 1; }

        QueuedFrame& GetCurrentQueueFrame() { return rdQueuedFrames[queuedFrameIndex]; }
        const QueuedFrame& GetCurrentQueueFrame() const { return rdQueuedFrames[queuedFrameIndex]; }
    };
} // namespace RAnimation
