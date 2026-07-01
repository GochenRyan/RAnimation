#include <Renderer/Passes/StaticMeshDrawPass.h>

#include <cstddef>
#include <cstring>

#include <fmt/base.h>
#include <fmt/color.h>
#include <glm/gtc/matrix_transform.hpp>

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
            int pickIDBase = 0;
        };
    } // namespace

    bool StaticMeshDrawPass::DeclareResources(ResourceContext& context)
    {
        // Owns CameraBuffer (also referenced by SkinnedMeshDrawPass) and WorldMatrixBuffer.
        mCameraBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kCameraBuffer,
                                                              SceneBufferDescs::Camera());
        mWorldMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kWorldMatrixBuffer,
                                                                   SceneBufferDescs::WorldMatrix(context.budget));

        mCameraView = context.registry.RegisterSharedView(SceneResourceNames::kCameraBufferView,
                                                          mCameraBuffer,
                                                          nri::BufferViewType::CONSTANT);
        mWorldMatrixView = context.registry.RegisterSharedView(SceneResourceNames::kWorldMatrixBufferView,
                                                               mWorldMatrixBuffer,
                                                               nri::BufferViewType::SHADER_RESOURCE);

        return mCameraBuffer.IsValid() && mWorldMatrixBuffer.IsValid() && mCameraView.IsValid() &&
               mWorldMatrixView.IsValid();
    }

    bool StaticMeshDrawPass::CreatePipeline(RenderContext& context)
    {
        mTextureRanges = {
                {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::FRAGMENT_SHADER},
                {0, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER},
        };
        mBufferRanges = {
                {0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::VERTEX_SHADER},
                {1, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::VERTEX_SHADER},
        };

        nri::DescriptorSetDesc setDescs[] = {
                {0, mTextureRanges.data(), static_cast<uint32_t>(mTextureRanges.size())},
                {1, mBufferRanges.data(), static_cast<uint32_t>(mBufferRanges.size())},
        };

        nri::RootConstantDesc rootConstantDesc = {};
        rootConstantDesc.registerIndex = 0;
        rootConstantDesc.shaderStages = nri::StageBits::VERTEX_SHADER;
        rootConstantDesc.size = sizeof(PushConstants);

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootConstants = &rootConstantDesc;
        layoutDesc.rootRegisterSpace = 2;
        layoutDesc.descriptorSetNum = helper::GetCountOf(setDescs);
        layoutDesc.descriptorSets = setDescs;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        // Owns the shared material pipeline layout — exposed on RRenderData for sibling passes
        // (SkinnedMeshDrawPass) and material descriptor set allocation (NRITexture).
        NRI_ABORT_ON_FAILURE(context.NRI.CreatePipelineLayout(context.device,
                                                              layoutDesc,
                                                              context.renderData.rdMaterialPipelineLayout));

        // Pipeline
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

        nri::ColorAttachmentDesc colorAttachmentDescs[2] = {};
        colorAttachmentDescs[0].format = context.swapChainFormat;
        colorAttachmentDescs[0].colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDescs[0].blendEnabled = false;
        // Second target: per-instance pick ID (see Renderer picking).
        colorAttachmentDescs[1].format = nri::Format::R32_UINT;
        colorAttachmentDescs[1].colorWriteMask = nri::ColorWriteBits::R;
        colorAttachmentDescs[1].blendEnabled = false;

        nri::DepthAttachmentDesc depthAttachmentDesc = {};
        depthAttachmentDesc.compareOp = nri::CompareOp::LESS_EQUAL;
        depthAttachmentDesc.write = true;
        depthAttachmentDesc.boundsTest = false;

        nri::StencilAttachmentDesc stencilAttachmentDesc = {};
        stencilAttachmentDesc.front.compareOp = nri::CompareOp::NONE;
        stencilAttachmentDesc.back.compareOp = nri::CompareOp::NONE;

        nri::OutputMergerDesc outputMergerDesc = {};
        outputMergerDesc.colors = colorAttachmentDescs;
        outputMergerDesc.colorNum = helper::GetCountOf(colorAttachmentDescs);
        outputMergerDesc.depthStencilFormat = context.depthFormat;
        outputMergerDesc.depth = depthAttachmentDesc;
        outputMergerDesc.stencil = stencilAttachmentDesc;

        const nri::DeviceDesc& deviceDesc = context.NRI.GetDeviceDesc(context.device);
        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shaderStages[] = {
                utils::LoadShader(deviceDesc.graphicsAPI,
                                  SHADER_SRC_DIR "/SkeletalAnimHelper/assimp.vs",
                                  shaderCodeStorage),
                utils::LoadShader(deviceDesc.graphicsAPI,
                                  SHADER_SRC_DIR "/SkeletalAnimHelper/assimp.fs",
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

    DescriptorPoolRequirements StaticMeshDrawPass::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        DescriptorPoolRequirements req = {};
        req.descriptorSetMaxNum = queuedFrameNum;
        // Set 1: camera (constant buffer) + world matrix (structured buffer)
        req.constantBufferMaxNum = queuedFrameNum;
        req.structuredBufferMaxNum = queuedFrameNum;
        return req;
    }

    bool StaticMeshDrawPass::CreateDescriptors(FrameContext& context)
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
            nri::Descriptor* worldView = context.registry.GetView(mWorldMatrixView, frameIndex);

            nri::Descriptor* descriptors[] = {cameraView, worldView};
            nri::UpdateDescriptorRangeDesc ranges[] = {
                    {mDescriptorSets[frameIndex], 0, 0, &descriptors[0], 1},
                    {mDescriptorSets[frameIndex], 1, 0, &descriptors[1], 1},
            };
            context.NRI.UpdateDescriptorRanges(ranges, helper::GetCountOf(ranges));
        }
        return true;
    }

    void StaticMeshDrawPass::Upload(FrameContext& context)
    {
        // ---- Camera upload (always runs) ----
        mMatrixGenerateTimer.Start();
        const RRenderData& rd = context.renderData;
        glm::vec3 forward = glm::normalize(glm::vec3(
                std::cos(glm::radians(rd.rdViewElevation)) * std::cos(glm::radians(rd.rdViewAzimuth)),
                std::sin(glm::radians(rd.rdViewElevation)),
                std::cos(glm::radians(rd.rdViewElevation)) * std::sin(glm::radians(rd.rdViewAzimuth))));
        glm::vec3 target = rd.rdCameraWorldPosition + forward;

        RUploadMatrices matrices = {};
        matrices.viewMatrix = glm::lookAtRH(rd.rdCameraWorldPosition, target, glm::vec3(0.0f, 1.0f, 0.0f));
        matrices.projectionMatrix =
                glm::perspectiveRH_ZO(glm::radians(static_cast<float>(rd.rdFieldOfView)),
                                      static_cast<float>(rd.rdOutputResolution.x) /
                                              static_cast<float>(rd.rdOutputResolution.y),
                                      0.1f,
                                      500.0f);
        context.renderData.rdMatrixGenerateTime += mMatrixGenerateTimer.Stop();

        const uint32_t frameIndex = context.renderData.queuedFrameIndex;
        RenderResourceRegistry& registry = context.registry;

        mUploadToUBOTimer.Start();
        {
            nri::Buffer* cameraBuffer = registry.GetBuffer(mCameraBuffer);
            const uint64_t cameraOffset = registry.GetOffsetForFrame(mCameraBuffer, frameIndex);
            void* cameraDst = context.NRI.MapBuffer(*cameraBuffer, cameraOffset, sizeof(RUploadMatrices));
            std::memcpy(cameraDst, &matrices, sizeof(RUploadMatrices));
            context.NRI.UnmapBuffer(*cameraBuffer);
        }
        context.renderData.rdUploadToUBOTime += mUploadToUBOTimer.Stop();

        // ---- World matrix upload (non-animated instances) ----
        mWorldPosMatrices.clear();
        if (context.sceneFrame != nullptr && context.sceneFrame->modelInstData != nullptr)
        {
            for (const auto& modelType : context.sceneFrame->modelInstData->miModelInstancesPerModel)
            {
                if (modelType.second.empty())
                {
                    continue;
                }
                std::shared_ptr<Model> model = modelType.second.front()->GetModel();
                if (model->HasAnimations() && !model->GetBoneList().empty())
                {
                    continue;
                }
                for (const auto& instance : modelType.second)
                {
                    mMatrixGenerateTimer.Start();
                    mWorldPosMatrices.emplace_back(instance->GetWorldTransformMatrix());
                    context.renderData.rdMatrixGenerateTime += mMatrixGenerateTimer.Stop();
                }
            }
        }

        // Budget usage reporting (static portion).
        context.renderData.rdResourceBudgetUsage.staticWorldMatrices = mWorldPosMatrices.size();
        context.renderData.rdResourceBudgetUsage.uploadBytes += mWorldPosMatrices.size() * sizeof(glm::mat4);
        context.renderData.rdMatricesSize +=
                static_cast<unsigned int>(mWorldPosMatrices.size() * sizeof(glm::mat4));

        const uint64_t maxWorldMatrices = context.renderData.rdResourceBudget.maxWorldMatrices;
        if (mWorldPosMatrices.size() > maxWorldMatrices)
        {
            fmt::print(stderr,
                       fg(fmt::color::red),
                       "StaticMeshDrawPass::Upload error: static world matrix upload capacity exceeded "
                       "({}/{})\n",
                       mWorldPosMatrices.size(),
                       maxWorldMatrices);
            return;
        }

        if (!mWorldPosMatrices.empty())
        {
            mUploadToUBOTimer.Start();
            nri::Buffer* worldBuffer = registry.GetBuffer(mWorldMatrixBuffer);
            const uint64_t worldOffset = registry.GetOffsetForFrame(mWorldMatrixBuffer, frameIndex);
            const size_t worldBytes = mWorldPosMatrices.size() * sizeof(glm::mat4);
            void* worldDst = context.NRI.MapBuffer(*worldBuffer, worldOffset, worldBytes);
            std::memcpy(worldDst, mWorldPosMatrices.data(), worldBytes);
            context.NRI.UnmapBuffer(*worldBuffer);
            context.renderData.rdUploadToUBOTime += mUploadToUBOTimer.Stop();
        }
    }

    void StaticMeshDrawPass::Record(CommandContext& context)
    {
        if (context.sceneFrame == nullptr || context.sceneFrame->modelInstData == nullptr)
        {
            return;
        }

        uint32_t worldPosOffset = 0;
        for (const auto& modelType : context.sceneFrame->modelInstData->miModelInstancesPerModel)
        {
            if (modelType.second.empty())
            {
                continue;
            }

            std::shared_ptr<Model> model = modelType.second.front()->GetModel();
            if (model->HasAnimations() && !model->GetBoneList().empty())
            {
                // Animated models are handled by SkinnedMeshDrawPass.
                continue;
            }

            const uint32_t instanceCount = static_cast<uint32_t>(modelType.second.size());

            const auto pickBaseIter = context.sceneFrame->pickBaseByModel.find(modelType.first);
            const uint32_t pickBase = pickBaseIter != context.sceneFrame->pickBaseByModel.end()
                                              ? pickBaseIter->second
                                              : 0;

            PushConstants pushConstants = {};
            pushConstants.modelStride = 0;
            pushConstants.worldPosOffset = static_cast<int>(worldPosOffset);
            pushConstants.pickIDBase = static_cast<int>(pickBase);

            context.NRI.CmdSetPipelineLayout(context.commandBuffer,
                                             nri::BindPoint::GRAPHICS,
                                             *context.renderData.rdMaterialPipelineLayout);
            context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
            context.NRI.CmdSetRootConstants(context.commandBuffer,
                                            {0, &pushConstants, sizeof(pushConstants), 0, nri::BindPoint::GRAPHICS});
            context.NRI.CmdSetDescriptorSet(context.commandBuffer,
                                            {1, mDescriptorSets[context.frameIndex], nri::BindPoint::GRAPHICS});
            model->DrawInstanced(context.renderData, instanceCount);

            worldPosOffset += instanceCount;
        }
    }

    void StaticMeshDrawPass::Cleanup(RRenderData& renderData)
    {
        if (mPipeline != nullptr)
        {
            renderData.NRI.DestroyPipeline(mPipeline);
            mPipeline = nullptr;
        }
        if (renderData.rdMaterialPipelineLayout != nullptr)
        {
            renderData.NRI.DestroyPipelineLayout(renderData.rdMaterialPipelineLayout);
            renderData.rdMaterialPipelineLayout = nullptr;
        }
        mDescriptorSets.clear();
        mTextureRanges.clear();
        mBufferRanges.clear();
    }
} // namespace RAnimation
