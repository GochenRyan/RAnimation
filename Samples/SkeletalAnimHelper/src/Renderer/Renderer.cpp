#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <unordered_map>

#include <fmt/base.h>
#include <fmt/color.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <Renderer/Passes/AnimationTransformComputePass.h>
#include <Renderer/Passes/BoneMatrixComputePass.h>
#include <Renderer/Passes/GizmoDrawPass.h>
#include <Renderer/Passes/ImguiPass.h>
#include <Renderer/Passes/OutlinePass.h>
#include <Renderer/Passes/SkinnedMeshDrawPass.h>
#include <Renderer/Passes/StaticMeshDrawPass.h>
#include <Renderer/Renderer.h>
#include <Renderer/SceneResourceNames.h>
#include <RHIWrap/Helper.h>

using namespace RAnimation;

#ifndef AI_LOG
#    define AI_LOG(...)
#endif

namespace
{
    const char* GetRenderResourceTierName(RenderResourceTier tier)
    {
        switch (tier)
        {
            case RenderResourceTier::Unsupported:
                return "Unsupported";
            case RenderResourceTier::Low:
                return "Low";
            case RenderResourceTier::Medium:
                return "Medium";
            case RenderResourceTier::High:
                return "High";
            default:
                return "Unknown";
        }
    }

    const char* GetRenderResourceRejectReasonName(RenderResourceRejectReason reason)
    {
        switch (reason)
        {
            case RenderResourceRejectReason::None:
                return "None";
            case RenderResourceRejectReason::BelowMinimumAnimatedInstances:
                return "BelowMinimumAnimatedInstances";
            default:
                return "Unknown";
        }
    }

    bool IsFiniteMatrix(const glm::mat4& matrix)
    {
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t col = 0; col < 4; ++col)
            {
                if (!std::isfinite(matrix[row][col]))
                {
                    return false;
                }
            }
        }

        return true;
    }

    float GetMaxAbsMatrixElement(const glm::mat4& matrix)
    {
        float maxAbsValue = 0.0f;
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t col = 0; col < 4; ++col)
            {
                maxAbsValue = std::max(maxAbsValue, std::abs(matrix[row][col]));
            }
        }

        return maxAbsValue;
    }

} // namespace

Renderer::Renderer(NativeWindowHandle* window) : Renderer(window, nullptr)
{
}

Renderer::Renderer(NativeWindowHandle* window, SDL_Window* sdlWindow)
{
    if (!window)
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: window is null\n", __FUNCTION__);
        return;
    }

    mRenderData.rdSDLWindow = sdlWindow;

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
    /* required for perspective */
    mRenderData.rdOutputResolution = {width, height};

    if (!initDevice() || !initNRI() || !createStreamer() || !getQueue() || !createSyncObjects())
    {
        return false;
    }

    if (!createSwapchain() || !createSwapchainTextures())
    {
        return false;
    }

    if (!createQueuedFrames() || !registerPasses() || !allocateAndBindMemory() || !createSampler() ||
        !createDescriptorPool() || !createPassPipelinesAndDescriptors())
    {
        return false;
    }

    // Point the outline pass at the pick-ID SRV now that both the views and the descriptor set exist.
    if (mOutlinePass != nullptr)
    {
        mOutlinePass->SetIdTextureSRV(mRenderData, mRenderData.rdPickIdShaderResource);
    }

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

    if (mRenderData.rdOutputResolution.x == width && mRenderData.rdOutputResolution.y == height)
    {
        return;
    }

    mRenderData.rdOutputResolution = {width, height};
    if (mRenderData.rdSwapChain != nullptr)
    {
        mSwapchainRecreateRequested = true;
    }
}

bool Renderer::Draw(float deltaTime, ModelAndInstanceData& modInstData)
{
    mRenderData.rdFrameTime = mFrameTimer.Stop();
    mFrameTimer.Start();

    updateTriangleCount(modInstData);

    if (mSwapchainRecreateRequested && !recreateSwapchain())
    {
        return false;
    }

    latencySleep(mFrameIndex);

    mRenderData.rdMatrixGenerateTime = 0.0f;
    mRenderData.rdUploadToUBOTime = 0.0f;
    mRenderData.rdMatricesSize = 0;
    mRenderData.rdResourceBudgetUsage = RenderResourceBudgetUsage{};

    mRenderData.queuedFrameIndex = mFrameIndex % mRenderData.GetQueuedFrameNum();
    QueuedFrame& queuedFrame = mRenderData.GetCurrentQueueFrame();

    // Resolve a pick readback issued queuedFrameNum frames ago (its fence has signalled via latencySleep).
    resolvePendingReadback(modInstData);

    const uint32_t recycledSemaphoreIndex = mFrameIndex % mRenderData.rdSwapChainTextures.size();
    nri::Fence* acquiredSemaphore = mRenderData.rdSwapChainTextures[recycledSemaphoreIndex].acquireSemaphore;
    const nri::Result acquireResult = mRenderData.NRI.AcquireNextTexture(
            *mRenderData.rdSwapChain, *acquiredSemaphore, queuedFrame.swapChainTextureIndex);
    if (acquireResult == nri::Result::OUT_OF_DATE)
    {
        mSwapchainRecreateRequested = true;
        return true;
    }
    NRI_ABORT_ON_FAILURE(acquireResult);

    const float safeDeltaTime = std::max(deltaTime, 0.000001f);

    // Per-frame upload: each pass marshals its CPU-side data into its owned GPU buffers.
    // AnimationTransformComputePass also publishes animatedDispatches + uploadedBoneOffsetMatrixCount
    // into mSceneFrame, which is later passed via CommandContext to RecordPhase.
    mSceneFrame = SceneFrameData{};
    mSceneFrame.modelInstData = &modInstData;
    mSceneFrame.hasSceneGeometry = !modInstData.miModelInstances.empty();

    // Assign global pick IDs in draw order. The draw passes iterate miModelInstancesPerModel over
    // disjoint subsets (static vs. skinned); both look up their group's base here so each emitted
    // pick ID (base + instanceID + 1) is globally unique. drawOrderInstances snapshots the order so
    // a returned ID can be resolved back to an instance. Pick ID 0 means "nothing".
    {
        uint32_t runningPickBase = 0;
        for (const auto& modelType : modInstData.miModelInstancesPerModel)
        {
            if (modelType.second.empty())
            {
                continue;
            }
            mSceneFrame.pickBaseByModel[modelType.first] = runningPickBase;
            for (const auto& instance : modelType.second)
            {
                mSceneFrame.drawOrderInstances.emplace_back(instance);
            }
            runningPickBase += static_cast<uint32_t>(modelType.second.size());
        }

        if (!modInstData.miModelInstances.empty())
        {
            const int selectedIndex = modInstData.miSelectedInstance;
            if (selectedIndex >= 0 && selectedIndex < static_cast<int>(modInstData.miModelInstances.size()))
            {
                const std::shared_ptr<ModelInstance>& selected =
                        modInstData.miModelInstances[selectedIndex];
                for (size_t i = 0; i < mSceneFrame.drawOrderInstances.size(); ++i)
                {
                    if (mSceneFrame.drawOrderInstances[i] == selected)
                    {
                        mSceneFrame.selectedPickID = static_cast<uint32_t>(i) + 1;
                        break;
                    }
                }
            }
        }
    }

    FrameContext frameContext = {mRenderData,
                                 mRenderData.NRI,
                                 mRenderData.rdResourceRegistry,
                                 *mRenderData.rdDescriptorPool,
                                 mRenderData.GetQueuedFrameNum(),
                                 &mSceneFrame,
                                 safeDeltaTime};
    mRenderData.rdPassRegistry.UploadFrame(frameContext);

    if (!recordCommandBuffer())
    {
        return false;
    }

    nri::Fence* releaseSemaphore = mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].releaseSemaphore;

    nri::FenceSubmitDesc waitFence = {};
    waitFence.fence = acquiredSemaphore;
    waitFence.stages = nri::StageBits::ALL;

    nri::FenceSubmitDesc signalFences[] = {
            {releaseSemaphore, 0, nri::StageBits::ALL},
            {mRenderData.rdFrameFence, 1 + mFrameIndex, nri::StageBits::ALL},
    };

    nri::CommandBuffer* commandBuffer = queuedFrame.commandBuffer;
    nri::QueueSubmitDesc queueSubmitDesc = {};
    queueSubmitDesc.waitFences = &waitFence;
    queueSubmitDesc.waitFenceNum = 1;
    queueSubmitDesc.commandBuffers = &commandBuffer;
    queueSubmitDesc.commandBufferNum = 1;
    queueSubmitDesc.signalFences = signalFences;
    queueSubmitDesc.signalFenceNum = helper::GetCountOf(signalFences);
    AI_LOG("[AI] frame={} submit\n", mFrameIndex);
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueSubmit(*mRenderData.rdGraphicsQueue, queueSubmitDesc));
    AI_LOG("[AI] frame={} present\n", mFrameIndex);
    const nri::Result presentResult = mRenderData.NRI.QueuePresent(*mRenderData.rdSwapChain, *releaseSemaphore);
    if (presentResult == nri::Result::OUT_OF_DATE)
    {
        mSwapchainRecreateRequested = true;
    }
    else
    {
        NRI_ABORT_ON_FAILURE(presentResult);
        mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].hasBeenPresented = true;
    }
    mRenderData.NRI.EndStreamerFrame(*mRenderData.rdStreamer);

    ++mFrameIndex;
    mRenderData.rdFrameIndex = mFrameIndex;

    return true;
}


bool Renderer::recordCommandBuffer()
{
    QueuedFrame& queuedFrame = mRenderData.GetCurrentQueueFrame();
    const bool hasSceneGeometry = mSceneFrame.hasSceneGeometry;
    const bool hasImgui = ImguiPass::HasDrawData();

    CommandContext cmdContext = {mRenderData,
                                 mRenderData.NRI,
                                 mRenderData.rdResourceRegistry,
                                 *queuedFrame.commandBuffer,
                                 *mRenderData.rdDescriptorPool,
                                 mRenderData.queuedFrameIndex,
                                 &mSceneFrame};

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.BeginCommandBuffer(*queuedFrame.commandBuffer, nullptr));

    // ImGui draw-list upload must run before any render-target transitions.
    ImguiPass::RecordCopyImguiData(cmdContext);

    // Compute phase: dispatches + buffer barriers (no BeginRendering wrap).
    mRenderData.NRI.CmdSetDescriptorPool(*queuedFrame.commandBuffer, *mRenderData.rdDescriptorPool);
    mRenderData.rdPassRegistry.RecordPhase(cmdContext, RenderPassPhase::Compute);

    // Render-target transitions before scene/UI rendering.
    nri::TextureBarrierDesc beginRenderingBarriers[3] = {};
    uint32_t beginRenderingBarrierCount = 0;
    beginRenderingBarriers[beginRenderingBarrierCount++] = {
            mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].texture,
            {nri::AccessBits::NONE,
             mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].hasBeenPresented
                     ? nri::Layout::PRESENT
                     : nri::Layout::UNDEFINED,
             nri::StageBits::NONE},
            {nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT, nri::StageBits::COLOR_ATTACHMENT}};

    if (hasSceneGeometry)
    {
        const bool isFirstScenePass = !mDepthAttachmentInitialized;
        beginRenderingBarriers[beginRenderingBarrierCount++] = {
                mRenderData.rdDepthTexture,
                {isFirstScenePass ? nri::AccessBits::NONE : nri::AccessBits::DEPTH_STENCIL_ATTACHMENT,
                 isFirstScenePass ? nri::Layout::UNDEFINED : nri::Layout::DEPTH_STENCIL_ATTACHMENT,
                 isFirstScenePass ? nri::StageBits::NONE : nri::StageBits::DEPTH_STENCIL_ATTACHMENT},
                {nri::AccessBits::DEPTH_STENCIL_ATTACHMENT,
                 nri::Layout::DEPTH_STENCIL_ATTACHMENT,
                 nri::StageBits::DEPTH_STENCIL_ATTACHMENT}};

        // Pick-ID target -> COLOR_ATTACHMENT (before-state tracked across its per-frame cycle).
        beginRenderingBarriers[beginRenderingBarrierCount++] = {
                mRenderData.rdPickIdTexture,
                {mPickIdAccess, mPickIdLayout, mPickIdStage},
                {nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT, nri::StageBits::COLOR_ATTACHMENT}};
        mPickIdAccess = nri::AccessBits::COLOR_ATTACHMENT;
        mPickIdLayout = nri::Layout::COLOR_ATTACHMENT;
        mPickIdStage = nri::StageBits::COLOR_ATTACHMENT;
    }

    nri::BarrierDesc beginRenderingBarrierDesc = {};
    beginRenderingBarrierDesc.textures = beginRenderingBarriers;
    beginRenderingBarrierDesc.textureNum = beginRenderingBarrierCount;
    mRenderData.NRI.CmdBarrier(*queuedFrame.commandBuffer, beginRenderingBarrierDesc);

    nri::Viewport viewport = {};
    viewport.width = static_cast<float>(mRenderData.rdOutputResolution.x);
    viewport.height = static_cast<float>(mRenderData.rdOutputResolution.y);
    viewport.depthMin = 0.0f;
    viewport.depthMax = 1.0f;

    nri::Rect scissor = {};
    scissor.width = static_cast<uint16_t>(mRenderData.rdOutputResolution.x);
    scissor.height = static_cast<uint16_t>(mRenderData.rdOutputResolution.y);

    nri::AttachmentDesc colorAttachment = {};
    colorAttachment.descriptor = mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].colorAttachment;
    colorAttachment.loadOp = nri::LoadOp::CLEAR;
    colorAttachment.storeOp = nri::StoreOp::STORE;
    colorAttachment.clearValue.color.f = {0.25f, 0.25f, 0.25f, 1.0f};

    if (hasSceneGeometry)
    {
        nri::AttachmentDesc depthAttachment = {};
        depthAttachment.descriptor = mRenderData.rdDepthAttachment;
        depthAttachment.loadOp = nri::LoadOp::CLEAR;
        depthAttachment.storeOp = nri::StoreOp::STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        // MRT: slot 0 = swapchain color, slot 1 = R32_UINT pick ID (cleared to 0 = "nothing").
        nri::AttachmentDesc sceneColorAttachments[2] = {};
        sceneColorAttachments[0] = colorAttachment;
        sceneColorAttachments[1].descriptor = mRenderData.rdPickIdColorAttachment;
        sceneColorAttachments[1].loadOp = nri::LoadOp::CLEAR;
        sceneColorAttachments[1].storeOp = nri::StoreOp::STORE;
        sceneColorAttachments[1].clearValue.color.ui = {0, 0, 0, 0};

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = sceneColorAttachments;
        renderingDesc.colorNum = helper::GetCountOf(sceneColorAttachments);
        renderingDesc.depth = depthAttachment;

        mRenderData.NRI.CmdBeginRendering(*queuedFrame.commandBuffer, renderingDesc);
        mRenderData.NRI.CmdSetViewports(*queuedFrame.commandBuffer, &viewport, 1);
        mRenderData.NRI.CmdSetScissors(*queuedFrame.commandBuffer, &scissor, 1);
        mRenderData.NRI.CmdSetDescriptorPool(*queuedFrame.commandBuffer, *mRenderData.rdDescriptorPool);

        mRenderData.rdPassRegistry.RecordPhase(cmdContext, RenderPassPhase::Scene);

        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
        mDepthAttachmentInitialized = true;

        // Copy the clicked pixel of the pick-ID target into the readback ring (if a pick is pending).
        recordPickReadback(*queuedFrame.commandBuffer);

        // Transition pick-ID target -> SHADER_RESOURCE so the outline post-process can sample it.
        nri::TextureBarrierDesc toShaderResource = {
                mRenderData.rdPickIdTexture,
                {mPickIdAccess, mPickIdLayout, mPickIdStage},
                {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE, nri::StageBits::FRAGMENT_SHADER}};
        nri::BarrierDesc toShaderResourceDesc = {};
        toShaderResourceDesc.textures = &toShaderResource;
        toShaderResourceDesc.textureNum = 1;
        mRenderData.NRI.CmdBarrier(*queuedFrame.commandBuffer, toShaderResourceDesc);
        mPickIdAccess = nri::AccessBits::SHADER_RESOURCE;
        mPickIdLayout = nri::Layout::SHADER_RESOURCE;
        mPickIdStage = nri::StageBits::FRAGMENT_SHADER;

        // Post-process (outline) renders over the scene color, sampling the pick-ID target.
        nri::AttachmentDesc postColorAttachment = colorAttachment;
        postColorAttachment.loadOp = nri::LoadOp::LOAD;

        nri::RenderingDesc postRenderingDesc = {};
        postRenderingDesc.colors = &postColorAttachment;
        postRenderingDesc.colorNum = 1;

        mRenderData.NRI.CmdBeginRendering(*queuedFrame.commandBuffer, postRenderingDesc);
        mRenderData.NRI.CmdSetViewports(*queuedFrame.commandBuffer, &viewport, 1);
        mRenderData.NRI.CmdSetScissors(*queuedFrame.commandBuffer, &scissor, 1);
        mRenderData.NRI.CmdSetDescriptorPool(*queuedFrame.commandBuffer, *mRenderData.rdDescriptorPool);

        mRenderData.rdPassRegistry.RecordPhase(cmdContext, RenderPassPhase::PostProcess);

        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
    }

    if (hasImgui)
    {
        colorAttachment.loadOp = hasSceneGeometry ? nri::LoadOp::LOAD : nri::LoadOp::CLEAR;

        nri::RenderingDesc uiRenderingDesc = {};
        uiRenderingDesc.colors = &colorAttachment;
        uiRenderingDesc.colorNum = 1;

        mRenderData.NRI.CmdBeginRendering(*queuedFrame.commandBuffer, uiRenderingDesc);
        mRenderData.NRI.CmdSetViewports(*queuedFrame.commandBuffer, &viewport, 1);
        mRenderData.NRI.CmdSetScissors(*queuedFrame.commandBuffer, &scissor, 1);

        mRenderData.rdPassRegistry.RecordPhase(cmdContext, RenderPassPhase::UI);

        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
    }
    else if (!hasSceneGeometry)
    {
        nri::RenderingDesc clearOnlyRenderingDesc = {};
        clearOnlyRenderingDesc.colors = &colorAttachment;
        clearOnlyRenderingDesc.colorNum = 1;

        mRenderData.NRI.CmdBeginRendering(*queuedFrame.commandBuffer, clearOnlyRenderingDesc);
        mRenderData.NRI.CmdSetViewports(*queuedFrame.commandBuffer, &viewport, 1);
        mRenderData.NRI.CmdSetScissors(*queuedFrame.commandBuffer, &scissor, 1);
        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
    }

    nri::TextureBarrierDesc endRenderingBarrier = {
            mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].texture,
            {nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT, nri::StageBits::COLOR_ATTACHMENT},
            {nri::AccessBits::NONE, nri::Layout::PRESENT, nri::StageBits::NONE}};

    nri::BarrierDesc endRenderingBarrierDesc = {};
    endRenderingBarrierDesc.textures = &endRenderingBarrier;
    endRenderingBarrierDesc.textureNum = 1;
    mRenderData.NRI.CmdBarrier(*queuedFrame.commandBuffer, endRenderingBarrierDesc);

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.EndCommandBuffer(*queuedFrame.commandBuffer));

    return true;
}

void Renderer::latencySleep(uint32_t frameIndex)
{
    uint32_t queuedFrameIndex = frameIndex % mRenderData.GetQueuedFrameNum();
    const QueuedFrame& queuedFrame = mRenderData.rdQueuedFrames[queuedFrameIndex];
    mRenderData.NRI.Wait(*mRenderData.rdFrameFence,
                         frameIndex >= mRenderData.GetQueuedFrameNum()
                                 ? 1 + frameIndex - mRenderData.GetQueuedFrameNum()
                                 : 0);
    mRenderData.NRI.ResetCommandAllocator(*queuedFrame.commandAllocator);
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

std::shared_ptr<RAnimation::Model> Renderer::LoadModel(const std::string& modelFileName)
{
    /* Runtime model import uploads textures and mesh buffers directly through the graphics queue.
     * Keep it serialized with the render loop so queue-owned upload work does not overlap frame submissions. */
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));

    std::shared_ptr<Model> model = std::make_shared<Model>();
    if (!model->LoadModel(mRenderData, modelFileName))
    {
        fmt::print(
                stderr, fg(fmt::color::red), "{} error: could not load model file '{}'\n", __FUNCTION__, modelFileName);
        return nullptr;
    }

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));

    return model;
}

void Renderer::ReleaseModel(const std::shared_ptr<RAnimation::Model>& model)
{
    if (model == nullptr)
    {
        return;
    }

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));
    model->Cleanup(mRenderData);
}

void Renderer::WaitIdle()
{
    if (mRenderData.rdDevice != nullptr)
    {
        mRenderData.NRI.DeviceWaitIdle(mRenderData.rdDevice);
    }
}

void Renderer::Cleanup()
{
    if (mRenderData.rdDevice == nullptr)
    {
        return;
    }

    mRenderData.NRI.DeviceWaitIdle(mRenderData.rdDevice);

    // Scene models are owned by the SceneEditor; the Application releases them (and the UI) before
    // calling Cleanup(), while the device is still alive. Here the Renderer tears down only its own
    // resources.

    for (QueuedFrame& queuedFrame : mRenderData.rdQueuedFrames)
    {
        mRenderData.NRI.DestroyCommandBuffer(queuedFrame.commandBuffer);
        mRenderData.NRI.DestroyCommandAllocator(queuedFrame.commandAllocator);
    }

    mRenderData.rdPassRegistry.Cleanup(mRenderData);

    destroySwapchainResources();

    mRenderData.NRI.DestroyDescriptor(mRenderData.anisotropicSampler);
    mRenderData.NRI.DestroyDescriptorPool(mRenderData.rdDescriptorPool);

    mRenderData.rdResourceRegistry.Cleanup(mRenderData);

    for (nri::Memory* memory : mRenderData.rdMemoryAllocations)
    {
        mRenderData.NRI.FreeMemory(memory);
    }

    mRenderData.NRI.DestroyStreamer(mRenderData.rdStreamer);
    mRenderData.NRI.DestroyFence(mRenderData.rdFrameFence);
    nri::nriDestroyDevice(mRenderData.rdDevice);

    mRenderData = {};
}

bool Renderer::initDevice()
{
#if defined(_DEBUG)
    mRenderData.rdDebugAPI = true;
    mRenderData.rdDebugNRI = true;
#endif

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
            *mRenderData.rdDevice, NRI_INTERFACE(nri::ImguiInterface), (nri::ImguiInterface*) &mRenderData.NRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(
            *mRenderData.rdDevice, NRI_INTERFACE(nri::StreamerInterface), (nri::StreamerInterface*) &mRenderData.NRI));
    NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*mRenderData.rdDevice,
                                               NRI_INTERFACE(nri::SwapChainInterface),
                                               (nri::SwapChainInterface*) &mRenderData.NRI));

    const nri::DeviceDesc& deviceDesc = mRenderData.NRI.GetDeviceDesc(*mRenderData.rdDevice);
    mResourceBudget = RenderResourceBudget::CreateForDevice(deviceDesc, mRenderData.GetQueuedFrameNum());
    mRenderData.rdResourceBudget = mResourceBudget;
    if (!mResourceBudget.IsSupported())
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: resource budget unsupported reason={} computedAnimatedInstances={} "
                   "memoryLimitedAnimatedInstances={} deviceLimitedAnimatedInstances={} "
                   "animationMemoryBudget={} adapter='{}' architecture={} videoMemory={} sharedMemory={}\n",
                   __FUNCTION__,
                   GetRenderResourceRejectReasonName(mResourceBudget.rejectReason),
                   mResourceBudget.maxWorldMatrices,
                   mResourceBudget.memoryLimitedWorldMatrices,
                   mResourceBudget.deviceLimitedWorldMatrices,
                   mResourceBudget.animationMemoryBudget,
                   deviceDesc.adapterDesc.name,
                   static_cast<uint32_t>(deviceDesc.adapterDesc.architecture),
                   deviceDesc.adapterDesc.videoMemorySize,
                   deviceDesc.adapterDesc.sharedSystemMemorySize);
        return false;
    }

    fmt::print("{}: resource budget tier={} maxAnimatedInstances={} maxBoneMatrices={} maxNodeTransforms={} "
               "animationMemoryBudget={} estimatedAnimationBytesPerFrame={} estimatedAnimationBytesTotal={} "
               "memoryLimitedAnimatedInstances={} deviceLimitedAnimatedInstances={} "
               "adapter='{}' architecture={} videoMemory={} sharedMemory={}\n",
               __FUNCTION__,
               GetRenderResourceTierName(mResourceBudget.tier),
               mResourceBudget.maxWorldMatrices,
               mResourceBudget.GetMaxBoneMatrices(),
               mResourceBudget.GetMaxNodeTransforms(),
               mResourceBudget.animationMemoryBudget,
               mResourceBudget.estimatedAnimationBufferBytesPerFrame,
               mResourceBudget.estimatedAnimationBufferBytesTotal,
               mResourceBudget.memoryLimitedWorldMatrices,
               mResourceBudget.deviceLimitedWorldMatrices,
               deviceDesc.adapterDesc.name,
               static_cast<uint32_t>(deviceDesc.adapterDesc.architecture),
               deviceDesc.adapterDesc.videoMemorySize,
               deviceDesc.adapterDesc.sharedSystemMemorySize);

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

bool Renderer::registerPasses()
{
    // StaticMeshDrawPass must come first since it owns the shared material pipeline layout
    // that SkinnedMeshDrawPass and material descriptor allocation depend on.
    mRenderData.rdPassRegistry.Add<StaticMeshDrawPass>();
    mRenderData.rdPassRegistry.Add<SkinnedMeshDrawPass>();
    mRenderData.rdPassRegistry.Add<GizmoDrawPass>();
    mRenderData.rdPassRegistry.Add<AnimationTransformComputePass>();
    mRenderData.rdPassRegistry.Add<BoneMatrixComputePass>();
    mOutlinePass = mRenderData.rdPassRegistry.Add<OutlinePass>();
    mRenderData.rdPassRegistry.Add<ImguiPass>();

    ResourceContext resourceContext = {mRenderData.rdResourceRegistry, mResourceBudget};
    if (!mRenderData.rdPassRegistry.DeclareResources(resourceContext))
    {
        return false;
    }

    return mRenderData.rdResourceRegistry.CreateBuffers(mRenderData);
}

bool Renderer::createPassPipelinesAndDescriptors()
{
    if (!mRenderData.rdResourceRegistry.CreateViews(mRenderData))
    {
        return false;
    }

    const nri::DeviceDesc& deviceDesc = mRenderData.NRI.GetDeviceDesc(*mRenderData.rdDevice);
    (void)deviceDesc;

    uint32_t swapChainTextureNum = 0;
    nri::Texture* const* swapChainTextures = mRenderData.NRI.GetSwapChainTextures(*mRenderData.rdSwapChain,
                                                                                  swapChainTextureNum);
    nri::Format swapChainFormat = mRenderData.NRI.GetTextureDesc(*swapChainTextures[0]).format;

    RenderContext renderContext = {mRenderData,
                                   mRenderData.NRI,
                                   *mRenderData.rdDevice,
                                   mRenderData.rdResourceRegistry,
                                   mResourceBudget,
                                   swapChainFormat,
                                   mRenderData.rdDepthFormat};

    if (!mRenderData.rdPassRegistry.CreatePipelines(renderContext))
    {
        return false;
    }

    FrameContext frameContext = {mRenderData,
                                 mRenderData.NRI,
                                 mRenderData.rdResourceRegistry,
                                 *mRenderData.rdDescriptorPool,
                                 mRenderData.GetQueuedFrameNum()};
    return mRenderData.rdPassRegistry.CreateDescriptors(frameContext);
}

bool Renderer::allocateAndBindMemory()
{
    std::vector<nri::Memory*> allocations;

    auto allocGroup = [&](nri::MemoryLocation memoryLocation, std::vector<nri::Buffer*>& buffers) -> bool
    {
        if (buffers.empty())
        {
            return true;
        }

        nri::ResourceGroupDesc groupDesc = {};
        groupDesc.memoryLocation = memoryLocation;
        groupDesc.bufferNum = static_cast<uint32_t>(buffers.size());
        groupDesc.buffers = buffers.data();

        const uint32_t allocNum = mRenderData.NRI.CalculateAllocationNumber(*mRenderData.rdDevice, groupDesc);
        if (allocNum == 0)
        {
            return true;
        }

        const size_t base = allocations.size();
        allocations.resize(base + allocNum, nullptr);

        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.AllocateAndBindMemory(*mRenderData.rdDevice, groupDesc, allocations.data() + base));

        return true;
    };

    std::vector<nri::Buffer*> uploadBuffers =
            mRenderData.rdResourceRegistry.CollectBuffersByMemoryLocation(nri::MemoryLocation::HOST_UPLOAD);
    if (!allocGroup(nri::MemoryLocation::HOST_UPLOAD, uploadBuffers))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: failed to allocate and bind memory for upload buffers\n",
                   __FUNCTION__);
        return false;
    }

    std::vector<nri::Buffer*> deviceBuffers =
            mRenderData.rdResourceRegistry.CollectBuffersByMemoryLocation(nri::MemoryLocation::DEVICE);
    if (!allocGroup(nri::MemoryLocation::DEVICE, deviceBuffers))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: failed to allocate and bind memory for device buffers\n",
                   __FUNCTION__);
        return false;
    }

    mRenderData.rdMemoryAllocations = std::move(allocations);

    return true;
}

bool Renderer::createSampler()
{
    nri::SamplerDesc samplerDesc = {};
    samplerDesc.filters = {nri::Filter::LINEAR, nri::Filter::LINEAR, nri::Filter::LINEAR, nri::FilterOp::AVERAGE};
    samplerDesc.anisotropy = 8;
    samplerDesc.addressModes = {nri::AddressMode::REPEAT, nri::AddressMode::REPEAT, nri::AddressMode::REPEAT};
    samplerDesc.mipMin = 0.0f;
    samplerDesc.mipMax = 16.0f;
    samplerDesc.compareOp = nri::CompareOp::NONE;
    NRI_ABORT_ON_FAILURE(
            mRenderData.NRI.CreateSampler(*mRenderData.rdDevice, samplerDesc, mRenderData.anisotropicSampler));
    return true;
}

bool Renderer::createDescriptorPool()
{
    constexpr uint32_t materialNum = 1024;
    const uint32_t queuedFrameNum = mRenderData.GetQueuedFrameNum();

    const DescriptorPoolRequirements passReq =
            mRenderData.rdPassRegistry.GetDescriptorPoolRequirements(queuedFrameNum);

    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.descriptorSetMaxNum = materialNum + passReq.descriptorSetMaxNum;
    descriptorPoolDesc.textureMaxNum = materialNum * TEXTURES_PER_MATERIAL + passReq.textureMaxNum;
    descriptorPoolDesc.samplerMaxNum = materialNum + passReq.samplerMaxNum;
    descriptorPoolDesc.constantBufferMaxNum = passReq.constantBufferMaxNum;
    descriptorPoolDesc.structuredBufferMaxNum = passReq.structuredBufferMaxNum;
    descriptorPoolDesc.storageStructuredBufferMaxNum = passReq.storageStructuredBufferMaxNum;

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateDescriptorPool(
            *mRenderData.rdDevice, descriptorPoolDesc, mRenderData.rdDescriptorPool));

    return true;
}

bool Renderer::createSwapchainTextures()
{
    uint32_t swapChainTextureNum;
    nri::Texture* const* swapChainTextures = mRenderData.NRI.GetSwapChainTextures(*mRenderData.rdSwapChain,
                                                                                  swapChainTextureNum);
    nri::Format swapChainFormat = mRenderData.NRI.GetTextureDesc(*swapChainTextures[0]).format;

    if (!createDepthAttachmentResources())
    {
        return false;
    }

    if (!createPickingResources())
    {
        return false;
    }

    // Swap chain attachments
    mRenderData.rdSwapChainTextures.clear();
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

bool Renderer::createDepthAttachmentResources()
{
    mRenderData.rdDepthFormat = nri::GetSupportedDepthFormat(mRenderData.NRI, *mRenderData.rdDevice, 24, true);

    nri::TextureDesc textureDesc = {};
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    textureDesc.usage = nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
    textureDesc.format = mRenderData.rdDepthFormat;
    textureDesc.width = static_cast<uint16_t>(mRenderData.rdOutputResolution.x);
    textureDesc.height = static_cast<uint16_t>(mRenderData.rdOutputResolution.y);
    textureDesc.mipNum = 1;

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateTexture(*mRenderData.rdDevice, textureDesc, mRenderData.rdDepthTexture));

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &mRenderData.rdDepthTexture;

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateAndBindMemory(*mRenderData.rdDevice,
                                                               resourceGroupDesc,
                                                               &mRenderData.rdDepthMemory));

    nri::Texture2DViewDesc texture2DViewDesc = {mRenderData.rdDepthTexture,
                                                nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT,
                                                mRenderData.rdDepthFormat};

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateTexture2DView(texture2DViewDesc, mRenderData.rdDepthAttachment));

    return true;
}

void Renderer::recordPickReadback(nri::CommandBuffer& commandBuffer)
{
    if (!mRenderData.rdPendingPick.requested || mRenderData.rdPickReadbackSlots.empty())
    {
        return;
    }

    const uint32_t x = static_cast<uint32_t>(
            std::clamp(mRenderData.rdPendingPick.x, 0, static_cast<int>(mRenderData.rdOutputResolution.x) - 1));
    const uint32_t y = static_cast<uint32_t>(
            std::clamp(mRenderData.rdPendingPick.y, 0, static_cast<int>(mRenderData.rdOutputResolution.y) - 1));

    // Pick-ID target -> COPY_SOURCE for the 1px readback.
    nri::TextureBarrierDesc toCopySource = {
            mRenderData.rdPickIdTexture,
            {mPickIdAccess, mPickIdLayout, mPickIdStage},
            {nri::AccessBits::COPY_SOURCE, nri::Layout::COPY_SOURCE, nri::StageBits::COPY}};
    nri::BarrierDesc toCopySourceDesc = {};
    toCopySourceDesc.textures = &toCopySource;
    toCopySourceDesc.textureNum = 1;
    mRenderData.NRI.CmdBarrier(commandBuffer, toCopySourceDesc);
    mPickIdAccess = nri::AccessBits::COPY_SOURCE;
    mPickIdLayout = nri::Layout::COPY_SOURCE;
    mPickIdStage = nri::StageBits::COPY;

    PickReadbackSlot& slot = mRenderData.rdPickReadbackSlots[mRenderData.queuedFrameIndex];

    const nri::DeviceDesc& deviceDesc = mRenderData.NRI.GetDeviceDesc(*mRenderData.rdDevice);
    const uint32_t rowAlignment = std::max(1u, deviceDesc.memoryAlignment.uploadBufferTextureRow);
    const uint32_t rowPitch = ((sizeof(uint32_t) + rowAlignment - 1) / rowAlignment) * rowAlignment;

    nri::TextureRegionDesc srcRegion = {};
    srcRegion.x = static_cast<nri::Dim_t>(x);
    srcRegion.y = static_cast<nri::Dim_t>(y);
    srcRegion.z = 0;
    srcRegion.width = 1;
    srcRegion.height = 1;
    srcRegion.depth = 1;

    nri::TextureDataLayoutDesc dstLayout = {};
    dstLayout.offset = 0;
    dstLayout.rowPitch = rowPitch;
    dstLayout.slicePitch = rowPitch;

    mRenderData.NRI.CmdReadbackTextureToBuffer(
            commandBuffer, *slot.readbackBuffer, dstLayout, *mRenderData.rdPickIdTexture, srcRegion);

    slot.hasResult = true;
    slot.snapshot = mSceneFrame.drawOrderInstances;
    mRenderData.rdPendingPick.requested = false;
}

void Renderer::resolvePendingReadback(ModelAndInstanceData& modInstData)
{
    if (mRenderData.rdPickReadbackSlots.empty())
    {
        return;
    }

    // latencySleep() has already waited on this queued frame's fence, so its previous readback
    // (issued queuedFrameNum frames ago) is complete and safe to map.
    PickReadbackSlot& slot = mRenderData.rdPickReadbackSlots[mRenderData.queuedFrameIndex];
    if (!slot.hasResult)
    {
        return;
    }

    uint32_t pickID = 0;
    void* mapped = mRenderData.NRI.MapBuffer(*slot.readbackBuffer, 0, sizeof(uint32_t));
    if (mapped != nullptr)
    {
        std::memcpy(&pickID, mapped, sizeof(uint32_t));
        mRenderData.NRI.UnmapBuffer(*slot.readbackBuffer);
    }

    if (pickID != 0 && pickID - 1 < slot.snapshot.size())
    {
        const std::shared_ptr<ModelInstance>& picked = slot.snapshot[pickID - 1];
        for (size_t i = 0; i < modInstData.miModelInstances.size(); ++i)
        {
            if (modInstData.miModelInstances[i] == picked)
            {
                modInstData.miSelectedInstance = static_cast<int>(i);
                break;
            }
        }
    }

    slot.hasResult = false;
    slot.snapshot.clear();
}

bool Renderer::createPickingResources()
{
    // R32_UINT pick-ID target: MRT slot 1 of the scene pass, sampled by the outline post-process,
    // and the source of the 1px GPU readback used for mouse picking.
    nri::TextureDesc textureDesc = {};
    textureDesc.type = nri::TextureType::TEXTURE_2D;
    textureDesc.usage = nri::TextureUsageBits::COLOR_ATTACHMENT | nri::TextureUsageBits::SHADER_RESOURCE;
    textureDesc.format = nri::Format::R32_UINT;
    textureDesc.width = static_cast<uint16_t>(mRenderData.rdOutputResolution.x);
    textureDesc.height = static_cast<uint16_t>(mRenderData.rdOutputResolution.y);
    textureDesc.mipNum = 1;

    NRI_ABORT_ON_FAILURE(
            mRenderData.NRI.CreateTexture(*mRenderData.rdDevice, textureDesc, mRenderData.rdPickIdTexture));

    nri::ResourceGroupDesc resourceGroupDesc = {};
    resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
    resourceGroupDesc.textureNum = 1;
    resourceGroupDesc.textures = &mRenderData.rdPickIdTexture;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateAndBindMemory(*mRenderData.rdDevice,
                                                               resourceGroupDesc,
                                                               &mRenderData.rdPickIdMemory));

    nri::Texture2DViewDesc colorViewDesc = {mRenderData.rdPickIdTexture,
                                            nri::Texture2DViewType::COLOR_ATTACHMENT,
                                            nri::Format::R32_UINT};
    NRI_ABORT_ON_FAILURE(
            mRenderData.NRI.CreateTexture2DView(colorViewDesc, mRenderData.rdPickIdColorAttachment));

    nri::Texture2DViewDesc srvViewDesc = {mRenderData.rdPickIdTexture,
                                          nri::Texture2DViewType::SHADER_RESOURCE,
                                          nri::Format::R32_UINT};
    NRI_ABORT_ON_FAILURE(
            mRenderData.NRI.CreateTexture2DView(srvViewDesc, mRenderData.rdPickIdShaderResource));

    // Readback ring: one HOST_READBACK buffer per queued frame, each holding a single aligned pixel row.
    const nri::DeviceDesc& deviceDesc = mRenderData.NRI.GetDeviceDesc(*mRenderData.rdDevice);
    const uint32_t rowAlignment = std::max(1u, deviceDesc.memoryAlignment.uploadBufferTextureRow);
    const uint64_t readbackSize = ((sizeof(uint32_t) + rowAlignment - 1) / rowAlignment) * rowAlignment;

    const uint32_t queuedFrameNum = mRenderData.GetQueuedFrameNum();
    mRenderData.rdPickReadbackSlots.clear();
    mRenderData.rdPickReadbackSlots.resize(queuedFrameNum);
    for (PickReadbackSlot& slot : mRenderData.rdPickReadbackSlots)
    {
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = readbackSize;
        bufferDesc.structureStride = 0;
        bufferDesc.usage = nri::BufferUsageBits::NONE;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, bufferDesc, slot.readbackBuffer));

        nri::ResourceGroupDesc bufferGroupDesc = {};
        bufferGroupDesc.memoryLocation = nri::MemoryLocation::HOST_READBACK;
        bufferGroupDesc.bufferNum = 1;
        bufferGroupDesc.buffers = &slot.readbackBuffer;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateAndBindMemory(*mRenderData.rdDevice,
                                                                   bufferGroupDesc,
                                                                   &slot.readbackMemory));
    }

    return true;
}

bool Renderer::recreateSwapchain()
{
    if (mRenderData.rdOutputResolution.x == 0 || mRenderData.rdOutputResolution.y == 0)
    {
        return true;
    }

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));

    destroySwapchainResources();

    if (!createSwapchain() || !createSwapchainTextures())
    {
        return false;
    }

    for (QueuedFrame& queuedFrame : mRenderData.rdQueuedFrames)
    {
        queuedFrame.swapChainTextureIndex = 0;
    }

    // The pick-ID texture/views were recreated; re-point the outline pass at the new SRV.
    if (mOutlinePass != nullptr)
    {
        mOutlinePass->SetIdTextureSRV(mRenderData, mRenderData.rdPickIdShaderResource);
    }

    mDepthAttachmentInitialized = false;
    mSwapchainRecreateRequested = false;

    return true;
}

void Renderer::destroySwapchainResources()
{
    for (SwapChainTexture& swapChainTexture : mRenderData.rdSwapChainTextures)
    {
        if (swapChainTexture.colorAttachment != nullptr)
        {
            mRenderData.NRI.DestroyDescriptor(swapChainTexture.colorAttachment);
        }
        if (swapChainTexture.acquireSemaphore != nullptr)
        {
            mRenderData.NRI.DestroyFence(swapChainTexture.acquireSemaphore);
        }
        if (swapChainTexture.releaseSemaphore != nullptr)
        {
            mRenderData.NRI.DestroyFence(swapChainTexture.releaseSemaphore);
        }
    }
    mRenderData.rdSwapChainTextures.clear();

    if (mRenderData.rdDepthAttachment != nullptr)
    {
        mRenderData.NRI.DestroyDescriptor(mRenderData.rdDepthAttachment);
        mRenderData.rdDepthAttachment = nullptr;
    }

    if (mRenderData.rdDepthTexture != nullptr)
    {
        mRenderData.NRI.DestroyTexture(mRenderData.rdDepthTexture);
        mRenderData.rdDepthTexture = nullptr;
    }

    if (mRenderData.rdDepthMemory != nullptr)
    {
        mRenderData.NRI.FreeMemory(mRenderData.rdDepthMemory);
        mRenderData.rdDepthMemory = nullptr;
    }

    if (mRenderData.rdPickIdColorAttachment != nullptr)
    {
        mRenderData.NRI.DestroyDescriptor(mRenderData.rdPickIdColorAttachment);
        mRenderData.rdPickIdColorAttachment = nullptr;
    }

    if (mRenderData.rdPickIdShaderResource != nullptr)
    {
        mRenderData.NRI.DestroyDescriptor(mRenderData.rdPickIdShaderResource);
        mRenderData.rdPickIdShaderResource = nullptr;
    }

    if (mRenderData.rdPickIdTexture != nullptr)
    {
        mRenderData.NRI.DestroyTexture(mRenderData.rdPickIdTexture);
        mRenderData.rdPickIdTexture = nullptr;
    }

    if (mRenderData.rdPickIdMemory != nullptr)
    {
        mRenderData.NRI.FreeMemory(mRenderData.rdPickIdMemory);
        mRenderData.rdPickIdMemory = nullptr;
    }

    for (PickReadbackSlot& slot : mRenderData.rdPickReadbackSlots)
    {
        if (slot.readbackBuffer != nullptr)
        {
            mRenderData.NRI.DestroyBuffer(slot.readbackBuffer);
        }
        if (slot.readbackMemory != nullptr)
        {
            mRenderData.NRI.FreeMemory(slot.readbackMemory);
        }
    }
    mRenderData.rdPickReadbackSlots.clear();

    if (mRenderData.rdSwapChain != nullptr)
    {
        mRenderData.NRI.DestroySwapChain(mRenderData.rdSwapChain);
        mRenderData.rdSwapChain = nullptr;
    }

    mRenderData.rdDepthFormat = nri::Format::UNKNOWN;
    mDepthAttachmentInitialized = false;

    // Pick-ID texture is recreated by createPickingResources(); reset its tracked barrier state.
    mPickIdAccess = nri::AccessBits::NONE;
    mPickIdLayout = nri::Layout::UNDEFINED;
    mPickIdStage = nri::StageBits::NONE;
}

void Renderer::updateTriangleCount(const ModelAndInstanceData& modInstData)
{
    mRenderData.rdTriangleCount = 0;
    for (const auto& inst : modInstData.miModelInstances)
    {
        mRenderData.rdTriangleCount += inst->GetModel()->GetTriangleCount();
    }
}

void Renderer::FocusCameraOnPoint(const glm::vec3& focusPoint)
{
    const glm::vec3 defaultViewOffset(2.0f, 5.0f, 7.0f);
    mRenderData.rdCameraWorldPosition = focusPoint + defaultViewOffset;

    const glm::vec3 viewDirection = glm::normalize(focusPoint - mRenderData.rdCameraWorldPosition);
    mRenderData.rdViewElevation = glm::degrees(std::asin(glm::clamp(viewDirection.y, -1.0f, 1.0f)));
    mRenderData.rdViewAzimuth = glm::degrees(std::atan2(viewDirection.z, viewDirection.x));
    if (mRenderData.rdViewAzimuth < 0.0f)
    {
        mRenderData.rdViewAzimuth += 360.0f;
    }
}
