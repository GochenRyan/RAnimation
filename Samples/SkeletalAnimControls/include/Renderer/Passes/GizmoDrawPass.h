#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Draws a small X/Y/Z axis gizmo (line list) at the foot of each selected instance, inside the
    // scene MRT pass. Depth-tested but not depth-writing. Writes pick ID 0 to the second target so
    // the gizmo lines are never themselves pickable / outlined.
    class GizmoDrawPass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "GizmoDrawPass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::Scene; }

        bool DeclareResources(ResourceContext& context) override;
        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void Upload(FrameContext& context) override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

    private:
        struct GizmoVertex
        {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 color = glm::vec3(1.0f);
        };

        static constexpr uint32_t kMaxGizmos = 64;
        static constexpr uint32_t kVerticesPerGizmo = 6; // 3 axis lines
        static constexpr uint32_t kMaxVertices = kMaxGizmos * kVerticesPerGizmo;
        static constexpr float kAxisLength = 0.6f;

        nri::PipelineLayout* mPipelineLayout = nullptr;
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mBufferRanges;
        std::vector<nri::DescriptorSet*> mDescriptorSets;

        BufferHandle mCameraBuffer{};
        BufferViewHandle mCameraView{};
        BufferHandle mVertexBuffer{};

        std::vector<GizmoVertex> mVertices;
        uint32_t mVertexCount = 0;
    };
} // namespace RAnimation
