#include <StateMachine/SMManager.h>

#include <algorithm>

#include <fmt/format.h>

namespace RAnimation
{
    // ---- StateContext ------------------------------------------------------------------------------------
    const SMState& StateContext::GetState() const
    {
        return *mManager.Definition().GetState(mStateId);
    }

    const StateMachineDefinition& StateContext::Definition() const
    {
        return mManager.Definition();
    }

    bool StateContext::RequestTransition(TransitionId transition) const
    {
        return mManager.RequestTransition(transition);
    }

    bool StateContext::RequestTransitionTo(StateId target) const
    {
        return mManager.RequestTransitionTo(target);
    }

    // ---- SMManager: lifecycle ------------------------------------------------------------------------------
    SMManager::SMManager(std::shared_ptr<const StateMachineDefinition> definition) : mDefinition(std::move(definition))
    {
    }

    bool SMManager::Start()
    {
        if (!mDefinition || !mDefinition->IsFinalized())
        {
            return false;
        }
        if (mRunning)
        {
            Stop();
        }
        mRuntime.activeStates.clear();
        mRuntime.currentChildren.clear();
        mPending.clear();
        mRunning = true;

        TransitionRecord record;
        EnterState(mDefinition->GetRoot(), record.entered);
        EnterInitialDescendants(mDefinition->GetRoot(), record.entered);
        record.to = mDefinition->GetRoot();
        if (OnTransition)
        {
            OnTransition(record);
        }
        return true;
    }

    void SMManager::Stop()
    {
        if (!mRunning)
        {
            return;
        }
        std::vector<StateId> exited;
        ExitSubtree(mDefinition->GetRoot(), exited);
        mRuntime.activeStates.clear();
        mRuntime.currentChildren.clear();
        mPending.clear();
        mRunning = false;
    }

    // ---- enter / exit primitives ---------------------------------------------------------------------------
    void SMManager::EnterState(StateId state, std::vector<StateId>& entered)
    {
        SMState* s = mDefinition->GetState(state);
        if (s == nullptr || mRuntime.activeStates.count(state) != 0)
        {
            return;
        }
        mRuntime.activeStates.insert(state);
        mEnteredThisUpdate.insert(state);
        const SMState* parent = mDefinition->GetState(s->GetParent());
        if (parent != nullptr && parent->GetKind() == StateKind::Single)
        {
            mRuntime.currentChildren[parent->GetId()] = state;
        }
        entered.push_back(state);
        StateContext ctx(*this, state, mDeltaTime);
        s->OnEnter(ctx);
    }

    // Enters the default descendants of an already-entered state: the initial child of a SingleState (and
    // recursively its defaults), every region of a ParallelState.
    void SMManager::EnterInitialDescendants(StateId state, std::vector<StateId>& entered)
    {
        const SMState* s = mDefinition->GetState(state);
        if (s == nullptr)
        {
            return;
        }
        switch (s->GetKind())
        {
            case StateKind::Single:
            {
                const StateId child = static_cast<const SingleState*>(s)->GetInitialChild();
                EnterState(child, entered);
                EnterInitialDescendants(child, entered);
                break;
            }
            case StateKind::Parallel:
                for (StateId child : s->GetChildren())
                {
                    EnterState(child, entered);
                    EnterInitialDescendants(child, entered);
                }
                break;
            case StateKind::Leaf:
            default:
                break;
        }
    }

    // Enters the chain below `from` (exclusive) down to `target` (inclusive), then target's defaults. A
    // ParallelState met on the way also gets its other regions entered with their defaults.
    void SMManager::EnterPath(StateId from, StateId target, std::vector<StateId>& entered)
    {
        std::vector<StateId> path;
        for (StateId s = target; s != kInvalidStateId && s != from; s = mDefinition->GetState(s)->GetParent())
        {
            path.push_back(s);
        }
        std::reverse(path.begin(), path.end());

        for (size_t i = 0; i < path.size(); ++i)
        {
            const StateId state = path[i];
            EnterState(state, entered);
            const SMState* s = mDefinition->GetState(state);
            if (s->GetKind() == StateKind::Parallel && i + 1 < path.size())
            {
                const StateId onPath = path[i + 1];
                for (StateId child : s->GetChildren())
                {
                    if (child != onPath)
                    {
                        EnterState(child, entered);
                        EnterInitialDescendants(child, entered);
                    }
                }
            }
        }
        EnterInitialDescendants(target, entered);
    }

    // Exits `state` and everything active below it, children first.
    void SMManager::ExitSubtree(StateId state, std::vector<StateId>& exited)
    {
        SMState* s = mDefinition->GetState(state);
        if (s == nullptr || mRuntime.activeStates.count(state) == 0)
        {
            return;
        }
        switch (s->GetKind())
        {
            case StateKind::Single:
            {
                auto it = mRuntime.currentChildren.find(state);
                if (it != mRuntime.currentChildren.end())
                {
                    ExitSubtree(it->second, exited);
                    mRuntime.currentChildren.erase(state);
                }
                break;
            }
            case StateKind::Parallel:
                for (StateId child : s->GetChildren())
                {
                    ExitSubtree(child, exited);
                }
                break;
            case StateKind::Leaf:
            default:
                break;
        }
        StateContext ctx(*this, state, mDeltaTime);
        s->OnExit(ctx);
        mRuntime.activeStates.erase(state);
        exited.push_back(state);
    }

    // ---- update ---------------------------------------------------------------------------------------------
    void SMManager::UpdateSubtree(StateId state, float deltaTime)
    {
        SMState* s = mDefinition->GetState(state);
        if (s == nullptr || mRuntime.activeStates.count(state) == 0)
        {
            return;
        }
        StateContext ctx(*this, state, deltaTime);
        s->OnUpdate(ctx, deltaTime);
        switch (s->GetKind())
        {
            case StateKind::Single:
                UpdateSubtree(CurrentChild(state), deltaTime);
                break;
            case StateKind::Parallel:
                for (StateId child : s->GetChildren())
                {
                    UpdateSubtree(child, deltaTime);
                }
                break;
            case StateKind::Leaf:
            default:
                break;
        }
    }

    void SMManager::CollectAutomaticRequests(StateId state, float deltaTime)
    {
        const SMState* s = mDefinition->GetState(state);
        if (s == nullptr || mRuntime.activeStates.count(state) == 0)
        {
            return;
        }
        StateContext ctx(*this, state, deltaTime);
        for (TransitionId tid : s->GetTransitions())
        {
            const Transition& t = *mDefinition->GetTransition(tid);
            if (t.trigger != kInvalidEventId)
            {
                continue; // event transitions are evaluated when their event bubbles past this state
            }
            if (t.CanTrigger(ctx, nullptr))
            {
                QueueDefined(t, nullptr);
                break; // one candidate per state; conflicts are resolved afterwards
            }
        }
        switch (s->GetKind())
        {
            case StateKind::Single:
                CollectAutomaticRequests(CurrentChild(state), deltaTime);
                break;
            case StateKind::Parallel:
                for (StateId child : s->GetChildren())
                {
                    CollectAutomaticRequests(child, deltaTime);
                }
                break;
            case StateKind::Leaf:
            default:
                break;
        }
    }

    void SMManager::Update(float deltaTime)
    {
        if (!mRunning)
        {
            return;
        }
        mDeltaTime = deltaTime;
        mEnteredThisUpdate.clear();
        mTransitionsLastUpdate = 0;

        UpdateSubtree(mDefinition->GetRoot(), deltaTime);
        CollectAutomaticRequests(mDefinition->GetRoot(), deltaTime);
        ResolveRequests();
    }

    void SMManager::QueueDefined(const Transition& transition, const Event* event)
    {
        TransitionRequest r;
        r.kind = TransitionRequest::Kind::Defined;
        r.transitionId = transition.id;
        r.sourceStateId = transition.source;
        r.targetStateId = transition.target;
        r.priority = transition.priority;
        r.depth = mDefinition->GetState(transition.source)->GetDepth();
        r.order = mOrderCounter++;
        if (event != nullptr)
        {
            r.event = *event;
        }
        mPending.push_back(std::move(r));
    }

    bool SMManager::RequestTransition(TransitionId transition)
    {
        const Transition* t = mDefinition->GetTransition(transition);
        if (t == nullptr || !mRunning)
        {
            return false;
        }
        QueueDefined(*t, nullptr);
        return true;
    }

    bool SMManager::RequestTransitionTo(StateId target)
    {
        const SMState* s = mDefinition->GetState(target);
        if (s == nullptr || !mRunning)
        {
            return false;
        }
        TransitionRequest r;
        r.kind = TransitionRequest::Kind::Direct;
        r.targetStateId = target;
        r.depth = s->GetDepth();
        r.order = mOrderCounter++;
        mPending.push_back(std::move(r));
        return true;
    }

    void SMManager::ResolveRequests()
    {
        if (mPending.empty())
        {
            return;
        }
        // Requests queued by callbacks during execution belong to the next Update.
        std::vector<TransitionRequest> batch(mPending.begin(), mPending.end());
        mPending.clear();

        std::stable_sort(batch.begin(),
                         batch.end(),
                         [](const TransitionRequest& a, const TransitionRequest& b)
                         {
                             if (a.priority != b.priority)
                             {
                                 return a.priority > b.priority;
                             }
                             if (a.depth != b.depth)
                             {
                                 return a.depth > b.depth; // inner state wins a tie
                             }
                             return a.order < b.order;
                         });

        for (const TransitionRequest& r : batch)
        {
            if (ExecuteRequest(r))
            {
                ++mTransitionsLastUpdate;
            }
        }
    }

    // Computes the exit scope / enter path of one request and runs it. Returns false when the request is
    // stale (source inactive, target already active) or conflicts with a transition already executed in
    // this Update (it would exit a state entered this Update).
    bool SMManager::ExecuteRequest(const TransitionRequest& request)
    {
        const Transition* transition = request.kind == TransitionRequest::Kind::Defined
                                               ? mDefinition->GetTransition(request.transitionId)
                                               : nullptr;
        const StateId target = request.targetStateId;
        if (mDefinition->GetState(target) == nullptr)
        {
            return false;
        }

        // The nearest active ancestor of an inactive target is always a SingleState (every region of an
        // active ParallelState is active, so an inactive state never has an active Parallel parent). Its
        // current branch is what the transition replaces.
        auto firstActiveAncestor = [this](StateId state)
        {
            StateId ancestor = mDefinition->GetState(state)->GetParent();
            while (ancestor != kInvalidStateId && !IsActive(ancestor))
            {
                ancestor = mDefinition->GetState(ancestor)->GetParent();
            }
            return ancestor;
        };

        // The state whose active branch is replaced (exitRoot) and the state the enter path starts from.
        StateId exitRoot = kInvalidStateId;
        StateId enterFrom = kInvalidStateId;
        if (transition != nullptr)
        {
            if (!IsActive(transition->source))
            {
                return false;
            }
            const StateId lca = mDefinition->LowestCommonAncestor(transition->source, target);
            if (lca == target || IsActive(target))
            {
                // Target is the source, one of its ancestors, or otherwise already active: leave the whole
                // target subtree and re-enter it (external self-transition semantics: a restart).
                exitRoot = target;
                enterFrom = mDefinition->GetState(target)->GetParent();
            }
            else
            {
                // Target below the source, or on a sibling branch: swap the branch of the nearest active
                // ancestor (at or below the LCA, which is active since the source is). Cross-region
                // transitions were rejected by Validate, so that ancestor is a SingleState.
                const StateId swap = firstActiveAncestor(target);
                exitRoot = CurrentChild(swap);
                enterFrom = swap;
            }
        }
        else
        {
            if (IsActive(target))
            {
                return false;
            }
            const StateId swap = firstActiveAncestor(target);
            if (swap == kInvalidStateId)
            {
                return false;
            }
            exitRoot = CurrentChild(swap);
            enterFrom = swap;
        }

        // Conflict rule: a region switches at most once per Update.
        if (exitRoot != kInvalidStateId)
        {
            for (StateId active : mRuntime.activeStates)
            {
                if (mEnteredThisUpdate.count(active) != 0 && mDefinition->IsAncestor(exitRoot, active))
                {
                    return false;
                }
            }
        }

        TransitionRecord record;
        record.transition = transition != nullptr ? transition->id : kInvalidTransitionId;
        record.from = transition != nullptr ? transition->source : kInvalidStateId;
        record.to = target;

        if (exitRoot != kInvalidStateId)
        {
            ExitSubtree(exitRoot, record.exited);
        }
        if (transition != nullptr && transition->action)
        {
            const Event* event = request.event.id != kInvalidEventId ? &request.event : nullptr;
            StateContext ctx(*this, transition->source, mDeltaTime, event);
            transition->ExecuteAction(ctx);
        }
        EnterPath(enterFrom, target, record.entered);

        if (OnTransition)
        {
            OnTransition(record);
        }
        return true;
    }

    // ---- events ----------------------------------------------------------------------------------------------
    void SMManager::DispatchEvent(const Event& event)
    {
        mDispatcher.Dispatch(event);
        if (mRunning)
        {
            DeliverEvent(mDefinition->GetRoot(), event);
        }
    }

    void SMManager::DispatchEvent(EventId id)
    {
        Event e;
        e.id = id;
        DispatchEvent(e);
    }

    bool SMManager::DispatchEvent(std::string_view name)
    {
        const EventId id = mDefinition->FindEvent(name);
        if (id == kInvalidEventId)
        {
            return false;
        }
        DispatchEvent(id);
        return true;
    }

    // Bubbling: the deepest active state of a region sees the event first; a ParallelState only sees it
    // when none of its regions consumed it. Returns true when consumed.
    bool SMManager::DeliverEvent(StateId state, const Event& event)
    {
        const SMState* s = mDefinition->GetState(state);
        if (s == nullptr || mRuntime.activeStates.count(state) == 0)
        {
            return false;
        }
        bool consumed = false;
        switch (s->GetKind())
        {
            case StateKind::Single:
                consumed = DeliverEvent(CurrentChild(state), event);
                break;
            case StateKind::Parallel:
                for (StateId child : s->GetChildren())
                {
                    consumed = DeliverEvent(child, event) || consumed;
                }
                break;
            case StateKind::Leaf:
            default:
                break;
        }
        if (consumed)
        {
            return true;
        }
        return HandleEventAtState(state, event);
    }

    bool SMManager::HandleEventAtState(StateId state, const Event& event)
    {
        SMState* s = mDefinition->GetState(state);
        StateContext ctx(*this, state, mDeltaTime, &event);
        if (s->OnEvent(ctx, event))
        {
            return true;
        }
        for (TransitionId tid : s->GetTransitions())
        {
            const Transition& t = *mDefinition->GetTransition(tid);
            if (t.trigger != event.id)
            {
                continue;
            }
            if (t.CanTrigger(ctx, &event))
            {
                QueueDefined(t, &event);
                return true;
            }
        }
        return false;
    }

    // ---- queries ---------------------------------------------------------------------------------------------
    StateId SMManager::CurrentChild(StateId state) const
    {
        auto it = mRuntime.currentChildren.find(state);
        return it != mRuntime.currentChildren.end() ? it->second : kInvalidStateId;
    }

    std::vector<StateId> SMManager::ActiveLeaves() const
    {
        std::vector<StateId> leaves;
        if (!mRunning)
        {
            return leaves;
        }
        std::vector<StateId> stack{mDefinition->GetRoot()};
        while (!stack.empty())
        {
            const StateId state = stack.back();
            stack.pop_back();
            const SMState* s = mDefinition->GetState(state);
            if (s == nullptr || mRuntime.activeStates.count(state) == 0)
            {
                continue;
            }
            switch (s->GetKind())
            {
                case StateKind::Single:
                    stack.push_back(CurrentChild(state));
                    break;
                case StateKind::Parallel:
                    for (auto it = s->GetChildren().rbegin(); it != s->GetChildren().rend(); ++it)
                    {
                        stack.push_back(*it);
                    }
                    break;
                case StateKind::Leaf:
                default:
                    leaves.push_back(state);
                    break;
            }
        }
        return leaves;
    }

    IStateInstanceData* SMManager::GetStateData(StateId state)
    {
        auto it = mRuntime.stateData.find(state);
        if (it != mRuntime.stateData.end())
        {
            return it->second.get();
        }
        const SMState* s = mDefinition->GetState(state);
        if (s == nullptr)
        {
            return nullptr;
        }
        std::unique_ptr<IStateInstanceData> data = s->CreateRuntimeData();
        IStateInstanceData* raw = data.get();
        if (raw != nullptr)
        {
            mRuntime.stateData[state] = std::move(data);
        }
        return raw;
    }

    void SMManager::SetStateData(StateId state, std::unique_ptr<IStateInstanceData> data)
    {
        if (data)
        {
            mRuntime.stateData[state] = std::move(data);
        }
        else
        {
            mRuntime.stateData.erase(state);
        }
    }

    bool SMManager::RestoreActiveLeaves(const std::vector<StateId>& leaves, std::string& outError)
    {
        if (!mDefinition->IsFinalized())
        {
            outError = "definition is not finalized";
            return false;
        }
        std::set<StateId> active;
        std::map<StateId, StateId> children;
        for (StateId leaf : leaves)
        {
            StateId state = leaf;
            if (mDefinition->GetState(state) == nullptr)
            {
                outError = fmt::format("unknown state id {}", leaf);
                return false;
            }
            while (state != kInvalidStateId)
            {
                active.insert(state);
                const SMState* s = mDefinition->GetState(state);
                const StateId parent = s->GetParent();
                const SMState* p = mDefinition->GetState(parent);
                if (p != nullptr && p->GetKind() == StateKind::Single)
                {
                    auto it = children.find(parent);
                    if (it != children.end() && it->second != state)
                    {
                        outError = fmt::format("SingleState '{}' would have two active children", p->GetName());
                        return false;
                    }
                    children[parent] = state;
                }
                state = parent;
            }
        }
        if (active.count(mDefinition->GetRoot()) == 0)
        {
            outError = "no active leaf under the root";
            return false;
        }
        // Every active composite must be complete: Single has a child, Parallel has all regions.
        for (StateId state : active)
        {
            const SMState* s = mDefinition->GetState(state);
            if (s->GetKind() == StateKind::Single && children.count(state) == 0)
            {
                outError = fmt::format("SingleState '{}' has no active child", s->GetName());
                return false;
            }
            if (s->GetKind() == StateKind::Parallel)
            {
                for (StateId child : s->GetChildren())
                {
                    if (active.count(child) == 0)
                    {
                        outError = fmt::format("region '{}' of ParallelState '{}' is not active",
                                               mDefinition->GetState(child)->GetName(),
                                               s->GetName());
                        return false;
                    }
                }
            }
        }
        mRuntime.activeStates = std::move(active);
        mRuntime.currentChildren = std::move(children);
        mPending.clear();
        mEnteredThisUpdate.clear();
        mRunning = true;
        return true;
    }
} // namespace RAnimation
