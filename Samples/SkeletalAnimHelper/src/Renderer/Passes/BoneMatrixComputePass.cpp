#include <Renderer/Passes/BoneMatrixComputePass.h>

#include <fmt/base.h>
#include <fmt/color.h>

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

    bool BoneMatrixComputePass::DeclareResources(ResourceContext& context)
    {
        // Owns: BoneMatrix (write), ModelRootMatrix, NodeParentIndex, BoneNodeIndex, BoneOffsetMatrix.
        // References (shared with AnimationTransformComputePass): TRSMatrix (read).
        mTRSMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kTRSMatrixBuffer,
                                                                 SceneBufferDescs::TRSMatrix(context.budget));
        mBoneMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneMatrixBuffer,
                                                                  SceneBufferDescs::BoneMatrix(context.budget));
        mNodeParentIndexBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kNodeParentIndexBuffer,
                                                      SceneBufferDescs::NodeParentIndex(context.budget));
        mBoneOffsetMatrixBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneOffsetMatrixBuffer,
                                                      SceneBufferDescs::BoneOffsetMatrix(context.budget));
        mModelRootMatrixBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kModelRootMatrixBuffer,
                                                      SceneBufferDescs::ModelRootMatrix(context.budget));
        mBoneNodeIndexBuffer =
                context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneNodeIndexBuffer,
                                                      SceneBufferDescs::BoneNodeIndex(context.budget));

        mTRSMatrixView = context.registry.RegisterSharedView(SceneResourceNames::kTRSMatrixBufferView,
                                                             mTRSMatrixBuffer,
                                                             nri::BufferViewType::SHADER_RESOURCE);
        mBoneMatrixStorageView =
                context.registry.RegisterSharedView(SceneResourceNames::kBoneMatrixStorageView,
                                                    mBoneMatrixBuffer,
                                                    nri::BufferViewType::SHADER_RESOURCE_STORAGE);
        mNodeParentIndexView =
                context.registry.RegisterSharedView(SceneResourceNames::kNodeParentIndexBufferView,
                                                    mNodeParentIndexBuffer,
                                                    nri::BufferViewType::SHADER_RESOURCE);
        mBoneOffsetMatrixView =
                context.registry.RegisterSharedView(SceneResourceNames::kBoneOffsetMatrixBufferView,
                                                    mBoneOffsetMatrixBuffer,
                                                    nri::BufferViewType::SHADER_RESOURCE);
        mModelRootMatrixView =
                context.registry.RegisterSharedView(SceneResourceNames::kModelRootMatrixBufferView,
                                                    mModelRootMatrixBuffer,
                                                    nri::BufferViewType::SHADER_RESOURCE);
        mBoneNodeIndexView = context.registry.RegisterSharedView(SceneResourceNames::kBoneNodeIndexBufferView,
                                                                 mBoneNodeIndexBuffer,
                                                                 nri::BufferViewType::SHADER_RESOURCE);

        return mTRSMatrixBuffer.IsValid() && mBoneMatrixBuffer.IsValid() &&
               mNodeParentIndexBuffer.IsValid() && mBoneOffsetMatrixBuffer.IsValid() &&
               mModelRootMatrixBuffer.IsValid() && mBoneNodeIndexBuffer.IsValid() &&
               mTRSMatrixView.IsValid() && mBoneMatrixStorageView.IsValid() &&
               mNodeParentIndexView.IsValid() && mBoneOffsetMatrixView.IsValid() &&
               mModelRootMatrixView.IsValid() && mBoneNodeIndexView.IsValid();
    }

    bool BoneMatrixComputePass::CreatePipeline(RenderContext& context)
    {
        mDescriptorRanges0 = {
                {0, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
                {1, 1, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
        };
        mDescriptorRanges1 = {
                {0, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
                {1, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
                {2, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
                {3, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},
        };

        nri::DescriptorSetDesc setDescs[] = {
                {0, mDescriptorRanges0.data(), static_cast<uint32_t>(mDescriptorRanges0.size())},
                {1, mDescriptorRanges1.data(), static_cast<uint32_t>(mDescriptorRanges1.size())},
        };

        nri::RootConstantDesc rootConstantDesc = {};
        rootConstantDesc.registerIndex = 0;
        rootConstantDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        rootConstantDesc.size = sizeof(AnimatedDispatch);

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootConstants = &rootConstantDesc;
        layoutDesc.rootRegisterSpace = 2;
        layoutDesc.descriptorSetNum = helper::GetCountOf(setDescs);
        layoutDesc.descriptorSets = setDescs;
        layoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        NRI_ABORT_ON_FAILURE(context.NRI.CreatePipelineLayout(context.device, layoutDesc, mPipelineLayout));

        const nri::DeviceDesc& deviceDesc = context.NRI.GetDeviceDesc(context.device);
        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shader = utils::LoadShader(deviceDesc.graphicsAPI,
                                                   SHADER_SRC_DIR "/SkeletalAnimHelper/assimp_instance_matrix_mult.cs",
                                                   shaderCodeStorage);

        nri::ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.pipelineLayout = mPipelineLayout;
        pipelineDesc.shader = shader;
        NRI_ABORT_ON_FAILURE(context.NRI.CreateComputePipeline(context.device, pipelineDesc, mPipeline));

        return true;
    }

    DescriptorPoolRequirements BoneMatrixComputePass::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        DescriptorPoolRequirements req = {};
        req.descriptorSetMaxNum = queuedFrameNum * 2;
        // Set 0: TRS (structured), Bone (storage structured)
        // Set 1: parent index, bone offset, model root, bone node index (4 structured)
        req.structuredBufferMaxNum = queuedFrameNum * (1 + 4);
        req.storageStructuredBufferMaxNum = queuedFrameNum * 1;
        return req;
    }

    bool BoneMatrixComputePass::CreateDescriptors(FrameContext& context)
    {
        mDescriptorSet0PerFrame.assign(context.queuedFrameNum, nullptr);
        mDescriptorSet1PerFrame.assign(context.queuedFrameNum, nullptr);

        for (uint32_t frameIndex = 0; frameIndex < context.queuedFrameNum; ++frameIndex)
        {
            NRI_ABORT_ON_FAILURE(context.NRI.AllocateDescriptorSets(context.descriptorPool,
                                                                    *mPipelineLayout,
                                                                    0,
                                                                    &mDescriptorSet0PerFrame[frameIndex],
                                                                    1,
                                                                    0));
            NRI_ABORT_ON_FAILURE(context.NRI.AllocateDescriptorSets(context.descriptorPool,
                                                                    *mPipelineLayout,
                                                                    1,
                                                                    &mDescriptorSet1PerFrame[frameIndex],
                                                                    1,
                                                                    0));

            nri::Descriptor* trsSrView = context.registry.GetView(mTRSMatrixView, frameIndex);
            nri::Descriptor* boneStorageView = context.registry.GetView(mBoneMatrixStorageView, frameIndex);
            nri::Descriptor* nodeParentView = context.registry.GetView(mNodeParentIndexView, frameIndex);
            nri::Descriptor* boneOffsetView = context.registry.GetView(mBoneOffsetMatrixView, frameIndex);
            nri::Descriptor* modelRootView = context.registry.GetView(mModelRootMatrixView, frameIndex);
            nri::Descriptor* boneNodeView = context.registry.GetView(mBoneNodeIndexView, frameIndex);

            nri::Descriptor* set0Descriptors[] = {trsSrView, boneStorageView};
            nri::UpdateDescriptorRangeDesc set0Ranges[] = {
                    {mDescriptorSet0PerFrame[frameIndex], 0, 0, &set0Descriptors[0], 1},
                    {mDescriptorSet0PerFrame[frameIndex], 1, 0, &set0Descriptors[1], 1},
            };
            context.NRI.UpdateDescriptorRanges(set0Ranges, helper::GetCountOf(set0Ranges));

            nri::Descriptor* set1Descriptors[] = {nodeParentView, boneOffsetView, modelRootView, boneNodeView};
            nri::UpdateDescriptorRangeDesc set1Ranges[] = {
                    {mDescriptorSet1PerFrame[frameIndex], 0, 0, &set1Descriptors[0], 1},
                    {mDescriptorSet1PerFrame[frameIndex], 1, 0, &set1Descriptors[1], 1},
                    {mDescriptorSet1PerFrame[frameIndex], 2, 0, &set1Descriptors[2], 1},
                    {mDescriptorSet1PerFrame[frameIndex], 3, 0, &set1Descriptors[3], 1},
            };
            context.NRI.UpdateDescriptorRanges(set1Ranges, helper::GetCountOf(set1Ranges));
        }
        return true;
    }

    void BoneMatrixComputePass::DeclareAccess(RegistryAccessBuilder& builder) const
    {
        // Reads TRS as SR, writes BoneMatrix as SRS.
        builder.Use(mTRSMatrixBuffer, nri::AccessBits::SHADER_RESOURCE, nri::StageBits::COMPUTE_SHADER);
        builder.Use(mBoneMatrixBuffer,
                    nri::AccessBits::SHADER_RESOURCE_STORAGE,
                    nri::StageBits::COMPUTE_SHADER);
    }

    void BoneMatrixComputePass::Record(CommandContext& context)
    {
        if (context.sceneFrame == nullptr || context.sceneFrame->animatedDispatches == nullptr ||
            context.sceneFrame->animatedDispatches->empty())
        {
            return;
        }

        context.NRI.CmdSetPipelineLayout(context.commandBuffer, nri::BindPoint::COMPUTE, *mPipelineLayout);
        context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
        context.NRI.CmdSetDescriptorSet(context.commandBuffer,
                                        {0, mDescriptorSet0PerFrame[context.frameIndex], nri::BindPoint::COMPUTE});
        context.NRI.CmdSetDescriptorSet(context.commandBuffer,
                                        {1, mDescriptorSet1PerFrame[context.frameIndex], nri::BindPoint::COMPUTE});

        for (const AnimatedDispatch& dispatch : *context.sceneFrame->animatedDispatches)
        {
            context.NRI.CmdSetRootConstants(context.commandBuffer,
                                            {0, &dispatch, sizeof(dispatch), 0, nri::BindPoint::COMPUTE});
            context.NRI.CmdDispatch(context.commandBuffer,
                                    {dispatch.numberOfBones, GroupCount(dispatch.instanceCount, kThreadGroupSize), 1});
        }
    }

    void BoneMatrixComputePass::Cleanup(RRenderData& renderData)
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
        mDescriptorSet0PerFrame.clear();
        mDescriptorSet1PerFrame.clear();
        mDescriptorRanges0.clear();
        mDescriptorRanges1.clear();
    }
} // namespace RAnimation
