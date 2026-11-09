#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <StateMachine/IState.h>
#include <StateMachine/StateMachineTypes.h>

// Shared, immutable description of a state machine: the state tree, the transition rules and the event
// vocabulary. Built in code (or by StateMachineSerializer), then Finalize()d; afterwards many SMManagers
// (one per character) share the same definition and keep their own runtime state.

namespace RAnimation
{
    class StateContext;
    class StateMachineDefinition;

    // A state in the tree. Leaf states derive from SMState directly and override the IState callbacks;
    // SingleState / ParallelState are the two composite kinds. Structural fields are filled in by the
    // definition (ids, parent, children, outgoing transitions) and are read-only for everyone else.
    class SMState : public IState
    {
    public:
        explicit SMState(std::string name) : mName(std::move(name)) {}

        virtual StateKind GetKind() const { return StateKind::Leaf; }

        StateId GetId() const { return mId; }
        StateId GetParent() const { return mParent; }
        const std::string& GetName() const { return mName; }
        int GetDepth() const { return mDepth; }
        // Registry type name (serialization); empty for states built in code without a registry.
        const std::string& GetTypeName() const { return mTypeName; }
        void SetTypeName(std::string typeName) { mTypeName = std::move(typeName); }
        // Authored parameters (serialized with the definition, handed to the registry factory).
        const ParamBag& GetParams() const { return mParams; }
        ParamBag& Params() { return mParams; }

        const std::vector<StateId>& GetChildren() const { return mChildren; }
        // Outgoing transitions, sorted by priority (desc) then insertion order once finalized.
        const std::vector<TransitionId>& GetTransitions() const { return mTransitions; }

    private:
        friend class StateMachineDefinition;

        std::string mName;
        std::string mTypeName;
        ParamBag mParams;
        StateId mId = kInvalidStateId;
        StateId mParent = kInvalidStateId;
        int mDepth = 0;
        std::vector<StateId> mChildren;
        std::vector<TransitionId> mTransitions;
    };

    // XOR composite: exactly one child is active. Entering it enters the initial child (no history).
    // IsDone defaults to the active child's IsDone.
    class SingleState : public SMState
    {
    public:
        using SMState::SMState;

        StateKind GetKind() const override { return StateKind::Single; }
        StateId GetInitialChild() const { return mInitialChild; }
        void SetInitialChild(StateId child) { mInitialChild = child; }

        bool IsDone(StateContext& ctx) const override;

    private:
        StateId mInitialChild = kInvalidStateId;
    };

    // AND composite: every child (region) is active while it is. IsDone follows the completion policy.
    class ParallelState : public SMState
    {
    public:
        using SMState::SMState;

        StateKind GetKind() const override { return StateKind::Parallel; }
        CompletionPolicy GetCompletionPolicy() const { return mPolicy; }
        void SetCompletionPolicy(CompletionPolicy policy) { mPolicy = policy; }

        bool IsDone(StateContext& ctx) const override;

    private:
        CompletionPolicy mPolicy = CompletionPolicy::All;
    };

    // Guard: may the transition fire now? `event` is the triggering event for event transitions and null
    // for transitions evaluated every Update. Action: runs after the exits and before the enters.
    using GuardFn = std::function<bool(StateContext& ctx, const Event* event)>;
    using ActionFn = std::function<void(StateContext& ctx)>;

    // A transition rule (shared). Firing semantics:
    //   trigger set   -> evaluated when that event reaches `source` while bubbling; guard (if any) must pass
    //   no trigger    -> evaluated every Update: guard (if any) must pass; with no guard it fires when the
    //                    source state IsDone() ("completion transition")
    struct Transition
    {
        TransitionId id = kInvalidTransitionId;
        std::string name;
        StateId source = kInvalidStateId;
        StateId target = kInvalidStateId;
        EventId trigger = kInvalidEventId;
        int priority = 0;
        GuardFn guard;
        ActionFn action;
        // Registry names, kept so the definition can be serialized (the callables themselves cannot).
        std::string guardName;
        std::string actionName;

        bool CanTrigger(StateContext& ctx, const Event* event) const;
        void ExecuteAction(StateContext& ctx) const;
    };

    class StateMachineDefinition
    {
    public:
        std::string Name;

        // ---- building (before Finalize) ------------------------------------------------------------
        // Adds a state under `parent` (kInvalidStateId = top level; exactly one top-level state must be
        // the root). Returns the new id, or kInvalidStateId if the name is taken or the parent is a leaf.
        StateId AddState(std::unique_ptr<SMState> state, StateId parent = kInvalidStateId);
        void SetRoot(StateId root) { mRoot = root; }
        EventId RegisterEvent(const std::string& name);
        TransitionId AddTransition(Transition transition);

        // Structural checks (empty = ok). Finalize runs them and then derives the per-state data
        // (depth, sorted outgoing transitions). The definition is immutable afterwards.
        std::vector<std::string> Validate() const;
        bool Finalize(std::vector<std::string>* outErrors = nullptr);
        bool IsFinalized() const { return mFinalized; }

        // ---- queries ----------------------------------------------------------------------------------
        StateId GetRoot() const { return mRoot; }
        size_t StateCount() const { return mStates.size(); }
        SMState* GetState(StateId id) const { return id < mStates.size() ? mStates[id].get() : nullptr; }
        StateId FindState(std::string_view name) const;
        std::vector<StateId> TopLevelStates() const;

        size_t TransitionCount() const { return mTransitions.size(); }
        const Transition* GetTransition(TransitionId id) const
        {
            return id < mTransitions.size() ? &mTransitions[id] : nullptr;
        }

        EventId FindEvent(std::string_view name) const;
        const std::string& EventName(EventId id) const;
        size_t EventCount() const { return mEventNames.size(); }

        // Inclusive: IsAncestor(a, a) is true.
        bool IsAncestor(StateId ancestor, StateId state) const;
        StateId LowestCommonAncestor(StateId a, StateId b) const;
        // "Root/Locomotion/Idle"
        std::string StatePath(StateId id) const;

    private:
        std::vector<std::unique_ptr<SMState>> mStates;
        std::map<std::string, StateId, std::less<>> mStateLookup;
        std::vector<Transition> mTransitions;
        std::vector<std::string> mEventNames;
        std::map<std::string, EventId, std::less<>> mEventLookup;
        StateId mRoot = kInvalidStateId;
        bool mFinalized = false;
    };
} // namespace RAnimation
