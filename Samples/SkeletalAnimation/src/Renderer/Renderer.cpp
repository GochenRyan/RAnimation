#include <algorithm>
#include <filesystem>

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
    initDevice();
    initNRI();
    createStreamer();
    getQueue();
    createSyncObjects();
    createSwapchain();
    createMatrixUBO();
    createSSBOs();
    createQueuedFrames();
    createDescriptorLayouts();
    createPipelineLayout();
    createPipelines();
    createDescriptorPool();
    createDescriptorSets();
    createDescriptors();
    createSwapchainTextures();

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
    /* handle minimize */
    if (width == 0 || height == 0)
    {
        return;
    }

    mRenderData.rdOutputResolution = {width, height};
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
    auto modelIter = std::find_if(mModelInstData.miModelList.begin(),
                                  mModelInstData.miModelList.end(),
                                  [modelFileName](const auto& model)
                                  {
                                      return model->GetModelFileNamePath() == modelFileName ||
                                             model->GetModelFileName() == modelFileName;
                                  });
    return false;
}

std::shared_ptr<RAnimation::Model> Renderer::GetModel(std::string modelFileName)
{
    auto modelIter = std::find_if(mModelInstData.miModelList.begin(),
                                  mModelInstData.miModelList.end(),
                                  [modelFileName](const auto& model)
                                  {
                                      return model->GetModelFileNamePath() == modelFileName ||
                                             model->GetModelFileName() == modelFileName;
                                  });
    if (modelIter != mModelInstData.miModelList.end())
    {
        return *modelIter;
    }
    return nullptr;
}

bool Renderer::AddModel(std::string modelFileName)
{
    if (HasModel(modelFileName))
    {
        fmt::print("{} warning: model '{}' already existed, skipping\n", __FUNCTION__, modelFileName);
    }

    std::shared_ptr<Model> model = std::make_shared<Model>();
    if (!model->LoadModel(mRenderData, modelFileName))
    {
        fmt::print(
                stderr, fg(fmt::color::red), "{} error: could not load model file '{}'\n", __FUNCTION__, modelFileName);
        return false;
    }

    mModelInstData.miModelList.emplace_back(model);

    /* also add a new instance here to see the model */
    AddInstance(model);

    return true;
}

void Renderer::DeleteModel(std::string modelFileName)
{
    std::string shortModelFileName = std::filesystem::path(modelFileName).filename().generic_string();

    if (!mModelInstData.miModelInstances.empty())
    {
        mModelInstData.miModelInstances.erase(
                std::remove_if(mModelInstData.miModelInstances.begin(),
                               mModelInstData.miModelInstances.end(),
                               [shortModelFileName](std::shared_ptr<ModelInstance> instance)
                               { return instance->GetModel()->GetModelFileName() == shortModelFileName; }),
                mModelInstData.miModelInstances.end());
    }

    if (mModelInstData.miModelInstancesPerModel.count(shortModelFileName) > 0)
    {
        mModelInstData.miModelInstancesPerModel[shortModelFileName].clear();
        mModelInstData.miModelInstancesPerModel.erase(shortModelFileName);
    }

    /* add models to pending delete list */
    for (const auto& model : mModelInstData.miModelList)
    {
        if (model && (model->GetTriangleCount() > 0))
        {
            mModelInstData.miPendingDeleteModels.insert(model);
        }
    }

    mModelInstData.miModelList.erase(std::remove_if(mModelInstData.miModelList.begin(),
                                                    mModelInstData.miModelList.end(),
                                                    [modelFileName](std::shared_ptr<Model> model)
                                                    { return model->GetModelFileName() == modelFileName; }));
    updateTriangleCount();
}

std::shared_ptr<RAnimation::ModelInstance> Renderer::AddInstance(std::shared_ptr<RAnimation::Model> model)
{
    std::shared_ptr<ModelInstance> newInst = std::make_shared<ModelInstance>();
    mModelInstData.miModelInstances.emplace_back(newInst);
    mModelInstData.miModelInstancesPerModel[model->GetModelFileName()].emplace_back(newInst);

    updateTriangleCount();

    return newInst;
}

void Renderer::AddInstances(std::shared_ptr<RAnimation::Model> model, int numInstances)
{
    size_t animClipNum = model->GetAnimClips().size();
    for (int i = 0; i < numInstances; ++i)
    {
        int xPos = std::rand() % 50 - 25;
        int zPos = std::rand() % 50 - 25;
        int rotation = std::rand() % 360 - 180;
        int clipNr = std::rand() % animClipNum;

        std::shared_ptr<ModelInstance> newInstance =
                std::make_shared<ModelInstance>(model, glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));
        if (animClipNum > 0)
        {
            InstanceSettings instSettings = newInstance->GetInstanceSettings();
            instSettings.mAnimClipNr = clipNr;
            newInstance->SetInstanceSettings(instSettings);
        }

        mModelInstData.miModelInstances.emplace_back(newInstance);
        mModelInstData.miModelInstancesPerModel[model->GetModelFileName()].emplace_back(newInstance);
    }
    updateTriangleCount();
}

void Renderer::DeleteInstance(std::shared_ptr<RAnimation::ModelInstance> instance)
{
    std::shared_ptr<Model> currentModel = instance->GetModel();
    std::string currentModelName = currentModel->GetModelFileName();

    mModelInstData.miModelInstances.erase(std::remove_if(mModelInstData.miModelInstances.begin(),
                                                         mModelInstData.miModelInstances.end(),
                                                         [instance](std::shared_ptr<ModelInstance> inst)
                                                         { return inst == instance; }));


    mModelInstData.miModelInstancesPerModel[currentModelName].erase(
            std::remove_if(mModelInstData.miModelInstancesPerModel[currentModelName].begin(),
                           mModelInstData.miModelInstancesPerModel[currentModelName].end(),
                           [instance](std::shared_ptr<ModelInstance> inst) { return inst == instance; }));

    updateTriangleCount();
}

void Renderer::CloneInstance(std::shared_ptr<RAnimation::ModelInstance> instance)
{
    std::shared_ptr<Model> currentModel = instance->GetModel();
    std::shared_ptr<ModelInstance> newInstance = std::make_shared<ModelInstance>(currentModel);
    InstanceSettings newInstanceSettings = instance->GetInstanceSettings();

    /* slight offset to see new instance */
    newInstanceSettings.mWorldPosition += glm::vec3(1.0f, 0.0f, -1.0f);
    newInstance->SetInstanceSettings(newInstanceSettings);

    mModelInstData.miModelInstances.emplace_back(newInstance);
    mModelInstData.miModelInstancesPerModel[currentModel->GetModelFileName()].emplace_back(newInstance);

    updateTriangleCount();
}

void Renderer::Cleanup()
{
}

bool Renderer::initDevice()
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

    return true;
}

bool Renderer::initNRI()
{
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

    return true;
}

bool Renderer::createStreamer()
{
    // Create streamer
    nri::StreamerDesc streamerDesc = {};
    streamerDesc.dynamicBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    streamerDesc.dynamicBufferDesc = {0, 0, nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER};
    streamerDesc.constantBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    streamerDesc.queuedFrameNum = mRenderData.GetQueuedFrameNum();
    ;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateStreamer(*mRenderData.rdDevice, streamerDesc, mRenderData.rdStreamer));

    return true;
}

bool Renderer::getQueue()
{
    // Command queue
    NRI_ABORT_ON_FAILURE(
            mRenderData.NRI.GetQueue(*mRenderData.rdDevice, nri::QueueType::GRAPHICS, 0, mRenderData.rdGraphicsQueue));
    return true;
}

bool Renderer::createSyncObjects()
{
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateFence(*mRenderData.rdDevice, 0, mRenderData.rdFrameFence));
    return true;
}

bool Renderer::createSwapchain()
{
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
    return true;
}

bool Renderer::createQueuedFrames()
{
    // Queued frames
    mRenderData.rdQueuedFrames.resize(mRenderData.GetQueuedFrameNum());
    for (QueuedFrame& queuedFrame : mRenderData.rdQueuedFrames)
    {
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateCommandAllocator(*mRenderData.rdGraphicsQueue, queuedFrame.commandAllocator));
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateCommandBuffer(*queuedFrame.commandAllocator, queuedFrame.commandBuffer));
    }
    return true;
}

bool Renderer::createPipelineLayout()
{
    nri::RootConstantDesc rootConstantDesc = {};
    rootConstantDesc.registerIndex = 0;
    rootConstantDesc.shaderStages = nri::StageBits::VERTEX_SHADER;
    rootConstantDesc.size = sizeof(RPushConstants);

    nri::PipelineLayoutDesc pipelineLayoutDesc = {};
    pipelineLayoutDesc.rootConstantNum = 1;
    pipelineLayoutDesc.rootConstants = &rootConstantDesc;
    pipelineLayoutDesc.rootRegisterSpace = 2;
    pipelineLayoutDesc.descriptorSetNum = std::size(mRenderData.rdDescriptorSetDescs);
    pipelineLayoutDesc.descriptorSets = mRenderData.rdDescriptorSetDescs.data();
    pipelineLayoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreatePipelineLayout(
            *mRenderData.rdDevice, pipelineLayoutDesc, mRenderData.rdPipelineLayout));
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreatePipelineLayout(
            *mRenderData.rdDevice, pipelineLayoutDesc, mRenderData.rdSkinningPipelineLayout));

    return true;
}

bool Renderer::createPipelines()
{
    // todo: nri shader
    std::string vertexShaderFile = SHADER_SRC_DIR "/SkeletalAnimation/assimp.vs";
    std::string fragmentShaderFile = SHADER_SRC_DIR "/SkeletalAnimation/assimp.fs";
    if (!SkinningPipeline::Init(mRenderData,
                                *mRenderData.rdPipelineLayout,
                                mRenderData.rdPipeline,
                                vertexShaderFile,
                                fragmentShaderFile))
    {
        fmt::print(stderr, "{} error: could not init shader pipeline\n", __FUNCTION__);
        return false;
    }

    vertexShaderFile = SHADER_SRC_DIR "/SkeletalAnimation/assimp_skinning.vs";
    fragmentShaderFile = SHADER_SRC_DIR "/SkeletalAnimation/assimp_skinning.fs";
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

bool Renderer::createMatrixUBO()
{
    const nri::DeviceDesc& deviceDesc = mRenderData.NRI.GetDeviceDesc(*mRenderData.rdDevice);
    const uint32_t constantBufferSize = helper::Align((uint32_t) sizeof(RUploadMatrices),
                                                      deviceDesc.memoryAlignment.constantBufferOffset);

    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = constantBufferSize * mRenderData.GetQueuedFrameNum();
    bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
    nri::Buffer* buffer;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, bufferDesc, buffer));
    mRenderData.rdBuffers.push_back(buffer);
    return true;
}

bool Renderer::createSSBOs()
{
    {
        uint64_t bufferSize = uint64_t(sizeof(glm::mat4)) * mWorldPosMatrices.size();

        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = bufferSize;
        bufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;

        nri::Buffer* buffer = nullptr;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, bufferDesc, buffer));
        mRenderData.rdBuffers.push_back(buffer);
    }

    {
        uint64_t bufferSize = uint64_t(sizeof(glm::mat4)) * mModelBoneMatrices.size();

        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = bufferSize;
        bufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;

        nri::Buffer* buffer = nullptr;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, bufferDesc, buffer));
        mRenderData.rdBuffers.push_back(buffer);
    }
}

bool Renderer::allocateAndBindMemory()
{
    // nri::ResourceGroupDesc resourceGroupDesc = {};
    // resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    // resourceGroupDesc.bufferNum = 1;
    // resourceGroupDesc.buffers = &mRenderData.rdBuffers[VP_MATRIX_BUFFER];

    // size_t baseAllocation = mRenderData.rdMemoryAllocations.size();
    // mRenderData.rdMemoryAllocations.resize(baseAllocation + 1, nullptr);
    // NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateAndBindMemory(
    //         *mRenderData.rdDevice, resourceGroupDesc, mRenderData.rdMemoryAllocations.data() + baseAllocation));

    // resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    // resourceGroupDesc.bufferNum = 2;
    // resourceGroupDesc.buffers = &mRenderData.rdBuffers[WORLD_POS_BUFFER];

    // baseAllocation = mRenderData.rdMemoryAllocations.size();
    // uint32_t allocationNum = mRenderData.NRI.CalculateAllocationNumber(*mRenderData.rdDevice, resourceGroupDesc);
    // mRenderData.rdMemoryAllocations.resize(baseAllocation + allocationNum, nullptr);
    // NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateAndBindMemory(
    //         *mRenderData.rdDevice, resourceGroupDesc, mRenderData.rdMemoryAllocations.data() + baseAllocation));
    return true;
}

bool Renderer::createDescriptors()
{

    return true;
}

bool Renderer::createDescriptorPool()
{
    uint32_t materialNum = 1;
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.descriptorSetMaxNum = materialNum + mRenderData.GetQueuedFrameNum();
    descriptorPoolDesc.textureMaxNum = materialNum * TEXTURES_PER_MATERIAL;
    descriptorPoolDesc.samplerMaxNum = mRenderData.GetQueuedFrameNum();
    descriptorPoolDesc.constantBufferMaxNum = mRenderData.GetQueuedFrameNum();

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateDescriptorPool(
            *mRenderData.rdDevice, descriptorPoolDesc, mRenderData.rdDescriptorPool));

    return true;
}

bool Renderer::createDescriptorLayouts()
{
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

    mRenderData.rdDescriptorSetDescs = {textureSetDesc, bufferSetDesc};

    return true;
}

bool Renderer::createDescriptorSets()
{
    uint8_t materialNum = 1;
    mRenderData.rdDescriptorSets.resize(mRenderData.GetQueuedFrameNum() + materialNum);


    return true;
}

bool Renderer::createSwapchainTextures()
{
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
    return true;
}

void Renderer::updateTriangleCount()
{
    mRenderData.rdTriangleCount = 0;
    for (const auto& inst : mModelInstData.miModelInstances)
    {
        mRenderData.rdTriangleCount += inst->GetModel()->GetTriangleCount();
    }
}