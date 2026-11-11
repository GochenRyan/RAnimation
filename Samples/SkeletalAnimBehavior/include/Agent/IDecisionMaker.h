#pragma once
#include <limits>
#include <string>

#include <Agent/AgentIntent.h>
#include <Agent/ExecutionFeedback.h>
#include <Agent/WorldQuery.h>
#include <Behavior/Blackboard.h>
#include <Tools/SaveTree.h>

// The replaceable decision interface (D1, §4.1). A decision maker only sees the context below and only
// writes the AgentIntent; CharacterSystem schedules it, Steering executes the intent.

namespace RAnimation
{
    struct DecisionContext
    {
        float worldTime = 0.0f;          // accumulated world time (s), all timers use it
        float deltaSinceLastTick = 0.0f;
        EntityId self = kInvalidEntityId;
        const ExecutionFeedback* feedback = nullptr;
        Blackboard* instanceBlackboard = nullptr; // perception.* + the tree's instance keys
        BlackboardStore* globalBlackboard = nullptr;
        AgentRequestAllocator* requests = nullptr;
        const WorldQuery* world = nullptr;
    };

    class IDecisionMaker
    {
    public:
        virtual ~IDecisionMaker() = default;
        virtual void Tick(DecisionContext& ctx, AgentIntent& intent) = 0;
        // Pause / possession: stop everything running and hand the intent channels back.
        virtual void Halt(AgentIntent& intent) = 0;
        // Start over (load outside the save radius D15, Edit -> View D17, possession ends D3).
        virtual void Reset() = 0;
        // Earliest timer (world time), +inf when none (D9).
        virtual float NextWakeTime() const { return std::numeric_limits<float>::infinity(); }
        virtual bool SaveRuntime(SaveNode& out) const = 0;
        virtual bool LoadRuntime(const SaveNode& in, std::string& outError) = 0;
        virtual const char* Kind() const = 0; // "BehaviorTree" / "Player" / "Script" / "Idle"
        // Tick rate; 0 = every frame.
        virtual float TickHz() const { return 0.0f; }
        // Instance blackboard layout this brain needs (null = the perception keys only).
        virtual const BlackboardSchema* InstanceSchema() const { return nullptr; }
        // The agent's instance blackboard (built from InstanceSchema), known before the first tick so a
        // checkpoint can be loaded into a fresh brain.
        virtual void BindInstanceBlackboard(Blackboard*) {}
        virtual std::string Describe() const { return Kind(); }
    };

    // Does nothing: agents whose config names no tree (or whose tree failed to load).
    class IdleBrain final : public IDecisionMaker
    {
    public:
        void Tick(DecisionContext&, AgentIntent&) override {}
        void Halt(AgentIntent& intent) override { intent.ResetChannels(); }
        void Reset() override {}
        bool SaveRuntime(SaveNode&) const override { return true; }
        bool LoadRuntime(const SaveNode&, std::string&) override { return true; }
        const char* Kind() const override { return "Idle"; }
        float TickHz() const override { return 1.0f; }
    };
} // namespace RAnimation
