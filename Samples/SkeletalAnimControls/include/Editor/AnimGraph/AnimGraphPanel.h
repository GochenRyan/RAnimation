#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <AnimGraph/AnimGraphAsset.h>
#include <AnimGraph/ExecPlan.h>
#include <Editor/AnimGraph/EditorGraph.h>

namespace ax::NodeEditor
{
    struct EditorContext;
}

namespace RAnimation
{
    class SceneEditor;
    class ModelInstance;

    // AnimGraph editor window (imgui-node-editor).
    // Toolbar: Validate | Save | Save As | Load | preset picker | Add Node | GPU overlay | Apply to selected instance.
    // Node cards are tagged [GPU]/[CPU] from the ExecPlan and seam input pins show their upload slot.
    // Every edit goes through the SceneEditor's shared undo stack and is only allowed in Edit mode.
    class AnimGraphPanel
    {
    public:
        void Initialize(); // creates the node-editor context
        void Shutdown();
        // Per frame: pumps the file dialogs (outside the window), then draws the editor window.
        void Draw(SceneEditor& sceneEditor);
        void ReloadPresetList();
        // Call after the graph was replaced from outside: node positions are re-synced next frame.
        void NotifyGraphReplaced() { bPositionsRestored = false; }
        // Recomputes the CPU/GPU split used for node colouring and seam labels.
        void RefreshGpuPlan();

        // Loads a graph file into the panel as the current asset. Returns the asset or null.
        std::shared_ptr<AnimGraphAsset> LoadGraphFile(const std::string& path);
        std::shared_ptr<AnimGraphAsset> CurrentAsset() const { return mAsset; }
        // Presets are grouped by skeleton family: one sub directory of PresetDir per asset folder
        // (Model::GetAssetFamily), e.g. animgraph/animated_woman/*.animgraph.usda.
        const std::vector<std::string>& Families();
        const std::vector<std::string>& PresetFiles(const std::string& family);
        // The family the toolbar picker browses. It follows the selected instance's model until the
        // user picks another one in the Family combo (and snaps back when a different model is selected).
        const std::string& Family() const { return mFamily; }
        void FollowModelFamily(const std::string& family);

        bool bVisible = true;
        std::string FilePath;  // current save path (toolbar Save)
        std::string PresetDir; // preset root directory (one sub directory per skeleton family)

    private:
        void DrawToolbar(SceneEditor& sceneEditor);
        void DrawNodes(SceneEditor& sceneEditor, ModelInstance* selected);
        void DrawLinks();
        void HandleCreation(SceneEditor& sceneEditor, bool editMode);
        void HandleDeletion(SceneEditor& sceneEditor, bool editMode);
        void HandleDialogs(SceneEditor& sceneEditor);
        void DrawPresetPicker(SceneEditor& sceneEditor, bool editMode);
        std::string FamilyDir() const; // PresetDir/<family>

        // Edit plumbing: snapshot -> mutate -> commit (undoable) / live sync while dragging a slider.
        void BeginEdit();
        void CommitEdit(SceneEditor& sceneEditor, const char* name);
        void SyncAsset();
        void ReplaceGraph(std::shared_ptr<AnimGraphAsset> asset);
        bool SaveTo(const std::string& path);

        EditorGraph mGraph;
        EditorGraph mEditBefore;
        std::shared_ptr<AnimGraphAsset> mAsset;
        ax::NodeEditor::EditorContext* mContext = nullptr;

        std::vector<std::string> mFamilies;                        // sub directory names of PresetDir
        std::map<std::string, std::vector<std::string>> mPresetFiles; // family -> full paths
        bool bPresetListLoaded = false;
        std::string mFamily;              // family the picker browses
        std::string mFollowedModelFamily; // last family handed to FollowModelFamily
        bool bFamilyPinned = false;       // the user picked a family by hand
        std::string mPresetDoc; // doc string of the current preset

        std::string mStatusMessage; // e.g. why a link was rejected
        bool mStatusIsError = false;
        bool bPositionsRestored = false;
        int mPendingPlacement = -1;
        int mAddNodeType = 1;
        bool bShowGpuOverlay = true;

        ExecPlan mExec;                        // the split: which node runs where, which edges upload
        std::map<int, NodeSide> mSideByNodeId; // editor node id -> side (CpuOnly markers are Cpu)
        std::map<int, int> mSeamSlotOf;        // editor link id -> upload slot
        std::string mPlanSummary;
    };
} // namespace RAnimation
