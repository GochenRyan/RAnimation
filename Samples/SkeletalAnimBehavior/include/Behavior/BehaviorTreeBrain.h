#pragma once
#include <deque>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <Agent/AgentIntent.h>
#include <Agent/ExecutionFeedback.h>
#include <Agent/IDecisionMaker.h>
#include <Agent/WorldQuery.h>
#include <Behavior/BehaviorTreeAsset.h>
#include <Behavior/Blackboard.h>
#include <Behavior/NodeTypes.h>

// Per-agent runtime of a behaviour tree (§5.1) and the IDecisionMaker that runs it. Dependency rule (§2.1):
// Behavior/ only includes Agent/AgentIntent.h, ExecutionFeedback.h, IDecisionMaker.h, WorldQuery.h,
// PerceptionKeys.h and Behavior/ - never Controls/, StateMachine/ or AnimGraph/.

namespace RAnimation
{
    class BehaviorSystem;

    // Blackboard values <-> checkpoint (§8.4 encoding: one bb:<name> entry per set key, from firstSlot on).
    void SaveBlackboardValues(const BlackboardStore& store, SaveNode& out, size_t firstSlot);
    void LoadBlackboardValues(BlackboardStore& store, const SaveNode& in, size_t firstSlot, EntityId writer);

    // Built-in runtime data of one node (D12: shown read-only as node keys). Reset by Halt (readyTime kept).
    struct NodeMemory
    {
        int index = 0;              // Mem* current child
        uint64_t doneMask = 0;      // Parallel: finished children
        uint64_t successMask = 0;   // Parallel: succeeded children
        int counter = 0;            // Repeat iteration / Retry attempt
        float startTime = 0.0f;     // Timeout / MoveTo start
        float wakeAt = std::numeric_limits<float>::infinity(); // timer (Timeout deadline, Wait end, Cooldown ready)
        float readyTime = -std::numeric_limits<float>::infinity(); // Cooldown
        RequestId request = 0;      // PlayAction
        bool started = false;
    };

    struct BehaviorTreeInstance
    {
        std::vector<NodeStatus> status;
        std::vector<float> lastTickTime;
        std::vector<NodeMemory> memory;
        std::vector<BlackboardStore> nodeKeys; // per node index
        bool immediateWake = false;            // Repeat / Retry hit the per-tick loop limit
        float lastTickWorldTime = 0.0f;
    };

    struct TickLogEntry
    {
        float time = 0.0f;
        uint32_t nodeId = 0;
        NodeStatus from = NodeStatus::Idle;
        NodeStatus to = NodeStatus::Idle;
        std::string text; // Log node output / notes
    };

    // Everything a node function may touch during one tick or halt.
    class TreeContext
    {
    public:
        TreeContext(const CompiledTree& tree, BehaviorTreeInstance& instance, const DecisionContext& decision,
                    AgentIntent& intent, BlackboardStore* treeShared, std::deque<TickLogEntry>* log)
            : tree(tree), instance(instance), decision(decision), intent(intent), treeShared(treeShared), mLog(log)
        {
        }

        NodeStatus Tick(uint32_t node);
        void Halt(uint32_t node);

        const CompiledNode& Node(uint32_t node) const { return tree.nodes[node]; }
        NodeMemory& Mem(uint32_t node) { return instance.memory[node]; }
        float Now() const { return decision.worldTime; }
        EntityId Self() const { return decision.self; }

        // parameter p of a node: bound key value or literal
        BbValue Param(uint32_t node, size_t p) const;
        float ParamFloat(uint32_t node, size_t p) const;
        bool ParamBool(uint32_t node, size_t p) const;
        int ParamInt(uint32_t node, size_t p) const;
        std::string ParamString(uint32_t node, size_t p) const;
        bool ParamIsSet(uint32_t node, size_t p) const;
        BbType ParamKeyType(uint32_t node, size_t p) const { return Node(node).params[p].keyType; }
        bool SetParamKey(uint32_t node, size_t p, const BbValue& value);
        bool ClearParamKey(uint32_t node, size_t p);

        // D14 channel ownership
        void ClaimLocomotion(uint32_t node) { intent.locomotionOwner = node; }
        void ReleaseLocomotion(uint32_t node);
        void ClaimStance(uint32_t node) { intent.stanceOwner = node; }

        void Log(uint32_t node, std::string text);

        const CompiledTree& tree;
        BehaviorTreeInstance& instance;
        const DecisionContext& decision;
        AgentIntent& intent;
        BlackboardStore* treeShared;

    private:
        BlackboardStore* Store(const CompiledParam& param) const;
        std::deque<TickLogEntry>* mLog;
    };

    class BehaviorTreeBrain final : public IDecisionMaker
    {
    public:
        static constexpr size_t kLogCapacity = 200;

        BehaviorTreeBrain(BehaviorSystem* system, std::shared_ptr<const CompiledTree> tree, EntityId self, float tickHzOverride);
        ~BehaviorTreeBrain() override;

        void Tick(DecisionContext& ctx, AgentIntent& intent) override;
        void Halt(AgentIntent& intent) override;
        void Reset() override;
        float NextWakeTime() const override;
        bool SaveRuntime(SaveNode& out) const override;
        bool LoadRuntime(const SaveNode& in, std::string& outError) override;
        const char* Kind() const override { return "BehaviorTree"; }
        float TickHz() const override;
        const BlackboardSchema* InstanceSchema() const override { return &mTree->instanceSchema; }
        void BindInstanceBlackboard(Blackboard* blackboard) override { mLastInstanceBlackboard = blackboard; }
        Blackboard* InstanceBlackboard() const { return mLastInstanceBlackboard; }
        std::string Describe() const override;

        const std::shared_ptr<const CompiledTree>& Tree() const { return mTree; }
        const BehaviorTreeInstance& Instance() const { return mInstance; }
        const std::deque<TickLogEntry>& Log() const { return mLog; }
        NodeStatus RootStatus() const { return mInstance.status.empty() ? NodeStatus::Idle : mInstance.status[0]; }
        uint64_t TickCount() const { return mTicks; }
        BlackboardStore* TreeShared() const { return mTreeShared; }
        // Every write to a key this brain makes is attributed to this entity (self writes never wake it).
        EntityId Self() const { return mSelf; }

    private:
        void InitInstance();

        BehaviorSystem* mSystem = nullptr;
        std::shared_ptr<const CompiledTree> mTree;
        EntityId mSelf = kInvalidEntityId;
        float mTickHzOverride = 0.0f;
        BehaviorTreeInstance mInstance;
        BlackboardStore* mTreeShared = nullptr;
        Blackboard* mLastInstanceBlackboard = nullptr; // for Reset (tree instance keys back to defaults)
        std::deque<TickLogEntry> mLog;
        uint64_t mTicks = 0;
        DecisionContext mLastContext;
    };
} // namespace RAnimation
