#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include <fmt/base.h>
#include <fmt/color.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <Renderer/Renderer.h>
#include <Renderer/SkinningPipeline.h>
#include <RHIWrap/Helper.h>

using namespace RAnimation;

#ifndef AI_LOG
#define AI_LOG(...) fmt::print(__VA_ARGS__)
#endif

namespace
{
    constexpr uint32_t MAX_WORLD_MATRICES = 4096;
    constexpr uint32_t MAX_BONE_MATRICES = MAX_WORLD_MATRICES * MAX_BONES;

    uint64_t GetCameraBufferSize(const RRenderData& renderData)
    {
        const nri::DeviceDesc& deviceDesc = renderData.NRI.GetDeviceDesc(*renderData.rdDevice);
        return helper::Align<uint64_t>(sizeof(RUploadMatrices), deviceDesc.memoryAlignment.constantBufferOffset);
    }

    uint64_t GetStructuredBufferStride(const RRenderData& renderData)
    {
        const nri::DeviceDesc& deviceDesc = renderData.NRI.GetDeviceDesc(*renderData.rdDevice);
        return helper::Align<uint64_t>(sizeof(glm::mat4), deviceDesc.memoryAlignment.bufferShaderResourceOffset);
    }

    uint64_t GetWorldBufferSizePerFrame(const RRenderData& renderData)
    {
        return GetStructuredBufferStride(renderData) * MAX_WORLD_MATRICES;
    }

    uint64_t GetBoneBufferSizePerFrame(const RRenderData& renderData)
    {
        return GetStructuredBufferStride(renderData) * MAX_BONE_MATRICES;
    }
}

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

    initDevice();
    initNRI();
    createStreamer();
    getQueue();
    createSyncObjects();

    createMatrixUBO();
    createSSBOs();
    createSwapchain();
    createSwapchainTextures();

    allocateAndBindMemory();

    createQueuedFrames();
    createDescriptorLayouts();
    createPipelineLayout();
    createPipelines();
    createDescriptorPool();
    createDescriptorSets();
    createDescriptors();

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

    mRenderData.rdOutputResolution = {width, height};
}

bool Renderer::Draw(float deltaTime)
{
    mRenderData.rdFrameTime = mFrameTimer.Stop();
    mFrameTimer.Start();

    /* reset timers and other values */
    mRenderData.rdMatricesSize = 0;
    mRenderData.rdUploadToUBOTime = 0.0f;
    mRenderData.rdUploadToVBOTime = 0.0f;
    mRenderData.rdMatrixGenerateTime = 0.0f;
    mRenderData.rdUIGenerateTime = 0.0f;

    latencySleep(mFrameIndex);

    mRenderData.queuedFrameIndex = mFrameIndex % mRenderData.GetQueuedFrameNum();
    QueuedFrame& queuedFrame = mRenderData.GetCurrentQueueFrame();

    const uint32_t recycledSemaphoreIndex = mFrameIndex % mRenderData.rdSwapChainTextures.size();
    nri::Fence* acquiredSemaphore = mRenderData.rdSwapChainTextures[recycledSemaphoreIndex].acquireSemaphore;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.AcquireNextTexture(*mRenderData.rdSwapChain, *acquiredSemaphore, queuedFrame.swapChainTextureIndex));

    const float safeDeltaTime = std::max(deltaTime, 0.000001f);

    glm::vec3 forward = glm::normalize(glm::vec3(std::cos(glm::radians(mRenderData.rdViewElevation)) *
                                                         std::cos(glm::radians(mRenderData.rdViewAzimuth)),
                                                 std::sin(glm::radians(mRenderData.rdViewElevation)),
                                                 std::cos(glm::radians(mRenderData.rdViewElevation)) *
                                                         std::sin(glm::radians(mRenderData.rdViewAzimuth))));
    glm::vec3 target = mRenderData.rdCameraWorldPosition + forward;

    RUploadMatrices matrices = {};
    matrices.viewMatrix = glm::lookAtRH(mRenderData.rdCameraWorldPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
    matrices.projectionMatrix =
            glm::perspectiveRH_ZO(glm::radians(static_cast<float>(mRenderData.rdFieldOfView)),
                                  static_cast<float>(mRenderData.rdOutputResolution.x) /
                                          static_cast<float>(mRenderData.rdOutputResolution.y),
                                  0.1f,
                                  500.0f);
    matrices.projectionMatrix[1][1] *= -1.0f;

    mMatrixGenerateTimer.Start();
    mWorldPosMatrices.clear();
    mModelBoneMatrices.clear();

    for (const auto& modelType : mModelInstData.miModelInstancesPerModel)
    {
        if (modelType.second.empty())
        {
            continue;
        }

        std::shared_ptr<Model> model = modelType.second.front()->GetModel();
        if (model->HasAnimations() && !model->GetBoneList().empty())
        {
            for (const auto& instance : modelType.second)
            {
                instance->UpdateAnimation(safeDeltaTime);
                const std::vector<glm::mat4> boneMatrices = instance->GetBoneMatrices();
                mModelBoneMatrices.insert(mModelBoneMatrices.end(), boneMatrices.begin(), boneMatrices.end());
            }
        }
        else
        {
            for (const auto& instance : modelType.second)
            {
                mWorldPosMatrices.emplace_back(instance->GetWorldTransformMatrix());
            }
        }
    }
    mRenderData.rdMatrixGenerateTime = mMatrixGenerateTimer.Stop();
    mRenderData.rdMatricesSize =
            static_cast<unsigned int>((mWorldPosMatrices.size() + mModelBoneMatrices.size()) * sizeof(glm::mat4));

    if (mWorldPosMatrices.size() > MAX_WORLD_MATRICES || mModelBoneMatrices.size() > MAX_BONE_MATRICES)
    {
        fmt::print(stderr, fg(fmt::color::red), "{} error: matrix upload capacity exceeded\n", __FUNCTION__);
        return false;
    }

    mUploadToUBOTimer.Start();
    void* cameraDst = mRenderData.NRI.MapBuffer(*mRenderData.rdBuffers[VP_MATRIX_BUFFER],
                                                queuedFrame.cameraBufferOffset,
                                                sizeof(RUploadMatrices));
    std::memcpy(cameraDst, &matrices, sizeof(RUploadMatrices));
    mRenderData.NRI.UnmapBuffer(*mRenderData.rdBuffers[VP_MATRIX_BUFFER]);

    if (!mWorldPosMatrices.empty())
    {
        void* worldDst = mRenderData.NRI.MapBuffer(*mRenderData.rdBuffers[WORLD_POS_BUFFER],
                                                   queuedFrame.modelBufferOffset,
                                                   mWorldPosMatrices.size() * sizeof(glm::mat4));
        std::memcpy(worldDst, mWorldPosMatrices.data(), mWorldPosMatrices.size() * sizeof(glm::mat4));
        mRenderData.NRI.UnmapBuffer(*mRenderData.rdBuffers[WORLD_POS_BUFFER]);
    }

    if (!mModelBoneMatrices.empty())
    {
        void* boneDst = mRenderData.NRI.MapBuffer(*mRenderData.rdBuffers[MODEL_BONE_BUFFER],
                                                  queuedFrame.boneBufferOffset,
                                                  mModelBoneMatrices.size() * sizeof(glm::mat4));
        std::memcpy(boneDst, mModelBoneMatrices.data(), mModelBoneMatrices.size() * sizeof(glm::mat4));
        mRenderData.NRI.UnmapBuffer(*mRenderData.rdBuffers[MODEL_BONE_BUFFER]);
    }
    mRenderData.rdUploadToUBOTime = mUploadToUBOTimer.Stop();

    mUIGenerateTimer.Start();
    mUserInterface.HideMouse(false);
    mUserInterface.CreateFrame(mRenderData, mModelInstData);
    mRenderData.rdUIGenerateTime = mUIGenerateTimer.Stop();

    mUIDrawTimer.Start();
    mUserInterface.Render(mRenderData);
    mRenderData.rdUIDrawTime = mUIDrawTimer.Stop();

    ImDrawData* drawData = ImGui::GetDrawData();
    const uint32_t imguiDrawListNum = drawData != nullptr ? static_cast<uint32_t>(drawData->CmdListsCount) : 0;
    const uint32_t imguiTextureNum =
            (drawData != nullptr && drawData->Textures != nullptr) ? static_cast<uint32_t>(drawData->Textures->Size) : 0;
    const bool hasSceneGeometry = !mModelInstData.miModelInstances.empty();

    AI_LOG("[AI] frame={} acquireIndex={} sceneGeometry={} imguiDrawLists={} imguiTextures={}\n",
           mFrameIndex,
           queuedFrame.swapChainTextureIndex,
           hasSceneGeometry ? 1u : 0u,
           imguiDrawListNum,
           imguiTextureNum);

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.BeginCommandBuffer(*queuedFrame.commandBuffer, nullptr));

    if (drawData != nullptr && drawData->CmdListsCount > 0)
    {
        nri::CopyImguiDataDesc copyImguiDataDesc = {};
        copyImguiDataDesc.drawLists = drawData->CmdLists.Data;
        copyImguiDataDesc.drawListNum = static_cast<uint32_t>(drawData->CmdLists.Size);
        copyImguiDataDesc.textures = drawData->Textures != nullptr ? drawData->Textures->Data : nullptr;
        copyImguiDataDesc.textureNum = drawData->Textures != nullptr ? static_cast<uint32_t>(drawData->Textures->Size) : 0;
        mRenderData.NRI.CmdCopyImguiData(*queuedFrame.commandBuffer,
                                         *mRenderData.rdStreamer,
                                         *mRenderData.rdImgui,
                                         copyImguiDataDesc);
    }

    nri::TextureBarrierDesc beginRenderingBarriers[2] = {};
    uint32_t beginRenderingBarrierCount = 0;

    beginRenderingBarriers[beginRenderingBarrierCount++] = {
            mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].texture,
            {nri::AccessBits::NONE, nri::Layout::PRESENT, nri::StageBits::NONE},
            {nri::AccessBits::COLOR_ATTACHMENT, nri::Layout::COLOR_ATTACHMENT, nri::StageBits::COLOR_ATTACHMENT}};

    if (hasSceneGeometry)
    {
        const bool isFirstRenderedFrame = (mFrameIndex == 0);
        beginRenderingBarriers[beginRenderingBarrierCount++] = {
                mRenderData.rdDepthTexture,
                {isFirstRenderedFrame ? nri::AccessBits::NONE : nri::AccessBits::DEPTH_STENCIL_ATTACHMENT,
                 isFirstRenderedFrame ? nri::Layout::UNDEFINED : nri::Layout::DEPTH_STENCIL_ATTACHMENT,
                 isFirstRenderedFrame ? nri::StageBits::NONE : nri::StageBits::DEPTH_STENCIL_ATTACHMENT},
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

        AI_LOG("[AI] frame={} begin scene pass\n", mFrameIndex);
        mRenderData.NRI.CmdBeginRendering(*queuedFrame.commandBuffer, renderingDesc);
        mRenderData.NRI.CmdSetViewports(*queuedFrame.commandBuffer, &viewport, 1);
        mRenderData.NRI.CmdSetScissors(*queuedFrame.commandBuffer, &scissor, 1);
        mRenderData.NRI.CmdSetDescriptorPool(*queuedFrame.commandBuffer, *mRenderData.rdDescriptorPool);

        uint32_t worldPosOffset = 0;
        uint32_t boneMatrixOffset = 0;
        for (const auto& modelType : mModelInstData.miModelInstancesPerModel)
        {
            if (modelType.second.empty())
            {
                continue;
            }

            const uint32_t instanceCount = static_cast<uint32_t>(modelType.second.size());
            std::shared_ptr<Model> model = modelType.second.front()->GetModel();

            if (model->HasAnimations() && !model->GetBoneList().empty())
            {
                RPushConstants pushConstants = {};
                pushConstants.modelStride = static_cast<int>(model->GetBoneList().size());
                pushConstants.worldPosOffset = static_cast<int>(boneMatrixOffset);

                mRenderData.NRI.CmdSetPipelineLayout(
                        *queuedFrame.commandBuffer, nri::BindPoint::GRAPHICS, *mRenderData.rdSkinningPipelineLayout);
                mRenderData.NRI.CmdSetPipeline(*queuedFrame.commandBuffer, *mRenderData.rdSkinningPipeline);
                mRenderData.NRI.CmdSetRootConstants(*queuedFrame.commandBuffer,
                                                    {0,
                                                     &pushConstants,
                                                     sizeof(pushConstants),
                                                     0,
                                                     nri::BindPoint::GRAPHICS});
                mRenderData.NRI.CmdSetDescriptorSet(
                        *queuedFrame.commandBuffer, {1, queuedFrame.skinnedDescriptorSet, nri::BindPoint::GRAPHICS});
                model->DrawInstanced(mRenderData, instanceCount);

                boneMatrixOffset += instanceCount * static_cast<uint32_t>(model->GetBoneList().size());
            }
            else
            {
                RPushConstants pushConstants = {};
                pushConstants.modelStride = 0;
                pushConstants.worldPosOffset = static_cast<int>(worldPosOffset);

                mRenderData.NRI.CmdSetPipelineLayout(
                        *queuedFrame.commandBuffer, nri::BindPoint::GRAPHICS, *mRenderData.rdPipelineLayout);
                mRenderData.NRI.CmdSetPipeline(*queuedFrame.commandBuffer, *mRenderData.rdPipeline);
                mRenderData.NRI.CmdSetRootConstants(*queuedFrame.commandBuffer,
                                                    {0,
                                                     &pushConstants,
                                                     sizeof(pushConstants),
                                                     0,
                                                     nri::BindPoint::GRAPHICS});
                mRenderData.NRI.CmdSetDescriptorSet(
                        *queuedFrame.commandBuffer, {1, queuedFrame.staticDescriptorSet, nri::BindPoint::GRAPHICS});
                model->DrawInstanced(mRenderData, instanceCount);

                worldPosOffset += instanceCount;
            }
        }

        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
    }

    if (drawData != nullptr && drawData->CmdListsCount > 0)
    {
        colorAttachment.loadOp = hasSceneGeometry ? nri::LoadOp::LOAD : nri::LoadOp::CLEAR;

        nri::RenderingDesc uiRenderingDesc = {};
        uiRenderingDesc.colors = &colorAttachment;
        uiRenderingDesc.colorNum = 1;

        AI_LOG("[AI] frame={} begin imgui pass drawLists={} textures={}\n",
               mFrameIndex,
               imguiDrawListNum,
               imguiTextureNum);
        mRenderData.NRI.CmdBeginRendering(*queuedFrame.commandBuffer, uiRenderingDesc);
        mRenderData.NRI.CmdSetViewports(*queuedFrame.commandBuffer, &viewport, 1);
        mRenderData.NRI.CmdSetScissors(*queuedFrame.commandBuffer, &scissor, 1);

        nri::DrawImguiDesc drawImguiDesc = {};
        drawImguiDesc.drawLists = drawData->CmdLists.Data;
        drawImguiDesc.drawListNum = static_cast<uint32_t>(drawData->CmdLists.Size);
        drawImguiDesc.displaySize = {static_cast<uint16_t>(drawData->DisplaySize.x),
                                     static_cast<uint16_t>(drawData->DisplaySize.y)};
        drawImguiDesc.attachmentFormat =
                mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].attachmentFormat;
        drawImguiDesc.linearColor = true;
        drawImguiDesc.hdrScale = 1.0f;
        mRenderData.NRI.CmdDrawImgui(*queuedFrame.commandBuffer, *mRenderData.rdImgui, drawImguiDesc);
        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
    }
    else if (!hasSceneGeometry)
    {
        nri::RenderingDesc clearOnlyRenderingDesc = {};
        clearOnlyRenderingDesc.colors = &colorAttachment;
        clearOnlyRenderingDesc.colorNum = 1;

        AI_LOG("[AI] frame={} begin clear-only pass\n", mFrameIndex);
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

    nri::Fence* releaseSemaphore =
            mRenderData.rdSwapChainTextures[queuedFrame.swapChainTextureIndex].releaseSemaphore;

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
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueuePresent(*mRenderData.rdSwapChain, *releaseSemaphore));
    mRenderData.NRI.EndStreamerFrame(*mRenderData.rdStreamer);

    ++mFrameIndex;
    mRenderData.rdFrameIndex = mFrameIndex;

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
        if (model && (model->GetTriangleCount() > 0) &&
            (model->GetModelFileName() == shortModelFileName || model->GetModelFileNamePath() == modelFileName))
        {
            mModelInstData.miPendingDeleteModels.insert(model);
        }
    }

    mModelInstData.miModelList.erase(std::remove_if(mModelInstData.miModelList.begin(),
                                                    mModelInstData.miModelList.end(),
                                                    [modelFileName, shortModelFileName](std::shared_ptr<Model> model)
                                                    {
                                                        return model->GetModelFileName() == shortModelFileName ||
                                                               model->GetModelFileNamePath() == modelFileName;
                                                    }),
                                     mModelInstData.miModelList.end());
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
        mRenderData.NRI.DestroyDescriptor(queuedFrame.cameraBufferView);
        mRenderData.NRI.DestroyDescriptor(queuedFrame.modelBufferView);
        mRenderData.NRI.DestroyDescriptor(queuedFrame.boneBufferView);
        mRenderData.NRI.DestroyCommandBuffer(queuedFrame.commandBuffer);
        mRenderData.NRI.DestroyCommandAllocator(queuedFrame.commandAllocator);
    }

    SkinningPipeline::Cleanup(mRenderData, mRenderData.rdPipeline);
    SkinningPipeline::Cleanup(mRenderData, mRenderData.rdSkinningPipeline);

    mRenderData.NRI.DestroyPipelineLayout(mRenderData.rdPipelineLayout);
    mRenderData.NRI.DestroyPipelineLayout(mRenderData.rdSkinningPipelineLayout);

    for (SwapChainTexture& swapChainTexture : mRenderData.rdSwapChainTextures)
    {
        mRenderData.NRI.DestroyDescriptor(swapChainTexture.colorAttachment);
        mRenderData.NRI.DestroyFence(swapChainTexture.acquireSemaphore);
        mRenderData.NRI.DestroyFence(swapChainTexture.releaseSemaphore);
    }

    mRenderData.NRI.DestroyDescriptor(mRenderData.rdDepthAttachment);
    mRenderData.NRI.DestroyDescriptor(mRenderData.anisotropicSampler);
    mRenderData.NRI.DestroyDescriptorPool(mRenderData.rdDescriptorPool);

    for (nri::Buffer* buffer : mRenderData.rdBuffers)
    {
        mRenderData.NRI.DestroyBuffer(buffer);
    }

    mRenderData.NRI.DestroyTexture(mRenderData.rdDepthTexture);

    for (nri::Memory* memory : mRenderData.rdMemoryAllocations)
    {
        mRenderData.NRI.FreeMemory(memory);
    }

    mRenderData.NRI.DestroyStreamer(mRenderData.rdStreamer);
    mRenderData.NRI.DestroyFence(mRenderData.rdFrameFence);
    mRenderData.NRI.DestroySwapChain(mRenderData.rdSwapChain);
    nri::nriDestroyDevice(mRenderData.rdDevice);

    mRenderData = {};
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
            *mRenderData.rdDevice, NRI_INTERFACE(nri::ImguiInterface), (nri::ImguiInterface*) &mRenderData.NRI));
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
    const uint64_t cameraBufferSize = GetCameraBufferSize(mRenderData);
    const uint64_t worldBufferSize = GetWorldBufferSizePerFrame(mRenderData);
    const uint64_t boneBufferSize = GetBoneBufferSizePerFrame(mRenderData);

    for (uint32_t i = 0; i < mRenderData.GetQueuedFrameNum(); ++i)
    {
        QueuedFrame& queuedFrame = mRenderData.rdQueuedFrames[i];
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateCommandAllocator(*mRenderData.rdGraphicsQueue, queuedFrame.commandAllocator));
        NRI_ABORT_ON_FAILURE(
                mRenderData.NRI.CreateCommandBuffer(*queuedFrame.commandAllocator, queuedFrame.commandBuffer));

        queuedFrame.cameraBufferOffset = cameraBufferSize * i;
        queuedFrame.modelBufferOffset = worldBufferSize * i;
        queuedFrame.boneBufferOffset = boneBufferSize * i;
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
    nri::BufferDesc bufferDesc = {};
    bufferDesc.size = GetCameraBufferSize(mRenderData) * mRenderData.GetQueuedFrameNum();
    bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
    nri::Buffer* buffer;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, bufferDesc, buffer));
    mRenderData.rdBuffers.push_back(buffer);
    return true;
}

bool Renderer::createSSBOs()
{
    {
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = GetWorldBufferSizePerFrame(mRenderData) * mRenderData.GetQueuedFrameNum();
        bufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;

        nri::Buffer* buffer = nullptr;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, bufferDesc, buffer));
        mRenderData.rdBuffers.push_back(buffer);
    }

    {
        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = GetBoneBufferSizePerFrame(mRenderData) * mRenderData.GetQueuedFrameNum();
        bufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;

        nri::Buffer* buffer = nullptr;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, bufferDesc, buffer));
        mRenderData.rdBuffers.push_back(buffer);
    }

    return true;
}

bool Renderer::allocateAndBindMemory()
{
    std::vector<nri::Memory*> allocations;

    auto allocGroup = [&](nri::MemoryLocation memoryLocation,
                          nri::Buffer** buffers,
                          uint32_t bufferNum,
                          nri::Texture** textures,
                          uint32_t aiTextureNum) -> bool
    {
        nri::ResourceGroupDesc groupDesc = {};
        groupDesc.memoryLocation = memoryLocation;
        groupDesc.bufferNum = bufferNum;
        groupDesc.buffers = buffers;
        groupDesc.textureNum = aiTextureNum;
        groupDesc.textures = textures;

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

    nri::Buffer* uploadBuffers[] = {mRenderData.rdBuffers[VP_MATRIX_BUFFER],
                                    mRenderData.rdBuffers[WORLD_POS_BUFFER],
                                    mRenderData.rdBuffers[MODEL_BONE_BUFFER]};

    if (!allocGroup(nri::MemoryLocation::HOST_UPLOAD, uploadBuffers, std::size(uploadBuffers), nullptr, 0))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: failed to allocate and bind memory for buffers\n",
                   __FUNCTION__);
        return false;
    }

    nri::Texture* deviceTextures[] = {mRenderData.rdDepthTexture};

    if (!allocGroup(nri::MemoryLocation::DEVICE, nullptr, 0, deviceTextures, std::size(deviceTextures)))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: failed to allocate and bind memory for textures\n",
                   __FUNCTION__);
        return false;
    }

    mRenderData.rdMemoryAllocations = std::move(allocations);

    return true;
}

bool Renderer::createDescriptors()
{
    nri::SamplerDesc samplerDesc = {};
    samplerDesc.filters = {nri::Filter::LINEAR, nri::Filter::LINEAR, nri::Filter::LINEAR, nri::FilterOp::AVERAGE};
    samplerDesc.anisotropy = 8;
    samplerDesc.addressModes = {nri::AddressMode::REPEAT, nri::AddressMode::REPEAT, nri::AddressMode::REPEAT};
    samplerDesc.mipMin = 0.0f;
    samplerDesc.mipMax = 16.0f;
    samplerDesc.compareOp = nri::CompareOp::NONE;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateSampler(*mRenderData.rdDevice, samplerDesc, mRenderData.anisotropicSampler));

    const uint64_t cameraBufferSize = GetCameraBufferSize(mRenderData);
    const uint64_t worldBufferSize = GetWorldBufferSizePerFrame(mRenderData);
    const uint64_t boneBufferSize = GetBoneBufferSizePerFrame(mRenderData);

    for (QueuedFrame& queuedFrame : mRenderData.rdQueuedFrames)
    {
        nri::BufferViewDesc cameraBufferViewDesc = {};
        cameraBufferViewDesc.buffer = mRenderData.rdBuffers[VP_MATRIX_BUFFER];
        cameraBufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
        cameraBufferViewDesc.offset = queuedFrame.cameraBufferOffset;
        cameraBufferViewDesc.size = cameraBufferSize;
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBufferView(cameraBufferViewDesc, queuedFrame.cameraBufferView));

        nri::BufferViewDesc modelBufferViewDesc = {};
        modelBufferViewDesc.buffer = mRenderData.rdBuffers[WORLD_POS_BUFFER];
        modelBufferViewDesc.viewType = nri::BufferViewType::SHADER_RESOURCE;
        modelBufferViewDesc.offset = queuedFrame.modelBufferOffset;
        modelBufferViewDesc.size = worldBufferSize;
        modelBufferViewDesc.structureStride = sizeof(glm::mat4);
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBufferView(modelBufferViewDesc, queuedFrame.modelBufferView));

        nri::BufferViewDesc boneBufferViewDesc = {};
        boneBufferViewDesc.buffer = mRenderData.rdBuffers[MODEL_BONE_BUFFER];
        boneBufferViewDesc.viewType = nri::BufferViewType::SHADER_RESOURCE;
        boneBufferViewDesc.offset = queuedFrame.boneBufferOffset;
        boneBufferViewDesc.size = boneBufferSize;
        boneBufferViewDesc.structureStride = sizeof(glm::mat4);
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBufferView(boneBufferViewDesc, queuedFrame.boneBufferView));

        nri::Descriptor* staticDescriptors[] = {queuedFrame.cameraBufferView, queuedFrame.modelBufferView};
        nri::UpdateDescriptorRangeDesc staticRanges[] = {
                {queuedFrame.staticDescriptorSet, 0, 0, &staticDescriptors[0], 1},
                {queuedFrame.staticDescriptorSet, 1, 0, &staticDescriptors[1], 1},
        };
        mRenderData.NRI.UpdateDescriptorRanges(staticRanges, helper::GetCountOf(staticRanges));

        nri::Descriptor* skinningDescriptors[] = {queuedFrame.cameraBufferView, queuedFrame.boneBufferView};
        nri::UpdateDescriptorRangeDesc skinningRanges[] = {
                {queuedFrame.skinnedDescriptorSet, 0, 0, &skinningDescriptors[0], 1},
                {queuedFrame.skinnedDescriptorSet, 1, 0, &skinningDescriptors[1], 1},
        };
        mRenderData.NRI.UpdateDescriptorRanges(skinningRanges, helper::GetCountOf(skinningRanges));
    }

    return true;
}

bool Renderer::createDescriptorPool()
{
    constexpr uint32_t materialNum = 1024;
    const uint32_t frameSetNum = mRenderData.GetQueuedFrameNum() * 2;
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.descriptorSetMaxNum = materialNum + frameSetNum;
    descriptorPoolDesc.textureMaxNum = materialNum * TEXTURES_PER_MATERIAL;
    descriptorPoolDesc.samplerMaxNum = materialNum;
    descriptorPoolDesc.constantBufferMaxNum = frameSetNum;
    descriptorPoolDesc.structuredBufferMaxNum = frameSetNum;

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateDescriptorPool(
            *mRenderData.rdDevice, descriptorPoolDesc, mRenderData.rdDescriptorPool));

    return true;
}

bool Renderer::createDescriptorLayouts()
{
    // set 0: texture + sampler
    mRenderData.rdTextureDescriptorRanges = {
            {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER},
            {0, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER},
    };

    nri::DescriptorSetDesc textureSetDesc = {0, // set = 0
                                            mRenderData.rdTextureDescriptorRanges.data(),
                                            static_cast<uint32_t>(mRenderData.rdTextureDescriptorRanges.size()),
                                            nri::DescriptorSetBits::NONE};

    // set 1: camera + matrix buffer
    mRenderData.rdBufferDescriptorRanges = {
            {0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::VERTEX_SHADER},
            {1, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::VERTEX_SHADER},
    };

    nri::DescriptorSetDesc bufferSetDesc = {1, // set = 1
                                           mRenderData.rdBufferDescriptorRanges.data(),
                                           static_cast<uint32_t>(mRenderData.rdBufferDescriptorRanges.size())};

    mRenderData.rdDescriptorSetDescs = {textureSetDesc, bufferSetDesc};

    return true;
}

bool Renderer::createDescriptorSets()
{
    mRenderData.rdDescriptorSets.resize(mRenderData.GetQueuedFrameNum() * 2);
    for (uint32_t i = 0; i < mRenderData.GetQueuedFrameNum(); ++i)
    {
        QueuedFrame& queuedFrame = mRenderData.rdQueuedFrames[i];

        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateDescriptorSets(
                *mRenderData.rdDescriptorPool, *mRenderData.rdPipelineLayout, 1, &queuedFrame.staticDescriptorSet, 1, 0));
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateDescriptorSets(*mRenderData.rdDescriptorPool,
                                                                    *mRenderData.rdSkinningPipelineLayout,
                                                                    1,
                                                                    &queuedFrame.skinnedDescriptorSet,
                                                                    1,
                                                                    0));

        mRenderData.rdDescriptorSets[i * 2] = queuedFrame.staticDescriptorSet;
        mRenderData.rdDescriptorSets[i * 2 + 1] = queuedFrame.skinnedDescriptorSet;
    }

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
        mRenderData.rdDepthTexture = depthTexture;
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
