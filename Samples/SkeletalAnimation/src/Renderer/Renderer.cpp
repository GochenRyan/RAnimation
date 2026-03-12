#include <algorithm>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Renderer/Renderer.h>
#include <Renderer/SkinningPipeline.h>
#include <RHIWrap/Helper.h>

using namespace RAnimation;

Renderer::Renderer(NativeWindowHandle* window)
{
    if (!window)
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: window is null\n", __FUNCTION__);
        return;
    }

    switch (window->backend)
    {
        case NativeWindowBackend::Win32:
            mRenderData.rdNRIWindow.windows.hwnd = window->win32.hwnd;
            break;

        case NativeWindowBackend::Wayland:
            mRenderData.rdNRIWindow.wayland.display = window->wayland.display;
            mRenderData.rdNRIWindow.wayland.surface = window->wayland.surface;
            break;

        case NativeWindowBackend::X11:
            mRenderData.rdNRIWindow.x11.dpy = window->x11.display;
            mRenderData.rdNRIWindow.x11.window = window->x11.window;
            break;

        case NativeWindowBackend::Metal:
            mRenderData.rdNRIWindow.metal.caMetalLayer = window->metal.caMetalLayer;
            break;

        case NativeWindowBackend::Cocoa:
            // NRI rendering generally does not use NSWindow* for presentation,
            // Cocoa is merely a native window handle, not the rendering target of Metal
            fmt::print(stderr,
                       fg(fmt::color::red),
                       "{} error: Cocoa is merely a native window handle, not the rendering target of Metal\n",
                       __FUNCTION__);
            break;

        default:
            break;
    }
}

bool Renderer::Init(unsigned int width, unsigned int height)
{
    // Adapters
    nri::AdapterDesc adapterDesc[2] = {};
    uint32_t adapterDescsNum = helper::GetCountOf(adapterDesc);
    NRI_ABORT_ON_FAILURE(nri::nriEnumerateAdapters(adapterDesc, adapterDescsNum));

    nri::GraphicsAPI graphicsAPI = nri::GraphicsAPI::VK; // Default

    // Device
    nri::DeviceCreationDesc deviceCreationDesc = {};
    deviceCreationDesc.graphicsAPI = graphicsAPI;
    deviceCreationDesc.enableGraphicsAPIValidation = mRenderData.rdDebugAPI;
    deviceCreationDesc.enableNRIValidation = mRenderData.rdDebugNRI;
    deviceCreationDesc.enableD3D11CommandBufferEmulation = D3D11_ENABLE_COMMAND_BUFFER_EMULATION;
    deviceCreationDesc.disableD3D12EnhancedBarriers = D3D12_DISABLE_ENHANCED_BARRIERS;
    deviceCreationDesc.vkBindingOffsets = VK_BINDING_OFFSETS;
    deviceCreationDesc.adapterDesc = &adapterDesc[std::min(mRenderData.rdAdapterIndex, adapterDescsNum - 1)];
    deviceCreationDesc.allocationCallbacks = mRenderData.rdAllocationCallbacks;
    NRI_ABORT_ON_FAILURE(nri::nriCreateDevice(deviceCreationDesc, mRenderData.rdDevice));

    // NRI
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(
            *mRenderData.rdDevice, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*) &mRenderData.NRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(
            *mRenderData.rdDevice, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*) &mRenderData.NRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(
            *mRenderData.rdDevice, NRI_INTERFACE(nri::StreamerInterface), (nri::StreamerInterface*) &mRenderData.NRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*mRenderData.rdDevice,
                                              NRI_INTERFACE(nri::SwapChainInterface),
                                              (nri::SwapChainInterface*) &mRenderData.NRI));

    // Create streamer
    nri::StreamerDesc streamerDesc = {};
    streamerDesc.dynamicBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    streamerDesc.dynamicBufferDesc = {0, 0, nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER};
    streamerDesc.constantBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    streamerDesc.queuedFrameNum = mRenderData.GetQueuedFrameNum();
    ;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateStreamer(*mRenderData.rdDevice, streamerDesc, mRenderData.rdStreamer));

    // Command queue
    NRI_ABORT_ON_FAILURE(
            mRenderData.NRI.GetQueue(*mRenderData.rdDevice, nri::QueueType::GRAPHICS, 0, mRenderData.rdGraphicsQueue));

    // Fences
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateFence(*mRenderData.rdDevice, 0, mRenderData.rdFrameFence));

    { // Swap chain
        nri::SwapChainDesc swapChainDesc = {};
        swapChainDesc.window = mRenderData.rdNRIWindow;
        swapChainDesc.queue = mRenderData.rdGraphicsQueue;
        swapChainDesc.format = nri::SwapChainFormat::BT709_G22_10BIT;
        swapChainDesc.flags = (mRenderData.rdVsync ? nri::SwapChainBits::VSYNC : nri::SwapChainBits::NONE) |
                              nri::SwapChainBits::ALLOW_TEARING;
        swapChainDesc.width = static_cast<uint16_t>(mRenderData.rdOutputResolution.x);
        swapChainDesc.height = static_cast<uint16_t>(mRenderData.rdOutputResolution.y);
        swapChainDesc.textureNum = mRenderData.GetOptimalSwapChainTextureNum();
        swapChainDesc.queuedFrameNum = mRenderData.GetQueuedFrameNum();
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateSwapChain(*mRenderData.rdDevice, swapChainDesc, mRenderData.rdSwapChain));
    }

    // Queued frames
    mRenderData.rdQueuedFrames.resize(mRenderData.GetQueuedFrameNum());
    for (QueuedFrame& queuedFrame : mRenderData.rdQueuedFrames)
    {
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateCommandAllocator(*mRenderData.rdGraphicsQueue, queuedFrame.commandAllocator));
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateCommandBuffer(*queuedFrame.commandAllocator, queuedFrame.commandBuffer));
    }

    { // Pipeline layout
        // set 0: texture + sampler
        nri::DescriptorRangeDesc textureRanges[] = {{0, // binding 0
                                                     1,
                                                     nri::DescriptorType::TEXTURE, // image view
                                                     nri::StageBits::FRAGMENT_SHADER},
                                                    {0, // binding 0
                                                     1,
                                                     nri::DescriptorType::SAMPLER, // sampler
                                                     nri::StageBits::FRAGMENT_SHADER}};

        nri::DescriptorSetDesc textureSetDesc = {0, // set = 0
                                                 textureRanges,
                                                 std::size(textureRanges),
                                                 nri::DescriptorSetBits::NONE};

        // set 1: UBO + SSBO
        nri::DescriptorRangeDesc bufferRanges[] = {{0, // binding 0
                                                    1,
                                                    nri::DescriptorType::CONSTANT_BUFFER, // UBO
                                                    nri::StageBits::VERTEX_SHADER},
                                                   {1, // binding 1
                                                    1,
                                                    nri::DescriptorType::STRUCTURED_BUFFER, // readonly SSBO
                                                    nri::StageBits::VERTEX_SHADER}};

        nri::DescriptorSetDesc bufferSetDesc = {1, // set = 1
                                                bufferRanges,
                                                std::size(bufferRanges)};

        nri::DescriptorSetDesc descriptorSets[] = {textureSetDesc, bufferSetDesc};

        nri::PipelineLayoutDesc pipelineLayoutDesc = {2,
                                                      nullptr,
                                                      0,
                                                      nullptr,
                                                      0,
                                                      nullptr,
                                                      0,
                                                      descriptorSets,
                                                      std::size(descriptorSets),
                                                      nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER};

        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreatePipelineLayout(
                *mRenderData.rdDevice, pipelineLayoutDesc, mRenderData.rdPipelineLayout));
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreatePipelineLayout(
                *mRenderData.rdDevice, pipelineLayoutDesc, mRenderData.rdSkinningPipelineLayout));
    }

    createPipelines();

    uint32_t swapChainTextureNum;
    nri::Texture* const* swapChainTextures = mRenderData.NRI.GetSwapChainTextures(*mRenderData.rdSwapChain,
                                                                                  swapChainTextureNum);
    nri::Format swapChainFormat = mRenderData.NRI.GetTextureDesc(*swapChainTextures[0]).format;
    mRenderData.rdDepthFormat = nri::GetSupportedDepthFormat(mRenderData.NRI, *mRenderData.rdDevice, 24, true);

    // Depth attachment
    nri::Texture* depthTexture = nullptr;
    {
        nri::TextureDesc textureDesc = {};
        textureDesc.type = nri::TextureType::TEXTURE_2D;
        textureDesc.usage = nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
        textureDesc.format = mRenderData.rdDepthFormat;
        textureDesc.width = static_cast<uint16_t>(mRenderData.rdOutputResolution.x);
        textureDesc.height = static_cast<uint16_t>(mRenderData.rdOutputResolution.y);
        textureDesc.mipNum = 1;

        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateTexture(*mRenderData.rdDevice, textureDesc, depthTexture));
        mRenderData.rdTextures.push_back(depthTexture);
    }

    { // Depth buffer
        nri::Texture2DViewDesc texture2DViewDesc = {depthTexture,
                                                    nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT,
                                                    mRenderData.rdDepthFormat};

        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateTexture2DView(texture2DViewDesc, mRenderData.rdDepthAttachment));
    }

    // Swap chain
    for (uint32_t i = 0; i < swapChainTextureNum; i++)
    {
        nri::Texture2DViewDesc textureViewDesc = {swapChainTextures[i],
                                                  nri::Texture2DViewType::COLOR_ATTACHMENT,
                                                  swapChainFormat};

        nri::Descriptor* colorAttachment = nullptr;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateTexture2DView(textureViewDesc, colorAttachment));

        nri::Fence* acquireSemaphore = nullptr;
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateFence(*mRenderData.rdDevice, nri::SWAPCHAIN_SEMAPHORE, acquireSemaphore));

        nri::Fence* releaseSemaphore = nullptr;
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateFence(*mRenderData.rdDevice, nri::SWAPCHAIN_SEMAPHORE, releaseSemaphore));

        SwapChainTexture& swapChainTexture = mRenderData.rdSwapChainTextures.emplace_back();

        swapChainTexture = {};
        swapChainTexture.acquireSemaphore = acquireSemaphore;
        swapChainTexture.releaseSemaphore = releaseSemaphore;
        swapChainTexture.texture = swapChainTextures[i];
        swapChainTexture.colorAttachment = colorAttachment;
        swapChainTexture.attachmentFormat = swapChainFormat;
    }

    // todo: imgui

    /* required for perspective */
    mRenderData.rdOutputResolution = {width, height};

    /* register callbacks */
    mModelInstData.miModelCheckCallbackFunction = [this](std::string fileName) { return HasModel(fileName); };
    mModelInstData.miModelAddCallbackFunction = [this](std::string fileName) { return AddModel(fileName); };
    mModelInstData.miModelDeleteCallbackFunction = [this](std::string modelName) { DeleteModel(modelName); };

    mModelInstData.miInstanceAddCallbackFunction = [this](std::shared_ptr<RAnimation::Model> model)
    { return AddInstance(model); };
    mModelInstData.miInstanceAddManyCallbackFunction = [this](std::shared_ptr<RAnimation::Model> model,
                                                              int numInstances) { AddInstances(model, numInstances); };
    mModelInstData.miInstanceDeleteCallbackFunction = [this](std::shared_ptr<RAnimation::ModelInstance> instance)
    { DeleteInstance(instance); };
    mModelInstData.miInstanceCloneCallbackFunction = [this](std::shared_ptr<RAnimation::ModelInstance> instance)
    { CloneInstance(instance); };

    mFrameTimer.Start();

    fmt::print("{}: Renderer initialized to {}x{}\n", __FUNCTION__, width, height);

    return true;
}

void Renderer::SetSize(unsigned int width, unsigned int height)
{
}

bool Renderer::Draw(float deltaTime)
{
    return false;
}

void Renderer::HandleKeyEvents(int key, int scancode, int action, int mods)
{
}

void Renderer::HandleMouseButtonEvents(int button, int action, int mods)
{
}

void Renderer::HandleMousePositionEvents(double xPos, double yPos)
{
}

bool Renderer::HasModel(std::string modelFileName)
{
    return false;
}

std::shared_ptr<RAnimation::Model> Renderer::GetModel(std::string modelFileName)
{
    return std::shared_ptr<RAnimation::Model>();
}

bool Renderer::AddModel(std::string modelFileName)
{
    return false;
}

void Renderer::DeleteModel(std::string modelFileName)
{
}

std::shared_ptr<RAnimation::ModelInstance> Renderer::AddInstance(std::shared_ptr<RAnimation::Model> model)
{
    return std::shared_ptr<RAnimation::ModelInstance>();
}

void Renderer::AddInstances(std::shared_ptr<RAnimation::Model> model, int numInstances)
{
}

void Renderer::DeleteInstance(std::shared_ptr<RAnimation::ModelInstance> instance)
{
}

void Renderer::CloneInstance(std::shared_ptr<RAnimation::ModelInstance> instance)
{
}

void Renderer::Cleanup()
{
}

bool Renderer::createPipelines()
{
    // todo: nri shader
    std::string vertexShaderFile = ASSETS_SRC_DIR "/SkeletalAnimation/shader/assimp.vert.spv";
    std::string fragmentShaderFile = ASSETS_SRC_DIR "/SkeletalAnimation/shader/assimp.frag.spv";
    if (!SkinningPipeline::Init(mRenderData,
                                *mRenderData.rdPipelineLayout,
                                mRenderData.rdPipeline,
                                vertexShaderFile,
                                fragmentShaderFile))
    {
        fmt::print(stderr, "{} error: could not init shader pipeline\n", __FUNCTION__);
        return false;
    }

    vertexShaderFile = ASSETS_SRC_DIR "/SkeletalAnimation/shader/assimp_skinning.vert.spv";
    fragmentShaderFile = ASSETS_SRC_DIR "/SkeletalAnimation/shader/assimp_skinning.frag.spv";
    if (!SkinningPipeline::Init(mRenderData,
                                *mRenderData.rdSkinningPipelineLayout,
                                mRenderData.rdSkinningPipeline,
                                vertexShaderFile,
                                fragmentShaderFile))
    {
        fmt::print(stderr, "{} error: could not init Skinning shader pipeline\n", __FUNCTION__);
        return false;
    }
    return true;
}
