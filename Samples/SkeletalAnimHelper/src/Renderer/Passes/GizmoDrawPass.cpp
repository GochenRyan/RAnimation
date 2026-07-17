#include <Renderer/Passes/GizmoDrawPass.h>

#include <cstddef>
#include <cstring>

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
    bool GizmoDrawPass::DeclareResources(ResourceContext& context)
    {
        mCameraBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kCameraBuffer,
                                                              SceneBufferDescs::Camera());
        mCameraView = context.registry.RegisterSharedView(SceneResourceNames::kCameraBufferView,
                                                          mCameraBuffer,
                                                          nri::BufferViewType::CONSTANT);

        BufferDesc vertexDesc = {};
        vertexDesc.name = "GizmoVertexBuffer";
        vertexDesc.elementSize = sizeof(GizmoVertex);
        vertexDesc.elementCount = kMaxVertices;
        vertexDesc.structureStride = 0;
        vertexDesc.usage = nri::BufferUsageBits::VERTEX_BUFFER;
        vertexDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
        vertexDesc.perQueuedFrame = true;
        mVertexBuffer = context.registry.RegisterBuffer(vertexDesc);

        return mCameraBuffer.IsValid() && mCameraView.IsValid() && mVertexBuffer.IsValid();
    }

    bool GizmoDrawPass::CreatePipeline(RenderContext& context)
    {
        mBufferRanges = {
                {0, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::VERTEX_SHADER},
        };

        nri::DescriptorSetDesc setDescs[] = {
                {0, mBufferRanges.data(), static_cast<uint32_t>(mBufferRanges.size())},
        };

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.descriptorSetNum = helper::GetCountOf(setDescs);
        layoutDesc.descriptorSets = setDescs;
        layoutDesc.shaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

        NRI_ABORT_ON_FAILURE(context.NRI.CreatePipelineLayout(context.device, layoutDesc, mPipelineLayout));

        nri::VertexStreamDesc vertexStreamDesc = {};
        vertexStreamDesc.bindingSlot = 0;

        nri::VertexAttributeDesc vertexAttributeDesc[2] = {};
        vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
        vertexAttributeDesc[0].offset = offsetof(GizmoVertex, position);
        vertexAttributeDesc[0].d3d = {"POSITION", 0};
        vertexAttributeDesc[0].vk = {0};
        vertexAttributeDesc[1].format = nri::Format::RGB32_SFLOAT;
        vertexAttributeDesc[1].offset = offsetof(GizmoVertex, color);
        vertexAttributeDesc[1].d3d = {"COLOR", 0};
        vertexAttributeDesc[1].vk = {1};

        nri::VertexInputDesc vertexInputDesc = {};
        vertexInputDesc.attributes = vertexAttributeDesc;
        vertexInputDesc.attributeNum = static_cast<uint8_t>(helper::GetCountOf(vertexAttributeDesc));
        vertexInputDesc.streams = &vertexStreamDesc;
        vertexInputDesc.streamNum = 1;

        nri::InputAssemblyDesc inputAssemblyDesc = {};
        inputAssemblyDesc.topology = nri::Topology::LINE_LIST;

        nri::RasterizationDesc rasterizationDesc = {};
        rasterizationDesc.fillMode = nri::FillMode::SOLID;
        rasterizationDesc.cullMode = nri::CullMode::NONE;
        rasterizationDesc.frontCounterClockwise = true;

        nri::MultisampleDesc multisampleDesc = {};
        multisampleDesc.sampleNum = 1;

        nri::ColorAttachmentDesc colorAttachmentDescs[2] = {};
        colorAttachmentDescs[0].format = context.swapChainFormat;
        colorAttachmentDescs[0].colorWriteMask = nri::ColorWriteBits::RGBA;
        colorAttachmentDescs[0].blendEnabled = false;
        colorAttachmentDescs[1].format = nri::Format::R32_UINT;
        colorAttachmentDescs[1].colorWriteMask = nri::ColorWriteBits::R;
        colorAttachmentDescs[1].blendEnabled = false;

        // Depth-tested but not depth-writing, so the gizmo is occluded by geometry in front of it.
        nri::DepthAttachmentDesc depthAttachmentDesc = {};
        depthAttachmentDesc.compareOp = nri::CompareOp::GREATER_EQUAL; // reversed-Z
        depthAttachmentDesc.write = false;
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
                                  SHADER_SRC_DIR "/SkeletalAnimHelper/gizmo.vs",
                                  shaderCodeStorage),
                utils::LoadShader(deviceDesc.graphicsAPI,
                                  SHADER_SRC_DIR "/SkeletalAnimHelper/gizmo.fs",
                                  shaderCodeStorage),
        };

        nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        graphicsPipelineDesc.pipelineLayout = mPipelineLayout;
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

    DescriptorPoolRequirements GizmoDrawPass::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        DescriptorPoolRequirements req = {};
        req.descriptorSetMaxNum = queuedFrameNum;
        req.constantBufferMaxNum = queuedFrameNum;
        return req;
    }

    bool GizmoDrawPass::CreateDescriptors(FrameContext& context)
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

            nri::Descriptor* cameraView = context.registry.GetView(mCameraView, frameIndex);
            nri::Descriptor* descriptors[] = {cameraView};
            nri::UpdateDescriptorRangeDesc range = {mDescriptorSets[frameIndex], 0, 0, descriptors, 1};
            context.NRI.UpdateDescriptorRanges(&range, 1);
        }
        return true;
    }

    void GizmoDrawPass::Upload(FrameContext& context)
    {
        mVertices.clear();
        mVertexCount = 0;

        if (context.sceneFrame == nullptr || context.sceneFrame->modelInstData == nullptr)
        {
            return;
        }

        const ModelAndInstanceData& modelInstData = *context.sceneFrame->modelInstData;
        if (modelInstData.miModelInstances.empty())
        {
            return;
        }

        // Current selection set (single instance today; structured to support multiple).
        const int selectedIndex = modelInstData.miSelectedInstance;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(modelInstData.miModelInstances.size()))
        {
            return;
        }

        const std::shared_ptr<ModelInstance>& instance = modelInstData.miModelInstances[selectedIndex];
        if (instance == nullptr)
        {
            return;
        }

        // Draw the instance's local coordinate frame: origin and axes come from the instance's world
        // transform (the same matrix the mesh is rendered with), so the gizmo rotates/orients with the
        // instance instead of staying world-aligned. Basis vectors are normalized so the gizmo keeps a
        // constant on-screen length regardless of the instance scale.
        const glm::mat4 worldTransform = instance->GetWorldTransformMatrix();
        const glm::vec3 origin = glm::vec3(worldTransform[3]);
        const glm::vec3 axisColors[3] = {
                {1.0f, 0.15f, 0.15f}, // X red
                {0.15f, 1.0f, 0.15f}, // Y green
                {0.2f, 0.4f, 1.0f},   // Z blue
        };
        const glm::vec3 worldFallback[3] = {
                {1.0f, 0.0f, 0.0f},
                {0.0f, 1.0f, 0.0f},
                {0.0f, 0.0f, 1.0f},
        };

        for (int axis = 0; axis < 3; ++axis)
        {
            if (mVertices.size() + 2 > kMaxVertices)
            {
                break;
            }
            const glm::vec3 basis = glm::vec3(worldTransform[axis]);
            const float length = glm::length(basis);
            const glm::vec3 direction = length > 1e-6f ? basis / length : worldFallback[axis];
            mVertices.push_back({origin, axisColors[axis]});
            mVertices.push_back({origin + direction * kAxisLength, axisColors[axis]});
        }

        mVertexCount = static_cast<uint32_t>(mVertices.size());
        if (mVertexCount == 0)
        {
            return;
        }

        const uint32_t frameIndex = context.renderData.queuedFrameIndex;
        nri::Buffer* vertexBuffer = context.registry.GetBuffer(mVertexBuffer);
        const uint64_t offset = context.registry.GetOffsetForFrame(mVertexBuffer, frameIndex);
        const size_t bytes = mVertices.size() * sizeof(GizmoVertex);
        void* dst = context.NRI.MapBuffer(*vertexBuffer, offset, bytes);
        std::memcpy(dst, mVertices.data(), bytes);
        context.NRI.UnmapBuffer(*vertexBuffer);
    }

    void GizmoDrawPass::Record(CommandContext& context)
    {
        if (mVertexCount == 0)
        {
            return;
        }

        nri::Buffer* vertexBuffer = context.registry.GetBuffer(mVertexBuffer);
        const uint64_t offset = context.registry.GetOffsetForFrame(mVertexBuffer, context.frameIndex);

        nri::VertexBufferDesc vertexBufferDesc = {};
        vertexBufferDesc.buffer = vertexBuffer;
        vertexBufferDesc.offset = offset;
        vertexBufferDesc.stride = sizeof(GizmoVertex);

        context.NRI.CmdSetPipelineLayout(context.commandBuffer, nri::BindPoint::GRAPHICS, *mPipelineLayout);
        context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
        context.NRI.CmdSetDescriptorSet(context.commandBuffer,
                                        {0, mDescriptorSets[context.frameIndex], nri::BindPoint::GRAPHICS});
        context.NRI.CmdSetVertexBuffers(context.commandBuffer, 0, &vertexBufferDesc, 1);
        context.NRI.CmdDraw(context.commandBuffer, {mVertexCount, 1, 0, 0});
    }

    void GizmoDrawPass::Cleanup(RRenderData& renderData)
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
        mBufferRanges.clear();
        mVertices.clear();
        mVertexCount = 0;
    }
} // namespace RAnimation
