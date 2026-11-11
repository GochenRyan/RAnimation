/* Dear ImGui */
#pragma once

#include <map>
#include <memory>
#include <string>

#include <Model/RenderData.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/InstanceSettings.h>
#include <Model/ModelInstance.h>
#include <Editor/SceneEditor.h>
#include <Editor/AnimGraph/AnimGraphPanel.h>
#include <Editor/BehaviorTree/BehaviorTreePanel.h>
#include <functional>
#include <optional>
#include <Tools/Timer.h>

struct ImGuiIO;

namespace RAnimation
{
    class CharacterSystem;
    class CollisionSystem;
    class BehaviorSystem;

    class UserInterface
    {
    public:
        bool Init(RRenderData& renderData);
        void HideMouse(bool hide);
        // Optional: the "Agent" header shows / drives the selected instance's agent (possession, events).
        void SetCharacterSystem(CharacterSystem* characters)
        {
            mCharacterSystem = characters;
        }
        // Optional: behaviour trees (editor window, agent tree picker). Call after SetCharacterSystem.
        void SetBehaviorSystem(BehaviorSystem* behavior)
        {
            mBehaviorSystem = behavior;
            mBehaviorTreePanel.Initialize(behavior, mCharacterSystem);
        }
        void ShowBehaviorTreeEditor(bool visible) { mBehaviorTreePanel.bVisible = visible; }
        // Test hook: called every frame between the platform backend's NewFrame and ImGui::NewFrame, so injected
        // events are the last ones in the queue for that frame (--editor-smoke).
        std::function<void(ImGuiIO&)> inputInjector;
        BehaviorTreePanel& BehaviorTrees() { return mBehaviorTreePanel; }
        // Checkpoint buttons (§8.4): the application executes them at the end of the frame.
        void SetCheckpointHooks(std::function<void(const std::string&)> save, std::function<void(const std::string&)> load)
        {
            mSaveCheckpoint = std::move(save);
            mLoadCheckpoint = std::move(load);
        }
        // Optional: the "Collision (Jolt)" header drives this system (toggles, threading, telemetry).
        void SetCollisionSystem(CollisionSystem* collision)
        {
            mCollisionSystem = collision;
        }

        void CreateFrame(RRenderData& renderData, SceneEditor& sceneEditor);
        void Render(RRenderData& renderData);

        void Cleanup(RRenderData& renderData);

    private:
        // Builds the top menu bar (Edit menu: undo/redo + mode toggle) and processes editor hotkeys.
        void drawMenuBarAndHotkeys(RRenderData& renderData, SceneEditor& sceneEditor);

        // "Agent" header: the selected instance's agent (brain, schedule, feedback, possession), checkpoints.
        void drawCharacterPanel(ModelAndInstanceData& modInstData);
        // Instances panel: the selected instance's agent config (tree, tick rate, perception), undoable.
        void drawAgentConfig(SceneEditor& sceneEditor, const std::shared_ptr<ModelInstance>& instance);
        // Edit -> View is refused while the behaviour tree editor has unsaved / invalid changes (D17).
        void requestModeToggle(SceneEditor& sceneEditor);

        // "Collision (Jolt)" header: enable / push / debug toggles, thread mode + threshold, per-frame timings.
        void drawCollisionPanel(ModelAndInstanceData& modInstData);

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

        // AnimGraph editor window + the graph assets picked from the Animation header (keyed by path).
        AnimGraphPanel mAnimGraphPanel{};
        std::map<std::string, std::shared_ptr<AnimGraphAsset>> mGraphAssetCache;

        CharacterSystem* mCharacterSystem = nullptr;
        BehaviorSystem* mBehaviorSystem = nullptr;
        BehaviorTreePanel mBehaviorTreePanel{};
        std::function<void(const std::string&)> mSaveCheckpoint;
        std::function<void(const std::string&)> mLoadCheckpoint;
        char mCheckpointSlot[256] = ASSETS_SRC_DIR "/SkeletalAnimBehavior/saves/quick";
        std::string mModeStatus;
        std::optional<AgentConfig> mPendingAgentBefore;
        std::shared_ptr<ModelInstance> mPendingAgentInstance;
        CollisionSystem* mCollisionSystem = nullptr;
        std::string mCharacterStatus;

        // Crowd spawn (tier C demo): N x M grid of the selected model with random clip / phase / yaw.
        int mCrowdCols = 20;
        int mCrowdRows = 20;
        float mCrowdSpacing = 1.5f;

        // UI timing (moved out of the Renderer); written into RRenderData for the stats panel.
        Timer mUIGenerateTimer{};
        Timer mUIDrawTimer{};
    };
} // namespace RAnimation