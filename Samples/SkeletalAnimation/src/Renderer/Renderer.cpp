#include <algorithm>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Renderer/Renderer.h>
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

    mRenderData.rdDepthFormat = nri::GetSupportedDepthFormat(mRenderData.NRI, *mRenderData.rdDevice, 24, true);

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

    // Pipeline
    const nri::DeviceDesc& deviceDesc = mRenderData.NRI.GetDeviceDesc(*mRenderData.rdDevice);
    utils::ShaderCodeStorage shaderCodeStorage;
    {
        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;

        nri::VertexAttributeDesc vertexAttributeDesc[4] = {};
        {
            vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
            vertexAttributeDesc[0].offset = offsetof(RVertex, position);
            vertexAttributeDesc[0].d3d = {"POSITION", 0};
            vertexAttributeDesc[0].vk = {0};

            vertexAttributeDesc[1].format = nri::Format::RGBA32_SFLOAT;
            vertexAttributeDesc[1].offset = offsetof(RVertex, color);
            vertexAttributeDesc[1].d3d = {"COLOR", 0};
            vertexAttributeDesc[1].vk = {1};

            vertexAttributeDesc[2].format = nri::Format::RGB32_SFLOAT;
            vertexAttributeDesc[2].offset = offsetof(RVertex, normal);
            vertexAttributeDesc[2].d3d = {"NORMAL", 0};
            vertexAttributeDesc[2].vk = {2};

            vertexAttributeDesc[3].format = nri::Format::RG32_SFLOAT;
            vertexAttributeDesc[3].offset = offsetof(RVertex, uv);
            vertexAttributeDesc[3].d3d = {"TEXCOORD", 0};
            vertexAttributeDesc[3].vk = {3};

            vertexAttributeDesc[4].format = nri::Format::RGBA32_UINT;
            vertexAttributeDesc[4].offset = offsetof(RVertex, boneNumber);
            vertexAttributeDesc[4].d3d = {"BLENDINDICES", 0};
            vertexAttributeDesc[4].vk = {4};

            vertexAttributeDesc[5].format = nri::Format::RGBA32_SFLOAT;
            vertexAttributeDesc[5].offset = offsetof(RVertex, boneWeight);
            vertexAttributeDesc[5].d3d = {"BLENDWEIGHT", 0};
            vertexAttributeDesc[5].vk = {5};
        }

        nri::VertexInputDesc vertexInputDesc = {};
        vertexInputDesc.attributes = vertexAttributeDesc;
        vertexInputDesc.attributeNum = (uint8_t) helper::GetCountOf(vertexAttributeDesc);
        vertexInputDesc.streams = &vertexStreamDesc;
        vertexInputDesc.streamNum = 1;

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::BACK;
        rasterizationDesc.frontCounterClockwise = true;
        rasterizationDesc.shadingRate = deviceDesc.tiers.shadingRate != 0;

        nri::MultisampleDesc multisampleDesc = {};
        multisampleDesc.sampleNum = 1;
        multisampleDesc.sampleLocations = deviceDesc.tiers.sampleLocations >= 2;

        uint32_t swapChainTextureNum;
        nri::Texture* const* swapChainTextures = mRenderData.NRI.GetSwapChainTextures(*mRenderData.rdSwapChain,
                                                                                      swapChainTextureNum);
        nri::Format swapChainFormat = mRenderData.NRI.GetTextureDesc(*swapChainTextures[0]).format;

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = swapChainFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDesc.blendEnabled = false;

        mRenderData.rdDepthFormat = nri::GetSupportedDepthFormat(mRenderData.NRI, *mRenderData.rdDevice, 24, true);


        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colors = &colorAttachmentDesc;
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.depthStencilFormat = mRenderData.rdDepthFormat;
        outputMergerDesc.depth.write = true;
        outputMergerDesc.depth.compareOp = nri::CompareOp::LESS_EQUAL;

        nri::ShaderDesc shaderStages[] = {
                utils::LoadShader(deviceDesc.graphicsAPI, "Forward.vs", shaderCodeStorage),
                utils::LoadShader(deviceDesc.graphicsAPI, "Forward.fs", shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
        graphicsPipelineDesc.vertexInput = &vertexInputDesc;
        graphicsPipelineDesc.inputAssembly = inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = rasterizationDesc;
        graphicsPipelineDesc.multisample = &multisampleDesc;
        graphicsPipelineDesc.outputMerger = outputMergerDesc;
        graphicsPipelineDesc.shaders = shaderStages;
        graphicsPipelineDesc.shaderNum = helper::GetCountOf(shaderStages);
    }

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
