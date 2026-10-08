#pragma once

#include <string>

#include <NRI.h>

namespace RAnimation
{
    struct RRenderData;
    class Model;

    // GPU side of a model's baked TRS texture: the RGBA16F Texture2D, its clip table and the per-model
    // descriptor set (set 1 of the hybrid_eval pipeline layout). Created once at model load, destroyed with
    // the model. The descriptor set comes from the pre-allocated free list on RRenderData (NRI cannot free
    // individual sets) and is returned on release.
    struct TrsTextureGpu
    {
        nri::Texture* texture = nullptr;
        nri::Memory* textureMemory = nullptr;
        nri::Descriptor* textureView = nullptr;
        nri::Buffer* clipTable = nullptr;
        nri::Memory* clipTableMemory = nullptr;
        nri::Descriptor* clipTableView = nullptr;
        nri::DescriptorSet* set1 = nullptr;

        bool IsReady() const { return set1 != nullptr; }
    };

    class NRITrsTexture
    {
    public:
        // Uploads model.GetTrsTexCpu() (must be ready) and fills model.TrsTexGpu(). On failure the model's
        // TRS status string explains why and the GPU tiers fall back to the CPU tier for this model.
        static bool Load(RRenderData& renderData, Model& model);
        static void Release(RRenderData& renderData, TrsTextureGpu& gpu);
    };
} // namespace RAnimation
