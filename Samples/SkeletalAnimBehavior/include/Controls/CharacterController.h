#pragma once
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Agent/CharacterSignals.h>
#include <Agent/ExecutionFeedback.h>
#include <Controls/CharacterProfile.h>
#include <StateMachine/SMManager.h>
#include <StateMachine/StateMachineDefinition.h>
#include <StateMachine/StateTypeRegistry.h>
#include <Tools/SaveTree.h>

struct ModelAndInstanceData;

// The controllable character: gameplay state + a hierarchical state machine + one resident AnimGraph.
//
//   Root (Parallel)
//    |- Locomotion (Single)  Stand / Crouch (blend space rings), Sprint {Enter, Loop, Exit}, Turn,
//    |                       Airborne {Start, Loop, Land} entered on the motion state (not an event), Dead
//    |- Action (Single)      Free / UpperBodyBusy / FullBodyBusy: montage requests by name (event "Action")
//    '- Weapon (Single)      Unarmed / Pistol: upper body layer on / off (event "ToggleWeapon")
//
// Data flows one way through a small contract, and the state machine owns none of the gameplay state:
//   CharacterInput  (gameplay / keys)      -> guards read it
//   MotionState     (CharacterController::Move, per frame) -> guards read it; the controller integrates
//                   movement, jumps and gravity itself
//   GraphFeedback   (what the graph reports: selector done, slots idle) -> IsDone / guards read it
//   AnimParams      (written by the states) -> ApplyVisuals writes named graph parameters and montage
//                   requests; the graph never changes, only its parameters
// Everything model-specific (clips, speeds, montage table, which graph) comes from CharacterProfile; the
// state machine definition only knows input names of the "LocoSelect" CrossfadeSelect and montage names.

namespace RAnimation
{
    struct AnimGraphAsset;
    class AnimGraphInstance;
    class ModelInstance;
    class Model;
    class CrossfadeSelect;
    class BlendSpace2D;
    class BlendSpace1D;
    class LayeredBlend;
    class AnimSlot;

    // CharacterInput and MotionState: Agent/CharacterSignals.h (shared with the intent / decision layers).

    // "Action" event payload: the montage plus the request id the controller reports its status under.
    struct ActionPayload
    {
        std::string montage;
        RequestId request = 0;
    };

    // What the resident graph reports back before the state machine updates.
    struct GraphFeedback
    {
        bool locoDone = false;       // CrossfadeSelect "LocoSelect": every one-shot clip of the active input ended
        bool locoFading = false;
        bool upperBodyIdle = true;   // AnimSlot "UpperBody" has nothing playing
        bool fullBodyIdle = true;    // AnimSlot "FullBody"
    };

    struct SlotRequest
    {
        std::string slot;
        std::string montage;
    };

    // State machine -> AnimGraph. Written by the states, consumed by ApplyVisuals.
    struct AnimParams
    {
        std::string locoSelect = "stand"; // CrossfadeSelect input name
        bool locoRestart = false;         // rewind the branch when switching (one-shot clips)
        float moveSpeed = 0.0f;           // m/s the state wants to move at (0 = stand still)
        float upperBodyAlphaTarget = 0.0f;
        std::vector<SlotRequest> slotRequests; // consumed (cleared) by ApplyVisuals
        std::string currentMontage[2];    // [0] UpperBody, [1] FullBody: what the busy states hold
        std::string pendingMontage;       // parked by the RememberMontage transition action for OnEnter
        bool movementLock = false;        // the busy state's montage locks movement
        bool dead = false;

        // Request bookkeeping (not saved: a load empties the request table, D15).
        RequestId pendingRequest = 0;                          // parked with pendingMontage
        RequestId currentRequest[2] = {0, 0};                  // what each busy state is playing
        std::vector<std::pair<RequestId, bool>> endedRequests; // busy state left: (id, completed normally)
        std::vector<std::string> stopSlots;                    // CancelAction: fade these slots out (ApplyVisuals)
    };

    // Resources every controller of one definition shares (CharacterSystem creates one set for all agents):
    // the type registry, the finalized definition, and the profile / graph caches.
    struct CharacterShared
    {
        StateTypeRegistry registry;
        std::shared_ptr<StateMachineDefinition> definition;
        std::string source; // definition file or "(built-in)"
        std::string doc;
        std::string assetsDir;
        std::map<std::string, std::shared_ptr<AnimGraphAsset>> graphCache;   // by path
        std::map<std::string, std::shared_ptr<CharacterProfile>> profileCache; // by path
    };

    // What the states see (the SMManager user data). Inputs are read-only for them, AnimParams theirs.
    struct CharacterStateData
    {
        const CharacterInput* input = nullptr;
        const MotionState* motion = nullptr;
        const GraphFeedback* graph = nullptr;
        const CharacterProfile* profile = nullptr;
        AnimParams* anim = nullptr;
        // Ids resolved once (CharacterController::BindStateData) so guards never look states up by name.
        StateId airborneState = kInvalidStateId;
        StateId deadState = kInvalidStateId;
    };

    class CharacterController
    {
    public:
        static constexpr const char* kDefinitionRelPath = "statemachine/character.statemachine.usda";
        static constexpr const char* kLocoSelectNodeName = "LocoSelect";
        static constexpr const char* kStandNodeName = "Stand";
        static constexpr const char* kCrouchNodeName = "Crouch";
        static constexpr const char* kAimNodeName = "Aim";
        static constexpr const char* kUpperBodyLayerNodeName = "UpperBodyLayer";
        static constexpr const char* kUpperBodySlot = "UpperBody";
        static constexpr const char* kFullBodySlot = "FullBody";
        static constexpr float kCancelBlendOutSec = 0.2f;

        // Registers the character state types / guards and loads the definition from assetsDir.
        bool Init(const std::string& assetsDir, std::string& outError);
        // Same, as a shareable set (one per CharacterSystem); InitShared makes this controller use it.
        static std::shared_ptr<CharacterShared> CreateShared(const std::string& assetsDir, std::string& outError);
        void InitShared(std::shared_ptr<CharacterShared> shared) { mShared = std::move(shared); }
        const std::shared_ptr<CharacterShared>& Shared() const { return mShared; }

        static void RegisterCharacterTypes(StateTypeRegistry& registry);
        static std::shared_ptr<StateMachineDefinition> BuildDefaultDefinition(const StateTypeRegistry& registry,
                                                                              std::string& outError);
        static void BindStateData(const StateMachineDefinition& definition, CharacterStateData& data);
        bool SaveDefinition(const std::string& path, std::string& outError) const;

        // Attach loads the instance's model profile (<Stem>.profile.json next to the asset) and its graph.
        bool Attach(std::shared_ptr<ModelInstance> instance, std::string& outError);
        void Detach();
        bool IsAttached() const { return mInstance != nullptr; }
        ModelInstance* Target() const { return mInstance.get(); }

        void SetInput(const CharacterInput& input);
        void Jump();                                     // vertical impulse when grounded (the machine follows)
        // Collision depenetration (CollisionSystem::Resolve, after Update): shifts the gameplay position and the
        // instance together so the push is visible this frame and persists into next frame's Move.
        void ApplyPush(const glm::vec3& delta);
        // Event "Action" with the montage name. With a request id the outcome is tracked in the request table:
        // false here -> Rejected; otherwise Pending until the next Update decides Accepted / Rejected.
        bool RequestAction(const std::string& montage, RequestId request = 0);
        // Event "CancelAction": the busy state fades its slot out and returns to Free when the montage is
        // cancelable (profile, default true); the request then ends Cancelled. 0 = whatever is playing.
        void CancelAction(RequestId request = 0);
        void ToggleWeapon();
        void Die();

        // Per frame, before AnimationSystem::Update: Move -> feedback -> state machine -> ApplyVisuals.
        void Update(float deltaTime, ModelAndInstanceData& scene);

        // Motion / armed / dead and the request table; moves the status changes since the last call into out.
        void BuildFeedback(ExecutionFeedback& out);
        bool Armed() const;
        bool Dead() const { return mAnim.dead; }

        // Checkpoint support (§8.4): Motion, AnimParams (no slot requests), eased values and the state machine
        // runtime. Loading restores the states without OnEnter and empties the request table.
        void SaveRuntime(SaveNode& out) const;
        bool LoadRuntime(const SaveNode& in, std::string& outError);

        SMManager* Manager() const { return mManager.get(); }
        const std::string& DefinitionSource() const;
        const std::string& DefinitionDoc() const;
        const CharacterProfile* Profile() const { return mProfile.get(); }
        const MotionState& Motion() const { return mMotion; }
        const AnimParams& Anim() const { return mAnim; }
        const GraphFeedback& Feedback() const { return mFeedback; }
        std::string ActiveStatesText() const;
        const std::deque<std::string>& Log() const { return mLog; }
        const std::deque<std::string>& Notifies() const { return mNotifies; }

    private:
        struct GraphNodes
        {
            AnimGraphInstance* graph = nullptr;
            uint64_t revision = 0;
            CrossfadeSelect* locoSelect = nullptr;
            BlendSpace2D* stand = nullptr;
            BlendSpace2D* crouch = nullptr;
            BlendSpace1D* aim = nullptr;
            LayeredBlend* upperBodyLayer = nullptr;
            AnimSlot* upperBodySlot = nullptr;
            AnimSlot* fullBodySlot = nullptr;
        };

        void Move(float deltaTime);
        void ReadFeedback(ModelAndInstanceData& scene);
        void ApplyVisuals(ModelAndInstanceData& scene);
        GraphNodes* ResolveNodes(ModelAndInstanceData& scene);
        void PushLog(std::string line);
        void SetRequestStatus(RequestId id, RequestStatus status);
        void UpdateRequests();

        std::shared_ptr<CharacterShared> mShared;

        std::unique_ptr<SMManager> mManager;
        std::shared_ptr<ModelInstance> mInstance;
        std::shared_ptr<CharacterProfile> mProfile;
        std::shared_ptr<AnimGraphAsset> mGraphAsset;
        std::shared_ptr<AnimGraphAsset> mPreviousAsset; // the instance's graph before Attach, restored on Detach
        GraphNodes mNodes;

        std::map<RequestId, RequestStatus> mRequests;
        std::vector<std::pair<RequestId, RequestStatus>> mStatusChanges;

        CharacterInput mInput;
        MotionState mMotion;
        GraphFeedback mFeedback;
        AnimParams mAnim;
        float mEasedSpeed = 0.0f;
        float mEasedDirectionDeg = 0.0f;
        float mEasedAlpha = 0.0f;
        std::string mAppliedLocoSelect;
        float mLastDeltaTime = 0.0f;
        std::deque<std::string> mLog;
        std::deque<std::string> mNotifies;
    };
} // namespace RAnimation
