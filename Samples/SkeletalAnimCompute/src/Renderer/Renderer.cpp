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
#include <Renderer/Passes/ImguiPass.h>
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

    if (!mUserInterface.Init(mRenderData))
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: could not init user interface\n", __FUNCTION__);
        return false;
    }

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

bool Renderer::Draw(float deltaTime)
{
    mRenderData.rdFrameTime = mFrameTimer.Stop();
    mFrameTimer.Start();

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
    mSceneFrame.modelInstData = &mModelInstData;
    mSceneFrame.hasSceneGeometry = !mModelInstData.miModelInstances.empty();

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

    mUIGenerateTimer.Start();
    mUserInterface.HideMouse(false);
    mUserInterface.CreateFrame(mRenderData, mModelInstData);
    mRenderData.rdUIGenerateTime = mUIGenerateTimer.Stop();

    mUIDrawTimer.Start();
    mUserInterface.Render(mRenderData);
    mRenderData.rdUIDrawTime = mUIDrawTimer.Stop();

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
    nri::TextureBarrierDesc beginRenderingBarriers[2] = {};
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

        nri::RenderingDesc renderingDesc = {};
        renderingDesc.colors = &colorAttachment;
        renderingDesc.colorNum = 1;
        renderingDesc.depth = depthAttachment;

        mRenderData.NRI.CmdBeginRendering(*queuedFrame.commandBuffer, renderingDesc);
        mRenderData.NRI.CmdSetViewports(*queuedFrame.commandBuffer, &viewport, 1);
        mRenderData.NRI.CmdSetScissors(*queuedFrame.commandBuffer, &scissor, 1);
        mRenderData.NRI.CmdSetDescriptorPool(*queuedFrame.commandBuffer, *mRenderData.rdDescriptorPool);

        mRenderData.rdPassRegistry.RecordPhase(cmdContext, RenderPassPhase::Scene);

        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
        mDepthAttachmentInitialized = true;
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

bool Renderer::HasModel(std::string modelFileName)
{
    auto modelIter = std::find_if(mModelInstData.miModelList.begin(),
                                  mModelInstData.miModelList.end(),
                                  [modelFileName](const auto& model)
                                  {
                                      return model->GetModelFileNamePath() == modelFileName ||
                                             model->GetModelFileName() == modelFileName;
                                  });
    return modelIter != mModelInstData.miModelList.end();
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
        return true;
    }

    const bool shouldFocusImportedModel = mModelInstData.miModelList.empty();

    /* Runtime model import uploads textures and mesh buffers directly through the graphics queue.
     * Keep it serialized with the render loop so queue-owned upload work does not overlap frame submissions. */
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));

    std::shared_ptr<Model> model = std::make_shared<Model>();
    if (!model->LoadModel(mRenderData, modelFileName))
    {
        fmt::print(
                stderr, fg(fmt::color::red), "{} error: could not load model file '{}'\n", __FUNCTION__, modelFileName);
        return false;
    }

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));

    mModelInstData.miModelList.emplace_back(model);

    /* also add a new instance here to see the model */
    std::shared_ptr<ModelInstance> instance = AddInstance(model);

    if (shouldFocusImportedModel && instance != nullptr)
    {
        const glm::mat4 worldTransform = instance->GetWorldTransformMatrix();
        focusCameraOnPoint(glm::vec3(worldTransform[3]));
    }

    return true;
}

void Renderer::DeleteModel(std::string modelFileName)
{
    std::string shortModelFileName = std::filesystem::path(modelFileName).filename().generic_string();
    std::vector<std::shared_ptr<Model>> modelsToDelete;

    for (const auto& model : mModelInstData.miModelList)
    {
        if (model != nullptr &&
            (model->GetModelFileName() == shortModelFileName || model->GetModelFileNamePath() == modelFileName))
        {
            modelsToDelete.emplace_back(model);
        }
    }

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

    mModelInstData.miModelList.erase(std::remove_if(mModelInstData.miModelList.begin(),
                                                    mModelInstData.miModelList.end(),
                                                    [modelFileName, shortModelFileName](std::shared_ptr<Model> model)
                                                    {
                                                        return model->GetModelFileName() == shortModelFileName ||
                                                               model->GetModelFileNamePath() == modelFileName;
                                                    }),
                                     mModelInstData.miModelList.end());

    if (!modelsToDelete.empty())
    {
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));
        for (const auto& model : modelsToDelete)
        {
            model->Cleanup(mRenderData);
            mModelInstData.miPendingDeleteModels.erase(model);
        }
    }

    updateTriangleCount();
}

std::shared_ptr<RAnimation::ModelInstance> Renderer::AddInstance(std::shared_ptr<RAnimation::Model> model)
{
    std::shared_ptr<ModelInstance> newInst = std::make_shared<ModelInstance>(model);
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
        int clipNr = animClipNum > 0 ? std::rand() % animClipNum : 0;

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
                                                         { return inst == instance; }),
                                          mModelInstData.miModelInstances.end());


    mModelInstData.miModelInstancesPerModel[currentModelName].erase(
            std::remove_if(mModelInstData.miModelInstancesPerModel[currentModelName].begin(),
                           mModelInstData.miModelInstancesPerModel[currentModelName].end(),
                           [instance](std::shared_ptr<ModelInstance> inst) { return inst == instance; }),
            mModelInstData.miModelInstancesPerModel[currentModelName].end());

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
    if (mRenderData.rdDevice == nullptr)
    {
        return;
    }

    mRenderData.NRI.DeviceWaitIdle(mRenderData.rdDevice);

    mUserInterface.Cleanup(mRenderData);

    for (const auto& model : mModelInstData.miModelList)
    {
        if (model)
        {
            model->Cleanup(mRenderData);
        }
    }

    for (const auto& model : mModelInstData.miPendingDeleteModels)
    {
        if (model)
        {
            model->Cleanup(mRenderData);
        }
    }

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
    mRenderData.rdPassRegistry.Add<AnimationTransformComputePass>();
    mRenderData.rdPassRegistry.Add<BoneMatrixComputePass>();
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

    if (mRenderData.rdSwapChain != nullptr)
    {
        mRenderData.NRI.DestroySwapChain(mRenderData.rdSwapChain);
        mRenderData.rdSwapChain = nullptr;
    }

    mRenderData.rdDepthFormat = nri::Format::UNKNOWN;
    mDepthAttachmentInitialized = false;
}

void Renderer::updateTriangleCount()
{
    mRenderData.rdTriangleCount = 0;
    for (const auto& inst : mModelInstData.miModelInstances)
    {
        mRenderData.rdTriangleCount += inst->GetModel()->GetTriangleCount();
    }
}

void Renderer::focusCameraOnPoint(const glm::vec3& focusPoint)
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
