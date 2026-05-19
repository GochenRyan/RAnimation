#include <Renderer/Passes/SkinnedMeshDrawPass.h>

#include <cstddef>

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
        struct PushConstants
        {
            int modelStride = 0;
            int worldPosOffset = 0;
        };
    } // namespace

    bool SkinnedMeshDrawPass::DeclareResources(ResourceContext& context)
    {
        // Shares CameraBuffer (owned by StaticMeshDrawPass) and BoneMatrixBuffer
        // (owned by BoneMatrixComputePass). Re-registration with identical desc is idempotent.
        mCameraBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kCameraBuffer,
                                                              SceneBufferDescs::Camera());
        mBoneMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneMatrixBuffer,
                                                                  SceneBufferDescs::BoneMatrix(context.budget));

        mCameraView = context.registry.RegisterSharedView(SceneResourceNames::kCameraBufferView,
                                                          mCameraBuffer,
                                                          nri::BufferViewType::CONSTANT);
        mBoneMatrixView = context.registry.RegisterSharedView(SceneResourceNames::kBoneMatrixBufferView,
                                                              mBoneMatrixBuffer,
                                                              nri::BufferViewType::SHADER_RESOURCE);

        return mCameraBuffer.IsValid() && mBoneMatrixBuffer.IsValid() && mCameraView.IsValid() &&
               mBoneMatrixView.IsValid();
    }

    bool SkinnedMeshDrawPass::CreatePipeline(RenderContext& context)
    {
        if (context.renderData.rdMaterialPipelineLayout == nullptr)
        {
            fmt::print(stderr,
                       fg(fmt::color::red),
                       "SkinnedMeshDrawPass error: rdMaterialPipelineLayout is null. "
                       "StaticMeshDrawPass must be registered before SkinnedMeshDrawPass.\n");
            return false;
        }

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;

        nri::VertexAttributeDesc vertexAttributeDesc[6] = {};
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

        nri::VertexInputDesc vertexInputDesc = {};
        vertexInputDesc.attributes = vertexAttributeDesc;
        vertexInputDesc.attributeNum = static_cast<uint8_t>(helper::GetCountOf(vertexAttributeDesc));
        vertexInputDesc.streams = &vertexStreamDesc;
        vertexInputDesc.streamNum = 1;

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::BACK;
        rasterizationDesc.frontCounterClockwise = true;
        rasterizationDesc.shadingRate = false;

        nri::MultisampleDesc multisampleDesc = {};
        multisampleDesc.sampleNum = 1;
        multisampleDesc.sampleLocations = false;

        nri::ColorAttachmentDesc colorAttachmentDesc = {};
        colorAttachmentDesc.format = context.swapChainFormat;
        colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDesc.blendEnabled = false;

        nri::DepthAttachmentDesc depthAttachmentDesc = {};
        depthAttachmentDesc.compareOp = nri::CompareOp::LESS_EQUAL;
        depthAttachmentDesc.write = true;
        depthAttachmentDesc.boundsTest = false;

        nri::StencilAttachmentDesc stencilAttachmentDesc = {};
        stencilAttachmentDesc.front.compareOp = nri::CompareOp::NONE;
        stencilAttachmentDesc.back.compareOp = nri::CompareOp::NONE;

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colors = &colorAttachmentDesc;
        outputMergerDesc.colorNum = 1;
        outputMergerDesc.depthStencilFormat = context.depthFormat;
        outputMergerDesc.depth = depthAttachmentDesc;
        outputMergerDesc.stencil = stencilAttachmentDesc;

        const nri::DeviceDesc& deviceDesc = context.NRI.GetDeviceDesc(context.device);
        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shaderStages[] = {
                utils::LoadShader(deviceDesc.graphicsAPI,
                                  SHADER_SRC_DIR "/SkeletalAnimCompute/assimp_skinning.vs",
                                  shaderCodeStorage),
                utils::LoadShader(deviceDesc.graphicsAPI,
                                  SHADER_SRC_DIR "/SkeletalAnimCompute/assimp_skinning.fs",
                                  shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = context.renderData.rdMaterialPipelineLayout;
        graphicsPipelineDesc.vertexInput = &vertexInputDesc;
        graphicsPipelineDesc.inputAssembly = inputAssemblyDesc;
        graphicsPipelineDesc.rasterization = rasterizationDesc;
        graphicsPipelineDesc.multisample = &multisampleDesc;
        graphicsPipelineDesc.outputMerger = outputMergerDesc;
        graphicsPipelineDesc.shaders = shaderStages;
        graphicsPipelineDesc.shaderNum = helper::GetCountOf(shaderStages);

        NRI_ABORT_ON_FAILURE(context.NRI.CreateGraphicsPipeline(context.device, graphicsPipelineDesc, mPipeline));
        return true;
    }

    DescriptorPoolRequirements SkinnedMeshDrawPass::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        DescriptorPoolRequirements req = {};
        req.descriptorSetMaxNum = queuedFrameNum;
        req.constantBufferMaxNum = queuedFrameNum;
        req.structuredBufferMaxNum = queuedFrameNum;
        return req;
    }

    bool SkinnedMeshDrawPass::CreateDescriptors(FrameContext& context)
    {
        mDescriptorSets.assign(context.queuedFrameNum, nullptr);
        for (uint32_t frameIndex = 0; frameIndex < context.queuedFrameNum; ++frameIndex)
        {
            NRI_ABORT_ON_FAILURE(context.NRI.AllocateDescriptorSets(context.descriptorPool,
                                                                    *context.renderData.rdMaterialPipelineLayout,
                                                                    1,
                                                                    &mDescriptorSets[frameIndex],
                                                                    1,
                                                                    0));

            nri::Descriptor* cameraView = context.registry.GetView(mCameraView, frameIndex);
            nri::Descriptor* boneSrView = context.registry.GetView(mBoneMatrixView, frameIndex);

            nri::Descriptor* descriptors[] = {cameraView, boneSrView};
            nri::UpdateDescriptorRangeDesc ranges[] = {
                    {mDescriptorSets[frameIndex], 0, 0, &descriptors[0], 1},
                    {mDescriptorSets[frameIndex], 1, 0, &descriptors[1], 1},
            };
            context.NRI.UpdateDescriptorRanges(ranges, helper::GetCountOf(ranges));
        }
        return true;
    }

    void SkinnedMeshDrawPass::DeclareAccess(RegistryAccessBuilder& builder) const
    {
        // Reads bone matrices as SR in vertex shader (skinning).
        builder.Use(mBoneMatrixBuffer, nri::AccessBits::SHADER_RESOURCE, nri::StageBits::VERTEX_SHADER);
    }

    void SkinnedMeshDrawPass::Record(CommandContext& context)
    {
        if (context.sceneFrame == nullptr || context.sceneFrame->modelInstData == nullptr)
        {
            return;
        }

        uint32_t boneMatrixOffset = 0;
        for (const auto& modelType : context.sceneFrame->modelInstData->miModelInstancesPerModel)
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

            const uint32_t instanceCount = static_cast<uint32_t>(modelType.second.size());
            const uint32_t boneCount = static_cast<uint32_t>(model->GetBoneList().size());

            PushConstants pushConstants = {};
            pushConstants.modelStride = static_cast<int>(boneCount);
            pushConstants.worldPosOffset = static_cast<int>(boneMatrixOffset);

            const uint32_t requiredBoneMatrices = instanceCount * boneCount;
            if (boneMatrixOffset + requiredBoneMatrices > context.sceneFrame->uploadedBoneOffsetMatrixCount)
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "SkinnedMeshDrawPass error: skinning upload range for model '{}' exceeds uploaded bone "
                           "matrix count (offset={}, required={}, uploaded={})\n",
                           model->GetModelFileName(),
                           boneMatrixOffset,
                           requiredBoneMatrices,
                           context.sceneFrame->uploadedBoneOffsetMatrixCount);
                return;
            }

            context.NRI.CmdSetPipelineLayout(context.commandBuffer,
                                             nri::BindPoint::GRAPHICS,
                                             *context.renderData.rdMaterialPipelineLayout);
            context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
            context.NRI.CmdSetRootConstants(context.commandBuffer,
                                            {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::GRAPHICS});
            context.NRI.CmdSetDescriptorSet(context.commandBuffer,
                                            {1, mDescriptorSets[context.frameIndex], nri::BindPoint::GRAPHICS});
            model->DrawInstanced(context.renderData, instanceCount);

            boneMatrixOffset += requiredBoneMatrices;
        }
    }

    void SkinnedMeshDrawPass::Cleanup(RRenderData& renderData)
    {
        if (mPipeline != nullptr)
        {
            renderData.NRI.DestroyPipeline(mPipeline);
            mPipeline = nullptr;
        }
        mDescriptorSets.clear();
    }
} // namespace RAnimation
