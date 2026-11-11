#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Agent/AgentIntent.h>
#include <Agent/IDecisionMaker.h>
#include <Agent/Perception.h>
#include <Agent/PlayerBrain.h>
#include <Agent/Steering.h>
#include <Agent/WorldQuery.h>
#include <Behavior/Blackboard.h>
#include <Controls/CharacterController.h>
#include <Model/EntityId.h>

struct ModelAndInstanceData;

// Intent layer (§2, §4): one CharacterController per agent, the decision maker that drives it, the per-frame
// scheduler (D9 / D10), steering, perception and possession (D3). Frame order inside Update:
//   (1) Perception      last frame's contact events + distance / FOV / line of sight / zones -> instance blackboards
//   (2) Scheduling      agents in ascending entity id: tick the due ones -> AgentIntent
//   (3) Steering        AgentIntent + MotionState -> CharacterInput + commands
//   (4) Controllers     Move -> graph feedback -> state machine -> ApplyVisuals -> ExecutionFeedback
// Edit mode (paused): (1)-(3) are skipped, world time stops, controllers run with no input.

namespace RAnimation
{
    class ModelInstance;

    struct AgentSchedule
    {
        float intervalSec = 0.0f;  // 1 / Hz; 0 = every frame
        float accumulator = 0.0f;  // starts at the phase offset (staggerPhases)
        float nextWakeTime = std::numeric_limits<float>::infinity();
        bool wakeFlag = false;     // this frame
        bool wakeFlagNext = false; // raised during scheduling / execution: next frame
        uint64_t ticks = 0;
        uint64_t wakeTicks = 0;    // ticks caused by a wake (key change, timer, request status)
        float lastTickTime = 0.0f;
        bool lastTickWasWake = false;
    };

    struct Agent
    {
        EntityId entity = kInvalidEntityId;
        std::shared_ptr<ModelInstance> instance;
        std::unique_ptr<CharacterController> controller;
        std::unique_ptr<IDecisionMaker> brain;
        std::unique_ptr<IDecisionMaker> parkedBrain; // the agent's own brain while the player possesses it
        Blackboard instanceBlackboard;
        AgentIntent intent;
        ExecutionFeedback feedback;
        AgentSchedule schedule;
        SteeringMemory steering;
        AgentRequestAllocator requests;
        std::optional<AgentConfig> config; // level config the agent was built from
        bool fromConfig = false;           // created by SyncFromScene (removed when the config goes away)
        PerceptionMemory perception;

        // blocked detection (MoveTo)
        glm::vec3 blockedAnchor = glm::vec3(0.0f);
        float blockedAnchorTime = 0.0f;
        glm::vec3 lastTarget = glm::vec3(1e9f);
        LocomotionMode lastMode = LocomotionMode::None;

        // the brain that defines the agent's behaviour (parked one while possessed)
        IDecisionMaker* OwnBrain() const { return parkedBrain ? parkedBrain.get() : brain.get(); }
    };

    class CharacterSystem final : public WorldQuery
    {
    public:
        using BrainFactory = std::function<std::unique_ptr<IDecisionMaker>(const AgentConfig&, EntityId, std::string& error)>;

        bool Init(const std::string& assetsDir, std::string& outError);
        bool IsInitialized() const { return mShared != nullptr; }
        const std::shared_ptr<CharacterShared>& Shared() const { return mShared; }
        void SetBrainFactory(BrainFactory factory) { mBrainFactory = std::move(factory); }
        // Wake hook for blackboards outside the agents (global / tree shared, owned by the behaviour system).
        BlackboardStore* globalBlackboard = nullptr;

        // Creates the controller (needs a character profile for the model) and installs brain.
        Agent* AddAgent(const std::shared_ptr<ModelInstance>& instance, std::unique_ptr<IDecisionMaker> brain, std::string& outError);
        // Decision-only agent without instance / controller (self tests of scheduling and wake-ups): it is
        // scheduled and ticked, steering / perception / execution skip it.
        Agent* AddHeadlessAgent(EntityId id, std::unique_ptr<IDecisionMaker> brain);
        void RemoveAgent(EntityId id);
        void Clear();
        // Replaces the agent's own brain (Reset + schedule restart); while possessed the parked one.
        void SetBrain(Agent& agent, std::unique_ptr<IDecisionMaker> brain);
        // Rebuilds the brain from agent.config through the factory (tree changed, Edit -> View).
        void RebuildBrain(Agent& agent);

        Agent* Find(EntityId id);
        const Agent* Find(EntityId id) const;
        Agent* FindByInstance(const ModelInstance* instance);
        const std::vector<std::unique_ptr<Agent>>& Agents() const { return mAgents; }

        // D3: the current player agent gets its own brain back (Reset), the target's brain is halted and
        // parked, the target runs playerBrain (default: PlayerBrain on PlayerInput()). 0 releases.
        bool Possess(EntityId id, std::unique_ptr<IDecisionMaker> playerBrain = nullptr);
        EntityId Possessed() const { return mPossessed; }
        PlayerInputBuffer& PlayerInput() { return mPlayerInput; }

        // Config-driven agents: one per instance with an AgentConfig (and a character profile); removed with
        // their instance or config, rebuilt when the config's tree / tick rate changes. Instances that cannot be
        // agents are reported once.
        void SyncFromScene(ModelAndInstanceData& scene);

        void Update(float deltaTime, ModelAndInstanceData& scene, bool paused, const PerceptionInputs& perception);
        void ApplyPush(Agent& agent, const glm::vec3& delta);

        float WorldTime() const { return mWorldTime; }
        void SetWorldTime(float t) { mWorldTime = t; }
        // Brains reset / loaded: raise this frame's wake flag so they tick immediately.
        void WakeNow(EntityId id);
        void WakeNextFrame(EntityId id);
        void ResetSchedule(Agent& agent, bool wake);

        bool staggerPhases = true;
        SteeringSettings steering;
        PerceptionSettings perceptionSettings;
        float blockedSeconds = 1.0f;
        float blockedEpsilon = 0.2f;
        // P7: interaction request range filter (book §10): max distance, min distance, half FOV both ways.
        float interactMaxDistance = 3.0f;
        float interactMinDistance = 0.3f;
        float interactFovHalfDeg = 70.0f;

        // WorldQuery
        std::optional<glm::vec3> Position(EntityId id) const override;
        std::optional<float> YawDeg(EntityId id) const override;
        EntityId PlayerEntity() const override { return mPossessed; }
        std::optional<glm::vec3> FindMarker(MarkerKind kind, const std::string& name, int index) const override;
        int RouteLength(const std::string& route) const override;

        // statistics of the last Update
        struct Stats
        {
            int agents = 0;
            int ticked = 0;
            int woken = 0;
            int rays = 0;
            double perceptionUs = 0.0;
            double decisionUs = 0.0;
            double executionUs = 0.0;
        };
        const Stats& LastStats() const { return mStats; }
        // possession changes, interaction requests, agents that could not be created
        const std::deque<std::string>& EventLog() const { return mEventLog; }
        // P7: the last interaction request: target agent (0 = nobody qualified)
        EntityId LastInteractionTarget() const { return mLastInteractionTarget; }

    private:
        enum class Phase
        {
            Idle,
            Perception,
            Scheduling,
            Execution
        };

        void InitBlackboard(Agent& agent);
        void UpdatePerception(const PerceptionInputs& inputs);
        void HandleInteraction(Agent& player);
        void ExecuteAgent(Agent& agent, float deltaTime, ModelAndInstanceData& scene, bool paused);
        void Log(std::string line);
        void SortAgents();

        std::shared_ptr<CharacterShared> mShared;
        BrainFactory mBrainFactory;
        std::vector<std::unique_ptr<Agent>> mAgents; // ascending entity id
        EntityId mPossessed = kInvalidEntityId;
        PlayerInputBuffer mPlayerInput;
        float mWorldTime = 0.0f;
        Phase mPhase = Phase::Idle;
        ModelAndInstanceData* mScene = nullptr;
        std::set<EntityId> mRejected; // instances with a config that cannot become agents (reported once)
        Stats mStats;
        std::deque<std::string> mEventLog;
        EntityId mLastInteractionTarget = kInvalidEntityId;
    };
} // namespace RAnimation
