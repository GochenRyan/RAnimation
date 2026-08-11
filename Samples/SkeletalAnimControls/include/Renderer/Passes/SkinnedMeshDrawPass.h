#pragma once

#include <vector>

#include <Renderer/IRenderPass.h>

namespace RAnimation
{
    // Uses the shared material pipeline layout owned by StaticMeshDrawPass; only owns its own pipeline + descriptor sets.
    // Must run after StaticMeshDrawPass during Init() so rdMaterialPipelineLayout is non-null.
    class SkinnedMeshDrawPass : public IRenderPass
    {
    public:
        const char* GetName() const override { return "SkinnedMeshDrawPass"; }
        RenderPassPhase GetPhase() const override { return RenderPassPhase::Scene; }

        bool DeclareResources(ResourceContext& context) override;
        bool CreatePipeline(RenderContext& context) override;
        DescriptorPoolRequirements GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const override;
        bool CreateDescriptors(FrameContext& context) override;
        void DeclareAccess(RegistryAccessBuilder& builder) const override;
        void Record(CommandContext& context) override;
        void Cleanup(RRenderData& renderData) override;

    private:
        nri::Pipeline* mPipeline = nullptr;
        std::vector<nri::DescriptorSet*> mDescriptorSets;

        BufferHandle mCameraBuffer{};
        BufferHandle mBoneMatrixBuffer{};
        BufferViewHandle mCameraView{};
        BufferViewHandle mBoneMatrixView{};
    };
} // namespace RAnimation
