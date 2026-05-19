#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <Renderer/IRenderPass.h>
#include <Tools/Timer.h>

namespace RAnimation
{
    // Owns the shared "material pipeline layout" (set 0 = texture+sampler, set 1 = camera+world buffer).
    // The shared layout is also used by SkinnedMeshDrawPass (via RRenderData::rdMaterialPipelineLayout)
    // and by NRITexture for material descriptor set allocation.
    class StaticMeshDrawPass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "StaticMeshDrawPass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::Scene; }

        bool DeclareResources(ResourceContext& context) override;
        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void Upload(FrameContext& context) override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

    private:
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorRangeDesc> mTextureRanges;
        std::vector<nri::DescriptorRangeDesc> mBufferRanges;
        std::vector<nri::DescriptorSet*> mDescriptorSets;

        BufferHandle mCameraBuffer{};
        BufferHandle mWorldMatrixBuffer{};
        BufferViewHandle mCameraView{};
        BufferViewHandle mWorldMatrixView{};

        // Per-frame CPU-side scratch for non-animated instance world matrices.
        std::vector<glm::mat4> mWorldPosMatrices;

        Timer mMatrixGenerateTimer{};
        Timer mUploadToUBOTimer{};
    };
} // namespace RAnimation
