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

#include <Renderer/ComputePipeline.h>
#include <Renderer/Renderer.h>
#include <Renderer/SkinningPipeline.h>
#include <RHIWrap/Helper.h>

using namespace RAnimation;

#ifndef AI_LOG
#    define AI_LOG(...)
#endif

namespace
{
    BufferDesc GetCameraBufferDesc()
    {
        return {"CameraBuffer",
                sizeof(RUploadMatrices),
                1,
                0,
                nri::BufferUsageBits::CONSTANT_BUFFER,
                nri::MemoryLocation::HOST_UPLOAD,
                true};
    }

    BufferDesc GetWorldMatrixBufferDesc(const RenderResourceBudget& budget)
    {
        return {"WorldMatrixBuffer",
                sizeof(glm::mat4),
                budget.maxWorldMatrices,
                sizeof(glm::mat4),
                nri::BufferUsageBits::SHADER_RESOURCE,
                nri::MemoryLocation::HOST_UPLOAD,
                true};
    }

    BufferDesc GetBoneMatrixBufferDesc(const RenderResourceBudget& budget)
    {
        return {"BoneMatrixBuffer",
                sizeof(glm::mat4),
                budget.GetMaxBoneMatrices(),
                sizeof(glm::mat4),
                nri::BufferUsageBits::SHADER_RESOURCE | nri::BufferUsageBits::SHADER_RESOURCE_STORAGE,
                nri::MemoryLocation::DEVICE,
                true};
    }

    BufferDesc GetNodeTransformBufferDesc(const RenderResourceBudget& budget)
    {
        return {"NodeTransformBuffer",
                sizeof(RNodeTransformData),
                budget.GetMaxNodeTransforms(),
                sizeof(RNodeTransformData),
                nri::BufferUsageBits::SHADER_RESOURCE,
                nri::MemoryLocation::HOST_UPLOAD,
                true};
    }

    BufferDesc GetTRSMatrixBufferDesc(const RenderResourceBudget& budget)
    {
        return {"TRSMatrixBuffer",
                sizeof(glm::mat4),
                budget.GetMaxNodeTransforms(),
                sizeof(glm::mat4),
                nri::BufferUsageBits::SHADER_RESOURCE | nri::BufferUsageBits::SHADER_RESOURCE_STORAGE,
                nri::MemoryLocation::DEVICE,
                true};
    }

    BufferDesc GetModelRootMatrixBufferDesc(const RenderResourceBudget& budget)
    {
        return {"ModelRootMatrixBuffer",
                sizeof(glm::mat4),
                budget.maxWorldMatrices,
                sizeof(glm::mat4),
                nri::BufferUsageBits::SHADER_RESOURCE,
                nri::MemoryLocation::HOST_UPLOAD,
                true};
    }

    BufferDesc GetNodeParentIndexBufferDesc(const RenderResourceBudget& budget)
    {
        return {"NodeParentIndexBuffer",
                sizeof(int32_t),
                budget.GetMaxNodeTransforms(),
                sizeof(int32_t),
                nri::BufferUsageBits::SHADER_RESOURCE,
                nri::MemoryLocation::HOST_UPLOAD,
                true};
    }

    BufferDesc GetBoneNodeIndexBufferDesc(const RenderResourceBudget& budget)
    {
        return {"BoneNodeIndexBuffer",
                sizeof(uint32_t),
                budget.GetMaxBoneMatrices(),
                sizeof(uint32_t),
                nri::BufferUsageBits::SHADER_RESOURCE,
                nri::MemoryLocation::HOST_UPLOAD,
                true};
    }

    BufferDesc GetBoneOffsetMatrixBufferDesc(const RenderResourceBudget& budget)
    {
        return {"BoneOffsetMatrixBuffer",
                sizeof(glm::mat4),
                budget.GetMaxBoneMatrices(),
                sizeof(glm::mat4),
                nri::BufferUsageBits::SHADER_RESOURCE,
                nri::MemoryLocation::HOST_UPLOAD,
                true};
    }

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

    uint32_t GetGroupCount(uint32_t itemCount, uint32_t threadGroupSize)
    {
        return (itemCount + threadGroupSize - 1) / threadGroupSize;
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

    if (!createMatrixUBO() || !createSSBOs() || !allocateAndBindMemory())
    {
        return false;
    }

    if (!createSwapchain() || !createSwapchainTextures())
    {
        return false;
    }

    if (!createQueuedFrames() || !createDescriptorSetLayouts() || !createPipelineLayout() || !createPipelines() ||
        !createDescriptorPool() || !createDescriptorSets() || !updateDescriptors())
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

    updateCameraBuffer();
    if (!updateModelBuffer(safeDeltaTime))
    {
        return false;
    }

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

void Renderer::updateCameraBuffer()
{
    mMatrixGenerateTimer.Start();

    glm::vec3 forward = glm::normalize(glm::vec3(
            std::cos(glm::radians(mRenderData.rdViewElevation)) * std::cos(glm::radians(mRenderData.rdViewAzimuth)),
            std::sin(glm::radians(mRenderData.rdViewElevation)),
            std::cos(glm::radians(mRenderData.rdViewElevation)) * std::sin(glm::radians(mRenderData.rdViewAzimuth))));
    glm::vec3 target = mRenderData.rdCameraWorldPosition + forward;

    RUploadMatrices matrices = {};
    matrices.viewMatrix = glm::lookAtRH(mRenderData.rdCameraWorldPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
    matrices.projectionMatrix = glm::perspectiveRH_ZO(glm::radians(static_cast<float>(mRenderData.rdFieldOfView)),
                                                      static_cast<float>(mRenderData.rdOutputResolution.x) /
                                                              static_cast<float>(mRenderData.rdOutputResolution.y),
                                                      0.1f,
                                                      500.0f);
    mRenderData.rdMatrixGenerateTime += mMatrixGenerateTimer.Stop();

    mUploadToUBOTimer.Start();
    nri::Buffer* cameraBuffer = mRenderData.rdResourceRegistry.GetBuffer(mRenderData.rdCameraBuffer);
    const uint64_t cameraOffset =
            mRenderData.rdResourceRegistry.GetOffsetForFrame(mRenderData.rdCameraBuffer, mRenderData.queuedFrameIndex);
    void* cameraDst = mRenderData.NRI.MapBuffer(*cameraBuffer, cameraOffset, sizeof(RUploadMatrices));
    std::memcpy(cameraDst, &matrices, sizeof(RUploadMatrices));
    mRenderData.NRI.UnmapBuffer(*cameraBuffer);
    mRenderData.rdUploadToUBOTime += mUploadToUBOTimer.Stop();
}

bool Renderer::updateModelBuffer(float deltaTime)
{
    const uint32_t frameIndex = mRenderData.queuedFrameIndex;
    RenderResourceRegistry& registry = mRenderData.rdResourceRegistry;

    mWorldPosMatrices.clear();
    mNodeTransformData.clear();
    mNodeParentIndices.clear();
    mBoneNodeIndices.clear();
    mBoneOffsetMatrices.clear();
    mModelRootMatrices.clear();
    mAnimatedDispatches.clear();

    for (const auto& modelType : mModelInstData.miModelInstancesPerModel)
    {
        if (modelType.second.empty())
        {
            continue;
        }

        std::shared_ptr<Model> model = modelType.second.front()->GetModel();
        if (model->HasAnimations() && !model->GetBoneList().empty())
        {
            const auto& nodes = model->GetNodeList();
            const auto& bones = model->GetBoneList();
            const uint32_t nodeCount = static_cast<uint32_t>(nodes.size());
            const uint32_t boneCount = static_cast<uint32_t>(bones.size());
            const uint32_t realInstanceCount = static_cast<uint32_t>(modelType.second.size());

            if (nodeCount == 0)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "{} error: animated model '{}' has no nodes\n",
                           __FUNCTION__,
                           model->GetModelFileName());
                return false;
            }

            std::unordered_map<std::string, uint32_t> nodeIndices;
            nodeIndices.reserve(nodes.size());
            for (uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                nodeIndices[nodes[nodeIndex]->GetNodeName()] = nodeIndex;
            }

            std::vector<int32_t> localParentIndices(nodeCount, -1);
            for (uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const auto parentNode = nodes[nodeIndex]->GetParentNode();
                if (parentNode == nullptr)
                {
                    continue;
                }

                const auto parentIter = nodeIndices.find(parentNode->GetNodeName());
                localParentIndices[nodeIndex] =
                        parentIter != nodeIndices.end() ? static_cast<int32_t>(parentIter->second) : -1;
            }

            std::vector<uint32_t> localBoneNodeIndices(boneCount, 0);
            std::vector<glm::mat4> localBoneOffsets(boneCount, glm::mat4(1.0f));
            for (const auto& bone : bones)
            {
                const uint32_t boneId = bone->GetBoneId();
                if (boneId >= boneCount)
                {
                    fmt::print(stderr,
                               fg(fmt::color::red),
                               "{} error: model '{}' has bone id {} outside bone count {}\n",
                               __FUNCTION__,
                               model->GetModelFileName(),
                               boneId,
                               boneCount);
                    return false;
                }

                const auto nodeIter = nodeIndices.find(bone->GetBoneName());
                if (nodeIter == nodeIndices.end())
                {
                    fmt::print(stderr,
                               fg(fmt::color::red),
                               "{} error: bone '{}' has no matching node in model '{}'\n",
                               __FUNCTION__,
                               bone->GetBoneName(),
                               model->GetModelFileName());
                    return false;
                }

                localBoneNodeIndices[boneId] = nodeIter->second;
                localBoneOffsets[boneId] = bone->GetOffsetMatrix();
            }

            AnimatedDispatch dispatch = {};
            dispatch.nodeTransformOffset = static_cast<uint32_t>(mNodeTransformData.size());
            dispatch.boneMatrixOffset = static_cast<uint32_t>(mBoneOffsetMatrices.size());
            dispatch.modelRootOffset = static_cast<uint32_t>(mModelRootMatrices.size());
            dispatch.numberOfNodes = nodeCount;
            dispatch.numberOfBones = boneCount;
            dispatch.instanceCount = realInstanceCount;

            for (uint32_t instanceIndex = 0; instanceIndex < realInstanceCount; ++instanceIndex)
            {
                const auto& instance = modelType.second[instanceIndex];

                mMatrixGenerateTimer.Start();
                instance->UpdateAnimationState(deltaTime);
                mRenderData.rdMatrixGenerateTime += mMatrixGenerateTimer.Stop();

                mModelRootMatrices.emplace_back(instance->GetLocalTransformMatrix());

                const uint32_t nodeBase = dispatch.nodeTransformOffset + instanceIndex * nodeCount;
                for (uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
                {
                    mNodeTransformData.emplace_back(nodes[nodeIndex]->GetNodeTransformData());

                    const int32_t localParentIndex = localParentIndices[nodeIndex];
                    mNodeParentIndices.emplace_back(localParentIndex >= 0
                                                            ? static_cast<int32_t>(
                                                                      nodeBase + static_cast<uint32_t>(localParentIndex))
                                                            : -1);
                }

                for (uint32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
                {
                    mBoneNodeIndices.emplace_back(nodeBase + localBoneNodeIndices[boneIndex]);
                    mBoneOffsetMatrices.emplace_back(localBoneOffsets[boneIndex]);
                }
            }

            mAnimatedDispatches.emplace_back(dispatch);
        }
        else
        {
            for (const auto& instance : modelType.second)
            {
                mMatrixGenerateTimer.Start();
                mWorldPosMatrices.emplace_back(instance->GetWorldTransformMatrix());
                mRenderData.rdMatrixGenerateTime += mMatrixGenerateTimer.Stop();
            }
        }
    }

    mRenderData.rdMatricesSize =
            static_cast<unsigned int>(mWorldPosMatrices.size() * sizeof(glm::mat4) +
                                      mBoneOffsetMatrices.size() * sizeof(glm::mat4) +
                                      mNodeTransformData.size() * sizeof(RNodeTransformData));
    mRenderData.rdResourceBudgetUsage.staticWorldMatrices = mWorldPosMatrices.size();
    mRenderData.rdResourceBudgetUsage.animatedInstances = mModelRootMatrices.size();
    mRenderData.rdResourceBudgetUsage.boneMatrices = std::max(mBoneOffsetMatrices.size(), mBoneNodeIndices.size());
    mRenderData.rdResourceBudgetUsage.nodeTransforms = std::max(mNodeTransformData.size(), mNodeParentIndices.size());
    mRenderData.rdResourceBudgetUsage.uploadBytes =
            mWorldPosMatrices.size() * sizeof(glm::mat4) +
            mNodeTransformData.size() * sizeof(RNodeTransformData) +
            mNodeParentIndices.size() * sizeof(int32_t) +
            mBoneNodeIndices.size() * sizeof(uint32_t) +
            mBoneOffsetMatrices.size() * sizeof(glm::mat4) +
            mModelRootMatrices.size() * sizeof(glm::mat4);

    const uint64_t maxWorldMatrices = mResourceBudget.maxWorldMatrices;
    const uint64_t maxBoneMatrices = mResourceBudget.GetMaxBoneMatrices();
    const uint64_t maxNodeTransforms = mResourceBudget.GetMaxNodeTransforms();
    if (mWorldPosMatrices.size() > maxWorldMatrices || mBoneOffsetMatrices.size() > maxBoneMatrices ||
        mBoneNodeIndices.size() > maxBoneMatrices || mNodeTransformData.size() > maxNodeTransforms ||
        mNodeParentIndices.size() > maxNodeTransforms || mModelRootMatrices.size() > maxWorldMatrices)
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: matrix upload capacity exceeded "
                   "(world={}/{}, boneOffsets={}/{}, boneNodeIndices={}/{}, nodeTransforms={}/{}, "
                   "nodeParents={}/{}, modelRoots={}/{})\n",
                   __FUNCTION__,
                   mWorldPosMatrices.size(),
                   maxWorldMatrices,
                   mBoneOffsetMatrices.size(),
                   maxBoneMatrices,
                   mBoneNodeIndices.size(),
                   maxBoneMatrices,
                   mNodeTransformData.size(),
                   maxNodeTransforms,
                   mNodeParentIndices.size(),
                   maxNodeTransforms,
                   mModelRootMatrices.size(),
                   maxWorldMatrices);
        return false;
    }

    auto uploadToBuffer = [&](BufferHandle handle, const void* src, size_t bytes)
    {
        nri::Buffer* buffer = registry.GetBuffer(handle);
        const uint64_t offset = registry.GetOffsetForFrame(handle, frameIndex);
        void* dst = mRenderData.NRI.MapBuffer(*buffer, offset, bytes);
        std::memcpy(dst, src, bytes);
        mRenderData.NRI.UnmapBuffer(*buffer);
    };

    if (!mWorldPosMatrices.empty())
    {
        mUploadToUBOTimer.Start();
        uploadToBuffer(mRenderData.rdWorldMatrixBuffer,
                       mWorldPosMatrices.data(),
                       mWorldPosMatrices.size() * sizeof(glm::mat4));
        mRenderData.rdUploadToUBOTime += mUploadToUBOTimer.Stop();
    }

    if (!mNodeTransformData.empty())
    {
        mUploadToUBOTimer.Start();
        uploadToBuffer(mRenderData.rdNodeTransformBuffer,
                       mNodeTransformData.data(),
                       mNodeTransformData.size() * sizeof(RNodeTransformData));
        uploadToBuffer(mRenderData.rdNodeParentIndexBuffer,
                       mNodeParentIndices.data(),
                       mNodeParentIndices.size() * sizeof(int32_t));
        uploadToBuffer(mRenderData.rdBoneNodeIndexBuffer,
                       mBoneNodeIndices.data(),
                       mBoneNodeIndices.size() * sizeof(uint32_t));
        uploadToBuffer(mRenderData.rdBoneOffsetMatrixBuffer,
                       mBoneOffsetMatrices.data(),
                       mBoneOffsetMatrices.size() * sizeof(glm::mat4));
        uploadToBuffer(mRenderData.rdModelRootMatrixBuffer,
                       mModelRootMatrices.data(),
                       mModelRootMatrices.size() * sizeof(glm::mat4));
        mRenderData.rdUploadToUBOTime += mUploadToUBOTimer.Stop();
    }

    return true;
}

bool Renderer::recordCommandBuffer()
{
    QueuedFrame& queuedFrame = mRenderData.GetCurrentQueueFrame();

    ImDrawData* drawData = ImGui::GetDrawData();
    const uint32_t imguiDrawListNum = drawData != nullptr ? static_cast<uint32_t>(drawData->CmdListsCount) : 0;
    const uint32_t imguiTextureNum = (drawData != nullptr && drawData->Textures != nullptr)
                                             ? static_cast<uint32_t>(drawData->Textures->Size)
                                             : 0;
    const bool hasSceneGeometry = !mModelInstData.miModelInstances.empty();

    AI_LOG("[AI] frame={} queuedFrame={} acquireIndex={} staticSet={} skinnedSet={} sceneGeometry={} imguiDrawLists={} "
           "imguiTextures={}\n",
           mFrameIndex,
           mRenderData.queuedFrameIndex,
           queuedFrame.swapChainTextureIndex,
           static_cast<const void*>(queuedFrame.staticDescriptorSet),
           static_cast<const void*>(queuedFrame.skinnedDescriptorSet),
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
        copyImguiDataDesc.textureNum = drawData->Textures != nullptr ? static_cast<uint32_t>(drawData->Textures->Size)
                                                                     : 0;
        mRenderData.NRI.CmdCopyImguiData(
                *queuedFrame.commandBuffer, *mRenderData.rdStreamer, *mRenderData.rdImgui, copyImguiDataDesc);
    }

    if (!mAnimatedDispatches.empty())
    {
        mRenderData.NRI.CmdSetDescriptorPool(*queuedFrame.commandBuffer, *mRenderData.rdDescriptorPool);

        nri::Buffer* trsBuffer = mRenderData.rdResourceRegistry.GetBuffer(mRenderData.rdTRSMatrixBuffer);
        nri::Buffer* boneBuffer = mRenderData.rdResourceRegistry.GetBuffer(mRenderData.rdBoneMatrixBuffer);

        nri::BufferBarrierDesc beginComputeBarriers[] = {
                {trsBuffer,
                 {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::ALL},
                 {nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER}},
                {boneBuffer,
                 {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::ALL},
                 {nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER}},
        };
        nri::BarrierDesc beginComputeBarrierDesc = {};
        beginComputeBarrierDesc.buffers = beginComputeBarriers;
        beginComputeBarrierDesc.bufferNum = helper::GetCountOf(beginComputeBarriers);
        mRenderData.NRI.CmdBarrier(*queuedFrame.commandBuffer, beginComputeBarrierDesc);

        mRenderData.NRI.CmdSetPipelineLayout(*queuedFrame.commandBuffer,
                                             nri::BindPoint::COMPUTE,
                                             *mRenderData.rdComputeTransformPipelineLayout);
        mRenderData.NRI.CmdSetPipeline(*queuedFrame.commandBuffer, *mRenderData.rdComputeTransformPipeline);
        mRenderData.NRI.CmdSetDescriptorSet(*queuedFrame.commandBuffer,
                                            {0, queuedFrame.computeTransformDescriptorSet, nri::BindPoint::COMPUTE});

        for (const AnimatedDispatch& dispatch : mAnimatedDispatches)
        {
            RComputePushConstants pushConstants = {};
            pushConstants.nodeTransformOffset = dispatch.nodeTransformOffset;
            pushConstants.boneMatrixOffset = dispatch.boneMatrixOffset;
            pushConstants.modelRootOffset = dispatch.modelRootOffset;
            pushConstants.numberOfNodes = dispatch.numberOfNodes;
            pushConstants.numberOfBones = dispatch.numberOfBones;
            pushConstants.instanceCount = dispatch.instanceCount;

            mRenderData.NRI.CmdSetRootConstants(
                    *queuedFrame.commandBuffer,
                    {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::COMPUTE});
            mRenderData.NRI.CmdDispatch(
                    *queuedFrame.commandBuffer,
                    {dispatch.numberOfNodes, GetGroupCount(dispatch.instanceCount, 32), 1});
        }

        nri::BufferBarrierDesc trsBarrier = {
                trsBuffer,
                {nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER},
                {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::COMPUTE_SHADER}};
        nri::BarrierDesc trsBarrierDesc = {};
        trsBarrierDesc.buffers = &trsBarrier;
        trsBarrierDesc.bufferNum = 1;
        mRenderData.NRI.CmdBarrier(*queuedFrame.commandBuffer, trsBarrierDesc);

        mRenderData.NRI.CmdSetPipelineLayout(*queuedFrame.commandBuffer,
                                             nri::BindPoint::COMPUTE,
                                             *mRenderData.rdComputeMatrixMultPipelineLayout);
        mRenderData.NRI.CmdSetPipeline(*queuedFrame.commandBuffer, *mRenderData.rdComputeMatrixMultPipeline);
        mRenderData.NRI.CmdSetDescriptorSet(
                *queuedFrame.commandBuffer,
                {0, queuedFrame.computeMatrixMultDescriptorSet0, nri::BindPoint::COMPUTE});
        mRenderData.NRI.CmdSetDescriptorSet(
                *queuedFrame.commandBuffer,
                {1, queuedFrame.computeMatrixMultDescriptorSet1, nri::BindPoint::COMPUTE});

        for (const AnimatedDispatch& dispatch : mAnimatedDispatches)
        {
            RComputePushConstants pushConstants = {};
            pushConstants.nodeTransformOffset = dispatch.nodeTransformOffset;
            pushConstants.boneMatrixOffset = dispatch.boneMatrixOffset;
            pushConstants.modelRootOffset = dispatch.modelRootOffset;
            pushConstants.numberOfNodes = dispatch.numberOfNodes;
            pushConstants.numberOfBones = dispatch.numberOfBones;
            pushConstants.instanceCount = dispatch.instanceCount;

            mRenderData.NRI.CmdSetRootConstants(
                    *queuedFrame.commandBuffer,
                    {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::COMPUTE});
            mRenderData.NRI.CmdDispatch(
                    *queuedFrame.commandBuffer,
                    {dispatch.numberOfBones, GetGroupCount(dispatch.instanceCount, 32), 1});
        }

        nri::BufferBarrierDesc boneMatrixBarrier = {
                boneBuffer,
                {nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER},
                {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::VERTEX_SHADER}};
        nri::BarrierDesc boneMatrixBarrierDesc = {};
        boneMatrixBarrierDesc.buffers = &boneMatrixBarrier;
        boneMatrixBarrierDesc.bufferNum = 1;
        mRenderData.NRI.CmdBarrier(*queuedFrame.commandBuffer, boneMatrixBarrierDesc);
    }

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
                const uint32_t boneCount = static_cast<uint32_t>(model->GetBoneList().size());

                RPushConstants pushConstants = {};
                pushConstants.modelStride = static_cast<int>(boneCount);
                pushConstants.worldPosOffset = static_cast<int>(boneMatrixOffset);

                const uint32_t requiredBoneMatrices = instanceCount * boneCount;
                if (boneMatrixOffset + requiredBoneMatrices > mBoneOffsetMatrices.size())
                {
                    fmt::print(stderr,
                               fg(fmt::color::red),
                               "{} error: skinning upload range for model '{}' exceeds uploaded bone matrix count "
                               "(offset={}, required={}, uploaded={})\n",
                               __FUNCTION__,
                               model->GetModelFileName(),
                               boneMatrixOffset,
                               requiredBoneMatrices,
                               mBoneOffsetMatrices.size());
                    return false;
                }

                AI_LOG("[AI] frame={} animated model='{}' instances={} stride={} uploadedBones={} offset={}\n",
                       mFrameIndex,
                       model->GetModelFileName(),
                       instanceCount,
                       pushConstants.modelStride,
                       mBoneOffsetMatrices.size(),
                       boneMatrixOffset);

                mRenderData.NRI.CmdSetPipelineLayout(*queuedFrame.commandBuffer,
                                                     nri::BindPoint::GRAPHICS,
                                                     *mRenderData.rdSkinningPipelineLayout);
                mRenderData.NRI.CmdSetPipeline(*queuedFrame.commandBuffer, *mRenderData.rdSkinningPipeline);
                mRenderData.NRI.CmdSetRootConstants(
                        *queuedFrame.commandBuffer,
                        {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::GRAPHICS});
                mRenderData.NRI.CmdSetDescriptorSet(*queuedFrame.commandBuffer,
                                                    {1, queuedFrame.skinnedDescriptorSet, nri::BindPoint::GRAPHICS});
                model->DrawInstanced(mRenderData, instanceCount);

                boneMatrixOffset += requiredBoneMatrices;
            }
            else
            {
                RPushConstants pushConstants = {};
                pushConstants.modelStride = 0;
                pushConstants.worldPosOffset = static_cast<int>(worldPosOffset);

                mRenderData.NRI.CmdSetPipelineLayout(*queuedFrame.commandBuffer,
                                                     nri::BindPoint::GRAPHICS,
                                                     *mRenderData.rdPipelineLayout);
                mRenderData.NRI.CmdSetPipeline(*queuedFrame.commandBuffer, *mRenderData.rdPipeline);
                mRenderData.NRI.CmdSetRootConstants(
                        *queuedFrame.commandBuffer,
                        {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::GRAPHICS});
                mRenderData.NRI.CmdSetDescriptorSet(*queuedFrame.commandBuffer,
                                                    {1, queuedFrame.staticDescriptorSet, nri::BindPoint::GRAPHICS});
                model->DrawInstanced(mRenderData, instanceCount);

                worldPosOffset += instanceCount;
            }
        }

        mRenderData.NRI.CmdEndRendering(*queuedFrame.commandBuffer);
        mDepthAttachmentInitialized = true;
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

    SkinningPipeline::Cleanup(mRenderData, mRenderData.rdPipeline);
    SkinningPipeline::Cleanup(mRenderData, mRenderData.rdSkinningPipeline);

    if (mRenderData.rdSkinningPipelineLayout != nullptr &&
        mRenderData.rdSkinningPipelineLayout != mRenderData.rdPipelineLayout)
    {
        mRenderData.NRI.DestroyPipelineLayout(mRenderData.rdSkinningPipelineLayout);
    }

    mRenderData.NRI.DestroyPipelineLayout(mRenderData.rdPipelineLayout);

    ComputePipeline::Cleanup(mRenderData, mRenderData.rdComputeTransformPipeline);
    ComputePipeline::Cleanup(mRenderData, mRenderData.rdComputeMatrixMultPipeline);
    mRenderData.NRI.DestroyPipelineLayout(mRenderData.rdComputeTransformPipelineLayout);
    mRenderData.NRI.DestroyPipelineLayout(mRenderData.rdComputeMatrixMultPipelineLayout);

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
    mRenderData.rdSkinningPipelineLayout = mRenderData.rdPipelineLayout;

    nri::RootConstantDesc computeRootConstantDesc = {};
    computeRootConstantDesc.registerIndex = 0;
    computeRootConstantDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
    computeRootConstantDesc.size = sizeof(RComputePushConstants);

    nri::PipelineLayoutDesc computeTransformLayoutDesc = {};
    computeTransformLayoutDesc.rootConstantNum = 1;
    computeTransformLayoutDesc.rootConstants = &computeRootConstantDesc;
    computeTransformLayoutDesc.rootRegisterSpace = 2;
    computeTransformLayoutDesc.descriptorSetNum =
            static_cast<uint32_t>(mRenderData.rdComputeTransformDescriptorSetDescs.size());
    computeTransformLayoutDesc.descriptorSets = mRenderData.rdComputeTransformDescriptorSetDescs.data();
    computeTransformLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreatePipelineLayout(
            *mRenderData.rdDevice, computeTransformLayoutDesc, mRenderData.rdComputeTransformPipelineLayout));

    nri::PipelineLayoutDesc computeMatrixLayoutDesc = {};
    computeMatrixLayoutDesc.rootConstantNum = 1;
    computeMatrixLayoutDesc.rootConstants = &computeRootConstantDesc;
    computeMatrixLayoutDesc.rootRegisterSpace = 2;
    computeMatrixLayoutDesc.descriptorSetNum =
            static_cast<uint32_t>(mRenderData.rdComputeMatrixMultDescriptorSetDescs.size());
    computeMatrixLayoutDesc.descriptorSets = mRenderData.rdComputeMatrixMultDescriptorSetDescs.data();
    computeMatrixLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreatePipelineLayout(
            *mRenderData.rdDevice, computeMatrixLayoutDesc, mRenderData.rdComputeMatrixMultPipelineLayout));

    return true;
}

bool Renderer::createPipelines()
{
    std::string computeShaderFile = SHADER_SRC_DIR "/SkeletalAnimCompute/assimp_instance_transform.cs";
    if (!ComputePipeline::Init(mRenderData,
                               *mRenderData.rdComputeTransformPipelineLayout,
                               mRenderData.rdComputeTransformPipeline,
                               computeShaderFile))
    {
        fmt::print(stderr, "{} error: could not init compute transform shader pipeline\n", __FUNCTION__);
        return false;
    }

    computeShaderFile = SHADER_SRC_DIR "/SkeletalAnimCompute/assimp_instance_matrix_mult.cs";
    if (!ComputePipeline::Init(mRenderData,
                               *mRenderData.rdComputeMatrixMultPipelineLayout,
                               mRenderData.rdComputeMatrixMultPipeline,
                               computeShaderFile))
    {
        fmt::print(stderr, "{} error: could not init compute matrix multiplication shader pipeline\n", __FUNCTION__);
        return false;
    }

    std::string vertexShaderFile = SHADER_SRC_DIR "/SkeletalAnimCompute/assimp.vs";
    std::string fragmentShaderFile = SHADER_SRC_DIR "/SkeletalAnimCompute/assimp.fs";
    if (!SkinningPipeline::Init(mRenderData,
                                *mRenderData.rdPipelineLayout,
                                mRenderData.rdPipeline,
                                vertexShaderFile,
                                fragmentShaderFile))
    {
        fmt::print(stderr, "{} error: could not init shader pipeline\n", __FUNCTION__);
        return false;
    }

    vertexShaderFile = SHADER_SRC_DIR "/SkeletalAnimCompute/assimp_skinning.vs";
    fragmentShaderFile = SHADER_SRC_DIR "/SkeletalAnimCompute/assimp_skinning.fs";
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
    RenderResourceRegistry& registry = mRenderData.rdResourceRegistry;
    mRenderData.rdCameraBuffer = registry.RegisterBuffer(GetCameraBufferDesc());
    mRenderData.rdCameraBufferView = registry.RegisterView(mRenderData.rdCameraBuffer,
                                                            nri::BufferViewType::CONSTANT);
    return true;
}

bool Renderer::createSSBOs()
{
    RenderResourceRegistry& registry = mRenderData.rdResourceRegistry;

    mRenderData.rdWorldMatrixBuffer = registry.RegisterBuffer(GetWorldMatrixBufferDesc(mResourceBudget));
    mRenderData.rdNodeTransformBuffer = registry.RegisterBuffer(GetNodeTransformBufferDesc(mResourceBudget));
    mRenderData.rdTRSMatrixBuffer = registry.RegisterBuffer(GetTRSMatrixBufferDesc(mResourceBudget));
    mRenderData.rdModelRootMatrixBuffer = registry.RegisterBuffer(GetModelRootMatrixBufferDesc(mResourceBudget));
    mRenderData.rdNodeParentIndexBuffer = registry.RegisterBuffer(GetNodeParentIndexBufferDesc(mResourceBudget));
    mRenderData.rdBoneNodeIndexBuffer = registry.RegisterBuffer(GetBoneNodeIndexBufferDesc(mResourceBudget));
    mRenderData.rdBoneOffsetMatrixBuffer = registry.RegisterBuffer(GetBoneOffsetMatrixBufferDesc(mResourceBudget));
    mRenderData.rdBoneMatrixBuffer = registry.RegisterBuffer(GetBoneMatrixBufferDesc(mResourceBudget));

    mRenderData.rdWorldMatrixBufferView =
            registry.RegisterView(mRenderData.rdWorldMatrixBuffer, nri::BufferViewType::SHADER_RESOURCE);
    mRenderData.rdBoneMatrixBufferView =
            registry.RegisterView(mRenderData.rdBoneMatrixBuffer, nri::BufferViewType::SHADER_RESOURCE);
    mRenderData.rdBoneMatrixStorageView =
            registry.RegisterView(mRenderData.rdBoneMatrixBuffer, nri::BufferViewType::SHADER_RESOURCE_STORAGE);
    mRenderData.rdNodeTransformBufferView =
            registry.RegisterView(mRenderData.rdNodeTransformBuffer, nri::BufferViewType::SHADER_RESOURCE);
    mRenderData.rdTRSMatrixBufferView =
            registry.RegisterView(mRenderData.rdTRSMatrixBuffer, nri::BufferViewType::SHADER_RESOURCE);
    mRenderData.rdTRSMatrixStorageView =
            registry.RegisterView(mRenderData.rdTRSMatrixBuffer, nri::BufferViewType::SHADER_RESOURCE_STORAGE);
    mRenderData.rdModelRootMatrixBufferView =
            registry.RegisterView(mRenderData.rdModelRootMatrixBuffer, nri::BufferViewType::SHADER_RESOURCE);
    mRenderData.rdNodeParentIndexBufferView =
            registry.RegisterView(mRenderData.rdNodeParentIndexBuffer, nri::BufferViewType::SHADER_RESOURCE);
    mRenderData.rdBoneNodeIndexBufferView =
            registry.RegisterView(mRenderData.rdBoneNodeIndexBuffer, nri::BufferViewType::SHADER_RESOURCE);
    mRenderData.rdBoneOffsetMatrixBufferView =
            registry.RegisterView(mRenderData.rdBoneOffsetMatrixBuffer, nri::BufferViewType::SHADER_RESOURCE);

    return registry.CreateBuffers(mRenderData);
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

bool Renderer::updateDescriptors()
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

    RenderResourceRegistry& registry = mRenderData.rdResourceRegistry;
    if (!registry.CreateViews(mRenderData))
    {
        return false;
    }

    for (uint32_t frameIndex = 0; frameIndex < mRenderData.rdQueuedFrames.size(); ++frameIndex)
    {
        QueuedFrame& queuedFrame = mRenderData.rdQueuedFrames[frameIndex];

        nri::Descriptor* cameraView = registry.GetView(mRenderData.rdCameraBufferView, frameIndex);
        nri::Descriptor* worldView = registry.GetView(mRenderData.rdWorldMatrixBufferView, frameIndex);
        nri::Descriptor* boneSrView = registry.GetView(mRenderData.rdBoneMatrixBufferView, frameIndex);
        nri::Descriptor* boneStorageView = registry.GetView(mRenderData.rdBoneMatrixStorageView, frameIndex);
        nri::Descriptor* nodeTransformView = registry.GetView(mRenderData.rdNodeTransformBufferView, frameIndex);
        nri::Descriptor* trsSrView = registry.GetView(mRenderData.rdTRSMatrixBufferView, frameIndex);
        nri::Descriptor* trsStorageView = registry.GetView(mRenderData.rdTRSMatrixStorageView, frameIndex);
        nri::Descriptor* modelRootView = registry.GetView(mRenderData.rdModelRootMatrixBufferView, frameIndex);
        nri::Descriptor* nodeParentView = registry.GetView(mRenderData.rdNodeParentIndexBufferView, frameIndex);
        nri::Descriptor* boneNodeView = registry.GetView(mRenderData.rdBoneNodeIndexBufferView, frameIndex);
        nri::Descriptor* boneOffsetView = registry.GetView(mRenderData.rdBoneOffsetMatrixBufferView, frameIndex);

        nri::Descriptor* staticDescriptors[] = {cameraView, worldView};
        nri::UpdateDescriptorRangeDesc staticRanges[] = {
                {queuedFrame.staticDescriptorSet, 0, 0, &staticDescriptors[0], 1},
                {queuedFrame.staticDescriptorSet, 1, 0, &staticDescriptors[1], 1},
        };
        mRenderData.NRI.UpdateDescriptorRanges(staticRanges, helper::GetCountOf(staticRanges));

        nri::Descriptor* skinningDescriptors[] = {cameraView, boneSrView};
        nri::UpdateDescriptorRangeDesc skinningRanges[] = {
                {queuedFrame.skinnedDescriptorSet, 0, 0, &skinningDescriptors[0], 1},
                {queuedFrame.skinnedDescriptorSet, 1, 0, &skinningDescriptors[1], 1},
        };
        mRenderData.NRI.UpdateDescriptorRanges(skinningRanges, helper::GetCountOf(skinningRanges));

        nri::Descriptor* transformDescriptors[] = {nodeTransformView, trsStorageView};
        nri::UpdateDescriptorRangeDesc transformRanges[] = {
                {queuedFrame.computeTransformDescriptorSet, 0, 0, &transformDescriptors[0], 1},
                {queuedFrame.computeTransformDescriptorSet, 1, 0, &transformDescriptors[1], 1},
        };
        mRenderData.NRI.UpdateDescriptorRanges(transformRanges, helper::GetCountOf(transformRanges));

        nri::Descriptor* matrixSet0Descriptors[] = {trsSrView, boneStorageView};
        nri::UpdateDescriptorRangeDesc matrixSet0Ranges[] = {
                {queuedFrame.computeMatrixMultDescriptorSet0, 0, 0, &matrixSet0Descriptors[0], 1},
                {queuedFrame.computeMatrixMultDescriptorSet0, 1, 0, &matrixSet0Descriptors[1], 1},
        };
        mRenderData.NRI.UpdateDescriptorRanges(matrixSet0Ranges, helper::GetCountOf(matrixSet0Ranges));

        nri::Descriptor* matrixSet1Descriptors[] = {nodeParentView, boneOffsetView, modelRootView, boneNodeView};
        nri::UpdateDescriptorRangeDesc matrixSet1Ranges[] = {
                {queuedFrame.computeMatrixMultDescriptorSet1, 0, 0, &matrixSet1Descriptors[0], 1},
                {queuedFrame.computeMatrixMultDescriptorSet1, 1, 0, &matrixSet1Descriptors[1], 1},
                {queuedFrame.computeMatrixMultDescriptorSet1, 2, 0, &matrixSet1Descriptors[2], 1},
                {queuedFrame.computeMatrixMultDescriptorSet1, 3, 0, &matrixSet1Descriptors[3], 1},
        };
        mRenderData.NRI.UpdateDescriptorRanges(matrixSet1Ranges, helper::GetCountOf(matrixSet1Ranges));
    }

    return true;
}

bool Renderer::createDescriptorPool()
{
    constexpr uint32_t materialNum = 1024;
    const uint32_t frameSetNum = mRenderData.GetQueuedFrameNum();
    nri::DescriptorPoolDesc descriptorPoolDesc = {};
    descriptorPoolDesc.descriptorSetMaxNum = materialNum + frameSetNum * 5;
    descriptorPoolDesc.textureMaxNum = materialNum * TEXTURES_PER_MATERIAL;
    descriptorPoolDesc.samplerMaxNum = materialNum;
    descriptorPoolDesc.constantBufferMaxNum = frameSetNum * 2;
    descriptorPoolDesc.structuredBufferMaxNum = frameSetNum * 8;
    descriptorPoolDesc.storageStructuredBufferMaxNum = frameSetNum * 2;

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateDescriptorPool(
            *mRenderData.rdDevice, descriptorPoolDesc, mRenderData.rdDescriptorPool));

    return true;
}

bool Renderer::createDescriptorSetLayouts()
{
    // set 0: texture + sampler
    // NOTE:
    // These are logical HLSL register indices, not final Vulkan binding numbers.
    // Final Vulkan bindings are produced by NRI by adding VK_BINDING_OFFSETS:
    //   s0 -> 100
    //   t0 -> 200
    mRenderData.rdTextureDescriptorRanges = {
            {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER},
            {0, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER},
    };

    nri::DescriptorSetDesc textureSetDesc = {0, // set = 0
                                             mRenderData.rdTextureDescriptorRanges.data(),
                                             static_cast<uint32_t>(mRenderData.rdTextureDescriptorRanges.size()),
                                             nri::DescriptorSetBits::NONE};

    // set 1: camera + matrix buffer
    // Final Vulkan bindings are produced by NRI by adding VK_BINDING_OFFSETS:
    //   b0 -> 300
    //   t1 -> 201
    mRenderData.rdBufferDescriptorRanges = {
            {0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::VERTEX_SHADER},
            {1, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::VERTEX_SHADER},
    };

    nri::DescriptorSetDesc bufferSetDesc = {1, // set = 1
                                            mRenderData.rdBufferDescriptorRanges.data(),
                                            static_cast<uint32_t>(mRenderData.rdBufferDescriptorRanges.size())};

    mRenderData.rdDescriptorSetDescs = {textureSetDesc, bufferSetDesc};


    // Needs Optimization: Adding an extra pass necessitates adding multiple `DescriptorRangeDesc` and
    // `DescriptorSetDesc` (analogous to Vulkan's `DescriptorSetLayout`), a `PipelineLayout*`, and a `Pipeline*` to
    // `mRenderData`; furthermore, it requires adding multiple `nri::Descriptor*` and `DescriptorSet*` to `QueuedFrame`.

    mRenderData.rdComputeTransformDescriptorRanges = {
            {0, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
    };

    nri::DescriptorSetDesc computeTransformSetDesc = {0, // set = 0
                                                      mRenderData.rdComputeTransformDescriptorRanges.data(),
                                                      static_cast<uint32_t>(
                                                              mRenderData.rdComputeTransformDescriptorRanges.size())};
    mRenderData.rdComputeTransformDescriptorSetDescs = {computeTransformSetDesc};

    mRenderData.rdComputeMatrixMultDescriptorRanges1 = {
            {0, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
    };

    nri::DescriptorSetDesc computeMatrixMultSetDesc = {
            0, // set = 0
            mRenderData.rdComputeMatrixMultDescriptorRanges1.data(),
            static_cast<uint32_t>(mRenderData.rdComputeMatrixMultDescriptorRanges1.size())};

    mRenderData.rdComputeMatrixMultDescriptorRanges2 = {
            {0, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
            {3, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
    };
    nri::DescriptorSetDesc computeMatrixMultSetDesc2 = {
            1, // set = 1
            mRenderData.rdComputeMatrixMultDescriptorRanges2.data(),
            static_cast<uint32_t>(mRenderData.rdComputeMatrixMultDescriptorRanges2.size())};
    mRenderData.rdComputeMatrixMultDescriptorSetDescs = {computeMatrixMultSetDesc, computeMatrixMultSetDesc2};
    return true;
}

bool Renderer::createDescriptorSets()
{
    for (uint32_t i = 0; i < mRenderData.GetQueuedFrameNum(); ++i)
    {
        QueuedFrame& queuedFrame = mRenderData.rdQueuedFrames[i];

        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateDescriptorSets(*mRenderData.rdDescriptorPool,
                                                                    *mRenderData.rdPipelineLayout,
                                                                    1,
                                                                    &queuedFrame.staticDescriptorSet,
                                                                    1,
                                                                    0));
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateDescriptorSets(*mRenderData.rdDescriptorPool,
                                                                    *mRenderData.rdSkinningPipelineLayout,
                                                                    1,
                                                                    &queuedFrame.skinnedDescriptorSet,
                                                                    1,
                                                                    0));
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateDescriptorSets(*mRenderData.rdDescriptorPool,
                                                                    *mRenderData.rdComputeTransformPipelineLayout,
                                                                    0,
                                                                    &queuedFrame.computeTransformDescriptorSet,
                                                                    1,
                                                                    0));
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateDescriptorSets(*mRenderData.rdDescriptorPool,
                                                                    *mRenderData.rdComputeMatrixMultPipelineLayout,
                                                                    0,
                                                                    &queuedFrame.computeMatrixMultDescriptorSet0,
                                                                    1,
                                                                    0));
        NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateDescriptorSets(*mRenderData.rdDescriptorPool,
                                                                    *mRenderData.rdComputeMatrixMultPipelineLayout,
                                                                    1,
                                                                    &queuedFrame.computeMatrixMultDescriptorSet1,
                                                                    1,
                                                                    0));
    }
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
