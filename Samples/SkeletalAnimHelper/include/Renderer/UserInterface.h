/* Dear ImGui */
#pragma once

#include <Model/RenderData.h>
#include <Model/ModelAndInstanceData.h>

namespace RAnimation
{
    class UserInterface
    {
    public:
        bool Init(RRenderData& renderData);
        void HideMouse(bool hide);

        void CreateFrame(RRenderData& renderData, ModelAndInstanceData& modInstData);
        void Render(RRenderData& renderData);

        void Cleanup(RRenderData& renderData);

    private:
        float mFramesPerSecond = 0.0f;
        /* averaging speed */
        float mAveragingAlpha = 0.96f;

        std::vector<float> mFPSValues{};
        int mNumFPSValues = 90;

        std::vector<float> mFrameTimeValues{};
        int mNumFrameTimeValues = 90;

        std::vector<float> mModelUploadValues{};
        int mNumModelUploadValues = 90;

        std::vector<float> mMatrixGenerationValues{};
        int mNumMatrixGenerationValues = 90;

        std::vector<float> mMatrixUploadValues{};
        int mNumMatrixUploadValues = 90;

        std::vector<float> mUiGenValues{};
        int mNumUiGenValues = 90;

        std::vector<float> mUiDrawValues{};
        int mNumUiDrawValues = 90;

        float mNewFps = 0.0f;
        double mUpdateTime = 0.0;

        int mFpsOffset = 0;
        int mFrameTimeOffset = 0;
        int mModelUploadOffset = 0;
        int mMatrixGenOffset = 0;
        int mMatrixUploadOffset = 0;
        int mUiGenOffset = 0;
        int mUiDrawOffset = 0;

        int mManyInstanceCreateNum = 1;
    };
} // namespace RAnimation