#include <Renderer/Passes/AnimationTransformComputePass.h>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/RenderData.h>
#include <Renderer/RenderResourceRegistry.h>
#include <Renderer/SceneFrameData.h>
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

        struct PushConstants
        {
            uint32_t nodeTransformOffset = 0;
            uint32_t boneMatrixOffset = 0;
            uint32_t modelRootOffset = 0;
            uint32_t numberOfNodes = 0;
            uint32_t numberOfBones = 0;
            uint32_t instanceCount = 0;
        };
    } // namespace

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
        rootConstantDesc.size = sizeof(PushConstants);

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

            nri::Descriptor* nodeTransformView =
                    context.registry.GetView(context.renderData.rdNodeTransformBufferView, frameIndex);
            nri::Descriptor* trsStorageView =
                    context.registry.GetView(context.renderData.rdTRSMatrixStorageView, frameIndex);

            nri::Descriptor* descriptors[] = {nodeTransformView, trsStorageView};
            nri::UpdateDescriptorRangeDesc ranges[] = {
                    {mDescriptorSets[frameIndex], 0, 0, &descriptors[0], 1},
                    {mDescriptorSets[frameIndex], 1, 0, &descriptors[1], 1},
            };
            context.NRI.UpdateDescriptorRanges(ranges, helper::GetCountOf(ranges));
        }
        return true;
    }

    void AnimationTransformComputePass::Record(CommandContext& context)
    {
        if (context.sceneFrame == nullptr || context.sceneFrame->animatedDispatches == nullptr ||
            context.sceneFrame->animatedDispatches->empty())
        {
            return;
        }

        nri::Buffer* trsBuffer = context.registry.GetBuffer(context.renderData.rdTRSMatrixBuffer);
        nri::Buffer* boneBuffer = context.registry.GetBuffer(context.renderData.rdBoneMatrixBuffer);

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
        context.NRI.CmdBarrier(context.commandBuffer, beginComputeBarrierDesc);

        context.NRI.CmdSetPipelineLayout(context.commandBuffer, nri::BindPoint::COMPUTE, *mPipelineLayout);
        context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
        context.NRI.CmdSetDescriptorSet(context.commandBuffer,
                                        {0, mDescriptorSets[context.frameIndex], nri::BindPoint::COMPUTE});

        for (const AnimatedDispatch& dispatch : *context.sceneFrame->animatedDispatches)
        {
            PushConstants pushConstants = {};
            pushConstants.nodeTransformOffset = dispatch.nodeTransformOffset;
            pushConstants.boneMatrixOffset = dispatch.boneMatrixOffset;
            pushConstants.modelRootOffset = dispatch.modelRootOffset;
            pushConstants.numberOfNodes = dispatch.numberOfNodes;
            pushConstants.numberOfBones = dispatch.numberOfBones;
            pushConstants.instanceCount = dispatch.instanceCount;

            context.NRI.CmdSetRootConstants(context.commandBuffer,
                                            {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::COMPUTE});
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
