#pragma once
#include <any>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <StateMachine/EventDispatcher.h>
#include <StateMachine/StateMachineDefinition.h>

// Runtime side of the state machine. One SMManager per character; many managers share one definition.
//
// Frame protocol (Update):
//   1. OnUpdate on every active state, parent before child (parallel regions in child order)
//   2. evaluate the trigger-less transitions of every active state (guard, or IsDone when guard-less)
//   3. resolve the pending requests (from 2, from DispatchEvent, from RequestTransition):
//        order = priority desc, source depth desc (inner state wins a tie), request order asc;
//        a request is dropped when its source is no longer active or when it would exit a state that was
//        entered during this Update - so every region switches at most once per Update
//   4. execute: OnExit child->parent up to the scope, transition action, OnEnter parent->child down to
//      the target and its initial descendants
//
// Event protocol (DispatchEvent): dispatcher listeners, then bubbling from the deepest active leaf of
// every region towards the root: OnEvent first (true = consumed), then the state's event transitions
// (first match is queued and consumes). A ParallelState only sees the event if none of its regions
// consumed it. Requests queued by events execute in the next Update.

namespace RAnimation
{
    class SMManager;

    struct TransitionRequest
    {
        enum class Kind
        {
            Defined, // a Transition of the definition
            Direct   // jump to a target state (no guard, no action)
        };
        Kind kind = Kind::Defined;
        TransitionId transitionId = kInvalidTransitionId;
        StateId sourceStateId = kInvalidStateId; // Defined: transition source; Direct: unset
        StateId targetStateId = kInvalidStateId;
        int priority = 0;
        int depth = 0;      // ordering key (source depth, or target depth for Direct)
        uint32_t order = 0; // queue order
        Event event;        // triggering event (if any), available to the action through the context
    };

    // Everything that differs between two managers of the same definition.
    struct StateRuntimeData
    {
        std::set<StateId> activeStates;
        std::map<StateId, StateId> currentChildren; // SingleState -> active child
        std::map<StateId, std::unique_ptr<IStateInstanceData>> stateData;
    };

    // What one executed transition did (OnTransition hook, debug UI).
    struct TransitionRecord
    {
        TransitionId transition = kInvalidTransitionId; // kInvalidTransitionId for Direct
        StateId from = kInvalidStateId;
        StateId to = kInvalidStateId;
        std::vector<StateId> exited; // in callback order
        std::vector<StateId> entered;
    };

    // Short-lived view handed to the state callbacks, guards and actions. Holds no state of its own.
    class StateContext
    {
    public:
        StateContext(SMManager& manager, StateId stateId, float deltaTime, const Event* event = nullptr) :
            mManager(manager), mStateId(stateId), mDeltaTime(deltaTime), mEvent(event)
        {
        }

        SMManager& Manager() const { return mManager; }
        StateId GetStateId() const { return mStateId; }
        const SMState& GetState() const;
        const StateMachineDefinition& Definition() const;
        float DeltaTime() const { return mDeltaTime; }
        // The event being handled (OnEvent, event guards / actions); null otherwise.
        const Event* CurrentEvent() const { return mEvent; }

        // Per-instance data of this state (created on first use through SMState::CreateRuntimeData).
        template <typename T> T* GetStateData() const;
        // The manager's user data (see SMManager::SetUserData); null if none or of another type.
        template <typename T> T* UserData() const;

        bool RequestTransition(TransitionId transition) const;
        bool RequestTransitionTo(StateId target) const;

    private:
        SMManager& mManager;
        StateId mStateId;
        float mDeltaTime;
        const Event* mEvent;
    };

    class SMManager
    {
    public:
        explicit SMManager(std::shared_ptr<const StateMachineDefinition> definition);
        SMManager(const SMManager&) = delete;
        SMManager& operator=(const SMManager&) = delete;

        const StateMachineDefinition& Definition() const { return *mDefinition; }
        std::shared_ptr<const StateMachineDefinition> DefinitionPtr() const { return mDefinition; }

        // Enters the root and its initial descendants. False if the definition is not finalized.
        bool Start();
        // Exits every active state (child before parent) and drops pending requests.
        void Stop();
        bool IsRunning() const { return mRunning; }

        void Update(float deltaTime);

        void DispatchEvent(const Event& event);
        void DispatchEvent(EventId id);
        // By registered name; false if unknown.
        bool DispatchEvent(std::string_view name);

        // Queue a defined transition / a direct jump; executed by the next Update (subject to conflict
        // resolution). False for unknown ids.
        bool RequestTransition(TransitionId transition);
        bool RequestTransitionTo(StateId target);

        bool IsActive(StateId state) const { return mRuntime.activeStates.count(state) != 0; }
        // Active child of a SingleState (kInvalidStateId if the state is not active / not Single).
        StateId CurrentChild(StateId state) const;
        // Deepest active states, one per region, in tree order.
        std::vector<StateId> ActiveLeaves() const;
        const StateRuntimeData& Runtime() const { return mRuntime; }

        IStateInstanceData* GetStateData(StateId state);
        template <typename T> T* GetStateData(StateId state) { return dynamic_cast<T*>(GetStateData(state)); }

        // Untyped per-manager payload handed to every state of this instance (owner pointers, inputs the
        // guards read, outputs the states write). Deliberately not a key/value store with observers (a
        // behaviour-tree blackboard); the consumer defines the type and who writes which field.
        template <typename T> void SetUserData(T value) { mUserData = std::move(value); }
        template <typename T> T* UserData() { return std::any_cast<T>(&mUserData); }

        EventDispatcher& Dispatcher() { return mDispatcher; }

        // Debug / UI hook, called after every executed transition (also for the Start enter path, with
        // transition = kInvalidTransitionId and from = kInvalidStateId).
        std::function<void(const TransitionRecord&)> OnTransition;
        int TransitionsLastUpdate() const { return mTransitionsLastUpdate; }

        // Runtime snapshot support: makes the given leaves (and their ancestors) active without running
        // any callback, dropping the current state first. Returns false (and changes nothing) when the set
        // is not a valid configuration of the tree.
        bool RestoreActiveLeaves(const std::vector<StateId>& leaves, std::string& outError);
        // Replaces the per-state data (the manager takes ownership). Used by the runtime deserializer.
        void SetStateData(StateId state, std::unique_ptr<IStateInstanceData> data);

    private:
        void EnterState(StateId state, std::vector<StateId>& entered);
        void EnterInitialDescendants(StateId state, std::vector<StateId>& entered);
        void EnterPath(StateId from, StateId target, std::vector<StateId>& entered);
        void ExitSubtree(StateId state, std::vector<StateId>& exited);
        void UpdateSubtree(StateId state, float deltaTime);
        void CollectAutomaticRequests(StateId state, float deltaTime);
        bool DeliverEvent(StateId state, const Event& event);
        bool HandleEventAtState(StateId state, const Event& event);
        void ResolveRequests();
        bool ExecuteRequest(const TransitionRequest& request);
        void QueueDefined(const Transition& transition, const Event* event);

        std::shared_ptr<const StateMachineDefinition> mDefinition;
        StateRuntimeData mRuntime;
        EventDispatcher mDispatcher;
        std::deque<TransitionRequest> mPending;
        std::any mUserData;
        std::set<StateId> mEnteredThisUpdate;
        uint32_t mOrderCounter = 0;
        int mTransitionsLastUpdate = 0;
        float mDeltaTime = 0.0f;
        bool mRunning = false;
    };

    template <typename T> T* StateContext::GetStateData() const
    {
        return mManager.GetStateData<T>(mStateId);
    }

    template <typename T> T* StateContext::UserData() const
    {
        return mManager.UserData<T>();
    }
} // namespace RAnimation
