#pragma once

#include <string>

namespace nri
{
    struct PipelineLayout;
    struct Pipeline;
}

namespace RAnimation
{
    struct RRenderData;

    class SkinningPipeline
    {
    public:
        static bool Init(RRenderData& renderData, nri::PipelineLayout& pipelineLayout, nri::Pipeline* pipeline, std::string vertexShaderFilename, std::string fragmentShaderFilename);
        static void Cleanup(RRenderData& renderData, nri::Pipeline* pipeline);
    };
} // namespace RAnimation
