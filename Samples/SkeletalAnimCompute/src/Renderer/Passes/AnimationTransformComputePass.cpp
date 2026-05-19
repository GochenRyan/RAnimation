#include <Renderer/Passes/AnimationTransformComputePass.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>
#include <Model/RenderData.h>
#include <Renderer/RenderResourceRegistry.h>
#include <Renderer/SceneBufferDescs.h>
#include <Renderer/SceneFrameData.h>
#include <Renderer/SceneResourceNames.h>
#include <RHIWrap/Helper.h>

namespace RAnimation
{
    namespace
    {
        constexpr uint32_t kThreadGroupSize = 32;

        uint32_t GroupCount(uint32_t itemCount, uint32_t threadGroupSize)
        {
            return (itemCount + threadGroupSize - 1) / threadGroupSize;
        }

    } // namespace

    bool AnimationTransformComputePass::DeclareResources(ResourceContext& context)
    {
        // Owns (compute): NodeTransformBuffer + TRSMatrixBuffer.
        mNodeTransformBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kNodeTransformBuffer,
                                                      SceneBufferDescs::NodeTransform(context.budget));
        mTRSMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kTRSMatrixBuffer,
                                                                 SceneBufferDescs::TRSMatrix(context.budget));

        // Registered for Upload (shared with BoneMatrixComputePass which reads them on GPU).
        mNodeParentIndexBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kNodeParentIndexBuffer,
                                                      SceneBufferDescs::NodeParentIndex(context.budget));
        mBoneNodeIndexBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneNodeIndexBuffer,
                                                      SceneBufferDescs::BoneNodeIndex(context.budget));
        mBoneOffsetMatrixBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneOffsetMatrixBuffer,
                                                      SceneBufferDescs::BoneOffsetMatrix(context.budget));
        mModelRootMatrixBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kModelRootMatrixBuffer,
                                                      SceneBufferDescs::ModelRootMatrix(context.budget));

        mNodeTransformView =
                context.registry.RegisterSharedView(SceneResourceNames::kNodeTransformBufferView,
                                                    mNodeTransformBuffer,
                                                    nri::BufferViewType::SHADER_RESOURCE);
        mTRSMatrixStorageView =
                context.registry.RegisterSharedView(SceneResourceNames::kTRSMatrixStorageView,
                                                    mTRSMatrixBuffer,
                                                    nri::BufferViewType::SHADER_RESOURCE_STORAGE);

        return mNodeTransformBuffer.IsValid() && mTRSMatrixBuffer.IsValid() &&
               mNodeParentIndexBuffer.IsValid() && mBoneNodeIndexBuffer.IsValid() &&
               mBoneOffsetMatrixBuffer.IsValid() && mModelRootMatrixBuffer.IsValid() &&
               mNodeTransformView.IsValid() && mTRSMatrixStorageView.IsValid();
    }

    bool AnimationTransformComputePass::CreatePipeline(RenderContext& context)
    {
        mDescriptorRanges = {
                {0, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
                {1, 1, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
        };

        nri::DescriptorSetDesc setDesc = {0,
                                          mDescriptorRanges.data(),
                                          static_cast<uint32_t>(mDescriptorRanges.size())};

        nri::RootConstantDesc rootConstantDesc = {};
        rootConstantDesc.registerIndex = 0;
        rootConstantDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        rootConstantDesc.size = sizeof(AnimatedDispatch);

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootConstants = &rootConstantDesc;
        layoutDesc.rootRegisterSpace = 2;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.descriptorSets = &setDesc;
        layoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        NRI_ABORT_ON_FAILURE(context.NRI.CreatePipelineLayout(context.device, layoutDesc, mPipelineLayout));

        const nri::DeviceDesc& deviceDesc = context.NRI.GetDeviceDesc(context.device);
        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shader = utils::LoadShader(deviceDesc.graphicsAPI,
                                                   SHADER_SRC_DIR "/SkeletalAnimCompute/assimp_instance_transform.cs",
                                                   shaderCodeStorage);

        nri::ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.pipelineLayout = mPipelineLayout;
        pipelineDesc.shader = shader;
        NRI_ABORT_ON_FAILURE(context.NRI.CreateComputePipeline(context.device, pipelineDesc, mPipeline));

        return true;
    }

    DescriptorPoolRequirements AnimationTransformComputePass::GetDescriptorPoolRequirements(
            uint32_t queuedFrameNum) const
    {
        DescriptorPoolRequirements req = {};
        req.descriptorSetMaxNum = queuedFrameNum;
        req.structuredBufferMaxNum = queuedFrameNum;          // node transforms
        req.storageStructuredBufferMaxNum = queuedFrameNum;    // TRS matrices
        return req;
    }

    bool AnimationTransformComputePass::CreateDescriptors(FrameContext& context)
    {
        mDescriptorSets.assign(context.queuedFrameNum, nullptr);
        for (uint32_t frameIndex = 0; frameIndex < context.queuedFrameNum; ++frameIndex)
        {
            NRI_ABORT_ON_FAILURE(context.NRI.AllocateDescriptorSets(context.descriptorPool,
                                                                    *mPipelineLayout,
                                                                    0,
                                                                    &mDescriptorSets[frameIndex],
                                                                    1,
                                                                    0));

            nri::Descriptor* nodeTransformView = context.registry.GetView(mNodeTransformView, frameIndex);
            nri::Descriptor* trsStorageView = context.registry.GetView(mTRSMatrixStorageView, frameIndex);

            nri::Descriptor* descriptors[] = {nodeTransformView, trsStorageView};
            nri::UpdateDescriptorRangeDesc ranges[] = {
                    {mDescriptorSets[frameIndex], 0, 0, &descriptors[0], 1},
                    {mDescriptorSets[frameIndex], 1, 0, &descriptors[1], 1},
            };
            context.NRI.UpdateDescriptorRanges(ranges, helper::GetCountOf(ranges));
        }
        return true;
    }

    void AnimationTransformComputePass::Upload(FrameContext& context)
    {
        mNodeTransformData.clear();
        mNodeParentIndices.clear();
        mBoneNodeIndices.clear();
        mBoneOffsetMatrices.clear();
        mModelRootMatrices.clear();
        mAnimatedDispatches.clear();

        if (context.sceneFrame == nullptr || context.sceneFrame->modelInstData == nullptr)
        {
            return;
        }

        ModelAndInstanceData& modelInstData = *context.sceneFrame->modelInstData;
        const RenderResourceBudget& budget = context.renderData.rdResourceBudget;

        for (const auto& modelType : modelInstData.miModelInstancesPerModel)
        {
            if (modelType.second.empty())
            {
                continue;
            }

            std::shared_ptr<Model> model = modelType.second.front()->GetModel();
            if (!model->HasAnimations() || model->GetBoneList().empty())
            {
                continue;
            }

            const auto& nodes = model->GetNodeList();
            const auto& bones = model->GetBoneList();
            const uint32_t nodeCount = static_cast<uint32_t>(nodes.size());
            const uint32_t boneCount = static_cast<uint32_t>(bones.size());
            const uint32_t realInstanceCount = static_cast<uint32_t>(modelType.second.size());

            if (nodeCount == 0)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "AnimationTransformComputePass::Upload error: animated model '{}' has no nodes\n",
                           model->GetModelFileName());
                return;
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
                               "AnimationTransformComputePass::Upload error: model '{}' has bone id {} outside "
                               "bone count {}\n",
                               model->GetModelFileName(),
                               boneId,
                               boneCount);
                    return;
                }

                const auto nodeIter = nodeIndices.find(bone->GetBoneName());
                if (nodeIter == nodeIndices.end())
                {
                    fmt::print(stderr,
                               fg(fmt::color::red),
                               "AnimationTransformComputePass::Upload error: bone '{}' has no matching node in "
                               "model '{}'\n",
                               bone->GetBoneName(),
                               model->GetModelFileName());
                    return;
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
                instance->UpdateAnimationState(context.deltaTime);
                context.renderData.rdMatrixGenerateTime += mMatrixGenerateTimer.Stop();

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

        // Resource budget usage reporting (animated portion).
        context.renderData.rdResourceBudgetUsage.animatedInstances = mModelRootMatrices.size();
        context.renderData.rdResourceBudgetUsage.boneMatrices =
                std::max(mBoneOffsetMatrices.size(), mBoneNodeIndices.size());
        context.renderData.rdResourceBudgetUsage.nodeTransforms =
                std::max(mNodeTransformData.size(), mNodeParentIndices.size());
        context.renderData.rdResourceBudgetUsage.uploadBytes +=
                mNodeTransformData.size() * sizeof(RNodeTransformData) +
                mNodeParentIndices.size() * sizeof(int32_t) +
                mBoneNodeIndices.size() * sizeof(uint32_t) +
                mBoneOffsetMatrices.size() * sizeof(glm::mat4) +
                mModelRootMatrices.size() * sizeof(glm::mat4);
        context.renderData.rdMatricesSize +=
                static_cast<unsigned int>(mBoneOffsetMatrices.size() * sizeof(glm::mat4) +
                                          mNodeTransformData.size() * sizeof(RNodeTransformData));

        // Capacity check.
        const uint64_t maxBoneMatrices = budget.GetMaxBoneMatrices();
        const uint64_t maxNodeTransforms = budget.GetMaxNodeTransforms();
        const uint64_t maxWorldMatrices = budget.maxWorldMatrices;
        if (mBoneOffsetMatrices.size() > maxBoneMatrices || mBoneNodeIndices.size() > maxBoneMatrices ||
            mNodeTransformData.size() > maxNodeTransforms || mNodeParentIndices.size() > maxNodeTransforms ||
            mModelRootMatrices.size() > maxWorldMatrices)
        {
            fmt::print(stderr,
                       fg(fmt::color::red),
                       "AnimationTransformComputePass::Upload error: animated upload capacity exceeded "
                       "(boneOffsets={}/{}, boneNodeIndices={}/{}, nodeTransforms={}/{}, nodeParents={}/{}, "
                       "modelRoots={}/{})\n",
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
            return;
        }

        // Upload to GPU buffers.
        if (mNodeTransformData.empty())
        {
            // Still publish the (empty) AnimatedDispatch state to SceneFrameData so consumers see a stable view.
            context.sceneFrame->animatedDispatches = &mAnimatedDispatches;
            context.sceneFrame->uploadedBoneOffsetMatrixCount = mBoneOffsetMatrices.size();
            return;
        }

        const uint32_t frameIndex = context.renderData.queuedFrameIndex;
        RenderResourceRegistry& registry = context.registry;

        auto uploadToBuffer = [&](BufferHandle handle, const void* src, size_t bytes)
        {
            nri::Buffer* buffer = registry.GetBuffer(handle);
            const uint64_t offset = registry.GetOffsetForFrame(handle, frameIndex);
            void* dst = context.NRI.MapBuffer(*buffer, offset, bytes);
            std::memcpy(dst, src, bytes);
            context.NRI.UnmapBuffer(*buffer);
        };

        mUploadToUBOTimer.Start();
        uploadToBuffer(mNodeTransformBuffer,
                       mNodeTransformData.data(),
                       mNodeTransformData.size() * sizeof(RNodeTransformData));
        uploadToBuffer(mNodeParentIndexBuffer,
                       mNodeParentIndices.data(),
                       mNodeParentIndices.size() * sizeof(int32_t));
        uploadToBuffer(mBoneNodeIndexBuffer,
                       mBoneNodeIndices.data(),
                       mBoneNodeIndices.size() * sizeof(uint32_t));
        uploadToBuffer(mBoneOffsetMatrixBuffer,
                       mBoneOffsetMatrices.data(),
                       mBoneOffsetMatrices.size() * sizeof(glm::mat4));
        uploadToBuffer(mModelRootMatrixBuffer,
                       mModelRootMatrices.data(),
                       mModelRootMatrices.size() * sizeof(glm::mat4));
        context.renderData.rdUploadToUBOTime += mUploadToUBOTimer.Stop();

        // Publish state for sibling passes via SceneFrameData.
        context.sceneFrame->animatedDispatches = &mAnimatedDispatches;
        context.sceneFrame->uploadedBoneOffsetMatrixCount = mBoneOffsetMatrices.size();
    }

    void AnimationTransformComputePass::DeclareAccess(RegistryAccessBuilder& builder) const
    {
        // Writes TRS matrices in compute storage.
        builder.Use(mTRSMatrixBuffer,
                    nri::AccessBits::SHADER_RESOURCE_STORAGE,
                    nri::StageBits::COMPUTE_SHADER);
    }

    void AnimationTransformComputePass::Record(CommandContext& context)
    {
        if (context.sceneFrame == nullptr || context.sceneFrame->animatedDispatches == nullptr ||
            context.sceneFrame->animatedDispatches->empty())
        {
            return;
        }

        context.NRI.CmdSetPipelineLayout(context.commandBuffer, nri::BindPoint::COMPUTE, *mPipelineLayout);
        context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
        context.NRI.CmdSetDescriptorSet(context.commandBuffer,
                                        {0, mDescriptorSets[context.frameIndex], nri::BindPoint::COMPUTE});

        for (const AnimatedDispatch& dispatch : *context.sceneFrame->animatedDispatches)
        {
            context.NRI.CmdSetRootConstants(context.commandBuffer,
                                            {0, &dispatch, sizeof(dispatch), 0, nri::BindPoint::COMPUTE});
            context.NRI.CmdDispatch(context.commandBuffer,
                                    {dispatch.numberOfNodes, GroupCount(dispatch.instanceCount, kThreadGroupSize), 1});
        }
    }

    void AnimationTransformComputePass::Cleanup(RRenderData& renderData)
    {
        if (mPipeline != nullptr)
        {
            renderData.NRI.DestroyPipeline(mPipeline);
            mPipeline = nullptr;
        }
        if (mPipelineLayout != nullptr)
        {
            renderData.NRI.DestroyPipelineLayout(mPipelineLayout);
            mPipelineLayout = nullptr;
        }
        mDescriptorSets.clear();
        mDescriptorRanges.clear();
    }
} // namespace RAnimation
