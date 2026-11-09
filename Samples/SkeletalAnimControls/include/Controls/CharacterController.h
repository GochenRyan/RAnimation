#pragma once
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Controls/CharacterProfile.h>
#include <StateMachine/SMManager.h>
#include <StateMachine/StateMachineDefinition.h>
#include <StateMachine/StateTypeRegistry.h>

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

    struct CharacterInput
    {
        float moveX = 0.0f;    // -1..1 strafe (right positive)
        float moveY = 0.0f;    // -1..1 forward
        float turn = 0.0f;     // -1..1 yaw (left/right turn in place or while moving)
        bool run = false;      // jog instead of walk
        bool sprint = false;   // sprint sub machine
        bool crouch = false;
        bool aim = false;      // raises the upper body layer when armed
        float aimPitch = 0.0f; // -1..1, drives the aim offset blend space

        float MoveMagnitude() const
        {
            const float m = moveX * moveX + moveY * moveY;
            return m > 1.0f ? 1.0f : (m > 0.0f ? std::sqrt(m) : 0.0f);
        }
    };

    // Gameplay truth about where the character is, owned by the controller (Move), read by the guards.
    struct MotionState
    {
        bool grounded = true;
        bool airborne = false;
        float speed = 0.0f;           // horizontal m/s actually moved this frame
        float verticalVelocity = 0.0f;
        float yawDeg = 0.0f;
        glm::vec3 position = glm::vec3(0.0f);
        bool movementLocked = false;  // a montage with lockMove is playing
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

        // Registers the character state types / guards and loads the definition from assetsDir.
        bool Init(const std::string& assetsDir, std::string& outError);

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
        bool RequestAction(const std::string& montage);  // event "Action" with the montage name
        void ToggleWeapon();
        void Die();

        // Per frame, before AnimationSystem::Update: Move -> feedback -> state machine -> ApplyVisuals.
        void Update(float deltaTime, ModelAndInstanceData& scene);

        SMManager* Manager() const { return mManager.get(); }
        const std::string& DefinitionSource() const { return mSource; }
        const std::string& DefinitionDoc() const { return mDoc; }
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

        StateTypeRegistry mRegistry;
        std::shared_ptr<StateMachineDefinition> mDefinition;
        std::string mSource;
        std::string mDoc;
        std::string mAssetsDir;

        std::unique_ptr<SMManager> mManager;
        std::shared_ptr<ModelInstance> mInstance;
        std::shared_ptr<CharacterProfile> mProfile;
        std::shared_ptr<AnimGraphAsset> mGraphAsset;
        std::shared_ptr<AnimGraphAsset> mPreviousAsset; // the instance's graph before Attach, restored on Detach
        std::map<std::string, std::shared_ptr<AnimGraphAsset>> mGraphCache;   // by path
        std::map<std::string, std::shared_ptr<CharacterProfile>> mProfileCache; // by path
        GraphNodes mNodes;

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
