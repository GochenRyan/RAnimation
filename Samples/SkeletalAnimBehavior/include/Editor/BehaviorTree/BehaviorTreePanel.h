#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <Behavior/BehaviorTreeAsset.h>
#include <Model/EntityId.h>

namespace ax::NodeEditor
{
    struct EditorContext;
}

namespace RAnimation
{
    class SceneEditor;
    class BehaviorSystem;
    class CharacterSystem;
    class BehaviorTreeBrain;
    struct Agent;

    // Behaviour tree editor window (§9, imgui-node-editor). Top-down tree: input pin on top, output pin at the
    // bottom (composites: any number of links, decorators: one, leaves: none). Children are ordered by x (D17),
    // re-sorted when a drag ends or a link is made, as an undoable command; a badge shows each child's index.
    //   Edit mode: structure, parameters (literal or "@key" binding), node keys, tree keys and the global schema
    //              are editable; Save refuses a tree that fails validation (§9.2), the error list focuses nodes.
    //   View mode: read only; the tree of the selected agent is shown live: node status colours fading with the
    //              time since the node was ticked, the running path highlighted, live key values, the tick log.
    // Edit -> View (CanLeaveEdit / OnEnteredView): modified trees must be saved and valid; the agents running a
    // modified tree start over from the root (D17) and its TreeShared store is rebuilt (13.1 #4).
    class BehaviorTreePanel
    {
    public:
        void Initialize(BehaviorSystem* behavior, CharacterSystem* characters);
        void Shutdown();
        void Draw(SceneEditor& sceneEditor);

        // Edit -> View gate and follow-up.
        bool CanLeaveEdit(std::string& outReason) const;
        void OnEnteredView();

        bool LoadTree(const std::string& relativePath);
        const std::string& CurrentPath() const { return mPath; }
        const std::vector<ValidationError>& Errors() const { return mErrors; }
        std::vector<std::string> TreeFiles() const; // behavior/*.behaviortree.usda (relative)

        bool bVisible = false;

        // read-only state and last frame's screen positions (the --editor-smoke input test drives the real
        // widgets with injected mouse / key events and checks the outcome through these)
        const TreeDesc& Desc() const { return mDesc; }
        bool Dirty() const { return mDirty; }
        uint32_t SelectedNode() const { return mSelectedNode; }
        const std::string& Status() const { return mStatus; }
        EntityId LiveEntity() const { return mLiveEntity; }
        bool ScreenPos(const std::string& what, float& x, float& y) const; // "in:<id>", "out:<id>", "title:<id>", "Save", "Add node", "error0"
        void BringToFront() { mFocusWindow = 3; }
        void FrameAll() { mNavigateFrames = 2; }

    private:
        struct Snapshot
        {
            TreeDesc desc;
            BlackboardSchema global;
        };

        void DrawToolbar(SceneEditor& sceneEditor, bool editMode);
        void DrawCanvas(SceneEditor& sceneEditor, bool editMode, const BehaviorTreeBrain* live);
        void DrawInspector(SceneEditor& sceneEditor, bool editMode, const BehaviorTreeBrain* live, const Agent* agent);
        void DrawBlackboard(SceneEditor& sceneEditor, bool editMode, const BehaviorTreeBrain* live, const Agent* agent);
        void DrawErrors();
        void DrawTickLog(const BehaviorTreeBrain* live);
        void HandleCreation(SceneEditor& sceneEditor, bool editMode);
        void HandleDeletion(SceneEditor& sceneEditor, bool editMode);
        void SortChildrenByX(SceneEditor& sceneEditor, bool editMode);

        // edits: snapshot -> mutate mDesc -> commit as one undoable command
        void Begin();
        void Commit(SceneEditor& sceneEditor, const char* name);
        void Apply(const Snapshot& s);
        void Validate();
        bool Save(std::string& outError);
        uint32_t AddNode(const std::string& type, float x, float y);
        BtNodeDesc* ParentOf(uint32_t id);
        bool IsAncestor(uint32_t ancestor, uint32_t node) const;

        BehaviorSystem* mBehavior = nullptr;
        CharacterSystem* mCharacters = nullptr;
        ax::NodeEditor::EditorContext* mContext = nullptr;

        std::string mPath; // relative to the behaviour root
        TreeDesc mDesc;
        uint64_t mSavedHash = 0;    // content hash of what is on disk
        bool mDirty = false;        // differs from the file (positions included)
        Snapshot mBefore;
        std::vector<ValidationError> mErrors;
        std::set<std::string> mModifiedThisEdit; // saved while in Edit mode -> applied on View
        bool mGlobalModified = false;

        uint32_t mSelectedNode = ~0u;
        bool mPositionsDirty = true; // push mDesc positions into the editor next frame
        uint32_t mFocusNode = ~0u;
        int mAddType = 0;
        bool bFollowSelectedAgent = true;
        EntityId mLiveEntity = kInvalidEntityId;
        std::string mStatus;
        bool mStatusIsError = false;
        char mNewTreeName[64] = "new_tree";
        char mNewKeyName[64] = "myKey";
        int mNewKeyType = 2;
        int mNewKeyScope = 2;
        std::map<std::string, std::pair<float, float>> mScreen; // see ScreenPos
        int mNavigateFrames = 0;                                 // frame the whole tree a few frames after a load
        int mFocusWindow = 3;                                    // frames left to bring the window to the front (other
                                                                 // windows created in the first frames would cover it)
        bool mWasVisible = false;
        void Remember(const std::string& what);                  // centre of the last ImGui item
    };
} // namespace RAnimation
