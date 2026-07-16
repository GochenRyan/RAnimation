/* Dear ImGui */
#pragma once

#include <memory>

#include <Model/RenderData.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/InstanceSettings.h>
#include <Model/ModelInstance.h>
#include <Editor/SceneEditor.h>
#include <Tools/Timer.h>

namespace RAnimation
{
    class UserInterface
    {
    public:
        bool Init(RRenderData& renderData);
        void HideMouse(bool hide);

        void CreateFrame(RRenderData& renderData, SceneEditor& sceneEditor);
        void Render(RRenderData& renderData);

        void Cleanup(RRenderData& renderData);

    private:
        // Builds the top menu bar (Edit menu: undo/redo + mode toggle) and processes editor hotkeys.
        void drawMenuBarAndHotkeys(RRenderData& renderData, SceneEditor& sceneEditor);

        // Helper invoked right after a slider widget: coalesces a drag into one settings-edit command.
        // `edited` is the live working copy (the value being applied this frame), used as the "after".
        void trackSettingsEdit(SceneEditor& sceneEditor,
                               const std::shared_ptr<ModelInstance>& instance,
                               const InstanceSettings& edited,
                               const char* editName);

    private:
        // Pending instance-settings edit: snapshot captured when a slider/combo became active, used to
        // form the command's "before" state once editing finishes. Only one widget edits at a time.
        InstanceSettings mPendingSettingsBefore{};
        std::shared_ptr<ModelInstance> mPendingSettingsInstance = nullptr;

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

        // UI timing (moved out of the Renderer); written into RRenderData for the stats panel.
        Timer mUIGenerateTimer{};
        Timer mUIDrawTimer{};
    };
} // namespace RAnimation