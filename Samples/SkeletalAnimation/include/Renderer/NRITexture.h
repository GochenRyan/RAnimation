#pragma once


namespace RAnimation
{
    struct RRenderData;
    struct RTextureData;

    class NRITexture
    {
    public:
        static bool LoadTexture(RRenderData& renderData, RTextureData& texData);
        static bool UploadToGPU(RRenderData& renderData, RTextureData& texData);
        static void Cleanup(RRenderData& renderData, RTextureData& texData);
    };
} // namespace RAnimation