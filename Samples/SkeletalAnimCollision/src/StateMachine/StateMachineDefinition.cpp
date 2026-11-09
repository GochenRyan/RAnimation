#include <StateMachine/StateMachineDefinition.h>

#include <algorithm>
#include <cctype>

#include <fmt/format.h>

#include <StateMachine/SMManager.h>

namespace RAnimation
{
    // ---- enum names -------------------------------------------------------------------------------------
    const char* StateKindName(StateKind kind)
    {
        switch (kind)
        {
            case StateKind::Single:
                return "Single";
            case StateKind::Parallel:
                return "Parallel";
            case StateKind::Leaf:
            default:
                return "Leaf";
        }
    }

    bool ParseStateKind(const std::string& name, StateKind& out)
    {
        if (name == "Leaf")
        {
            out = StateKind::Leaf;
            return true;
        }
        if (name == "Single")
        {
            out = StateKind::Single;
            return true;
        }
        if (name == "Parallel")
        {
            out = StateKind::Parallel;
            return true;
        }
        return false;
    }

    const char* CompletionPolicyName(CompletionPolicy policy)
    {
        return policy == CompletionPolicy::Any ? "Any" : "All";
    }

    bool ParseCompletionPolicy(const std::string& name, CompletionPolicy& out)
    {
        if (name == "All")
        {
            out = CompletionPolicy::All;
            return true;
        }
        if (name == "Any")
        {
            out = CompletionPolicy::Any;
            return true;
        }
        return false;
    }

    // ---- composite defaults -----------------------------------------------------------------------------
    bool SingleState::IsDone(StateContext& ctx) const
    {
        SMManager& manager = ctx.Manager();
        const StateId child = manager.CurrentChild(GetId());
        const SMState* childState = manager.Definition().GetState(child);
        if (childState == nullptr)
        {
            return false;
        }
        StateContext childCtx(manager, child, ctx.DeltaTime(), ctx.CurrentEvent());
        return childState->IsDone(childCtx);
    }

    bool ParallelState::IsDone(StateContext& ctx) const
    {
        SMManager& manager = ctx.Manager();
        if (GetChildren().empty())
        {
            return false;
        }
        for (StateId child : GetChildren())
        {
            const SMState* childState = manager.Definition().GetState(child);
            StateContext childCtx(manager, child, ctx.DeltaTime(), ctx.CurrentEvent());
            const bool done = childState != nullptr && childState->IsDone(childCtx);
            if (mPolicy == CompletionPolicy::Any && done)
            {
                return true;
            }
            if (mPolicy == CompletionPolicy::All && !done)
            {
                return false;
            }
        }
        return mPolicy == CompletionPolicy::All;
    }

    // ---- transition ------------------------------------------------------------------------------------
    bool Transition::CanTrigger(StateContext& ctx, const Event* event) const
    {
        if (guard)
        {
            return guard(ctx, event);
        }
        if (trigger != kInvalidEventId)
        {
            return true; // event transition without a guard: the event itself is the condition
        }
        // Completion transition: fires when the source state reports it is done.
        const SMState* state = ctx.Definition().GetState(source);
        return state != nullptr && state->IsDone(ctx);
    }

    void Transition::ExecuteAction(StateContext& ctx) const
    {
        if (action)
        {
            action(ctx);
        }
    }

    // ---- definition --------------------------------------------------------------------------------------
    namespace
    {
        bool IsIdentifier(const std::string& name)
        {
            if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
            {
                return false;
            }
            return std::all_of(name.begin(),
                               name.end(),
                               [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
        }
    } // namespace

    StateId StateMachineDefinition::AddState(std::unique_ptr<SMState> state, StateId parent)
    {
        if (mFinalized || !state)
        {
            return kInvalidStateId;
        }
        if (mStateLookup.count(state->GetName()) != 0)
        {
            return kInvalidStateId;
        }
        SMState* parentState = nullptr;
        if (parent != kInvalidStateId)
        {
            parentState = GetState(parent);
            if (parentState == nullptr || parentState->GetKind() == StateKind::Leaf)
            {
                return kInvalidStateId;
            }
        }

        const StateId id = static_cast<StateId>(mStates.size());
        state->mId = id;
        state->mParent = parent;
        state->mDepth = parentState != nullptr ? parentState->mDepth + 1 : 0;
        mStateLookup[state->GetName()] = id;
        if (parentState != nullptr)
        {
            parentState->mChildren.push_back(id);
        }
        mStates.push_back(std::move(state));
        return id;
    }

    EventId StateMachineDefinition::RegisterEvent(const std::string& name)
    {
        auto it = mEventLookup.find(name);
        if (it != mEventLookup.end())
        {
            return it->second;
        }
        if (mFinalized)
        {
            return kInvalidEventId;
        }
        const EventId id = static_cast<EventId>(mEventNames.size());
        mEventNames.push_back(name);
        mEventLookup[name] = id;
        return id;
    }

    TransitionId StateMachineDefinition::AddTransition(Transition transition)
    {
        if (mFinalized)
        {
            return kInvalidTransitionId;
        }
        transition.id = static_cast<TransitionId>(mTransitions.size());
        if (transition.name.empty())
        {
            const SMState* s = GetState(transition.source);
            const SMState* t = GetState(transition.target);
            transition.name = fmt::format("{}_to_{}", s ? s->GetName() : "?", t ? t->GetName() : "?");
        }
        mTransitions.push_back(std::move(transition));
        return mTransitions.back().id;
    }

    StateId StateMachineDefinition::FindState(std::string_view name) const
    {
        auto it = mStateLookup.find(name);
        return it != mStateLookup.end() ? it->second : kInvalidStateId;
    }

    std::vector<StateId> StateMachineDefinition::TopLevelStates() const
    {
        std::vector<StateId> out;
        for (const auto& s : mStates)
        {
            if (s->GetParent() == kInvalidStateId)
            {
                out.push_back(s->GetId());
            }
        }
        return out;
    }

    EventId StateMachineDefinition::FindEvent(std::string_view name) const
    {
        auto it = mEventLookup.find(name);
        return it != mEventLookup.end() ? it->second : kInvalidEventId;
    }

    const std::string& StateMachineDefinition::EventName(EventId id) const
    {
        static const std::string kUnknown = "?";
        return id < mEventNames.size() ? mEventNames[id] : kUnknown;
    }

    bool StateMachineDefinition::IsAncestor(StateId ancestor, StateId state) const
    {
        while (state != kInvalidStateId)
        {
            if (state == ancestor)
            {
                return true;
            }
            const SMState* s = GetState(state);
            state = s != nullptr ? s->GetParent() : kInvalidStateId;
        }
        return false;
    }

    StateId StateMachineDefinition::LowestCommonAncestor(StateId a, StateId b) const
    {
        const SMState* sa = GetState(a);
        const SMState* sb = GetState(b);
        if (sa == nullptr || sb == nullptr)
        {
            return kInvalidStateId;
        }
        while (sa->GetDepth() > sb->GetDepth())
        {
            sa = GetState(sa->GetParent());
        }
        while (sb->GetDepth() > sa->GetDepth())
        {
            sb = GetState(sb->GetParent());
        }
        while (sa != nullptr && sb != nullptr && sa != sb)
        {
            sa = GetState(sa->GetParent());
            sb = GetState(sb->GetParent());
        }
        return sa != nullptr ? sa->GetId() : kInvalidStateId;
    }

    std::string StateMachineDefinition::StatePath(StateId id) const
    {
        std::string path;
        const SMState* s = GetState(id);
        while (s != nullptr)
        {
            path = path.empty() ? s->GetName() : s->GetName() + "/" + path;
            s = GetState(s->GetParent());
        }
        return path;
    }

    std::vector<std::string> StateMachineDefinition::Validate() const
    {
        std::vector<std::string> errors;

        if (mRoot == kInvalidStateId || GetState(mRoot) == nullptr)
        {
            errors.push_back("no root state");
        }
        else if (GetState(mRoot)->GetParent() != kInvalidStateId)
        {
            errors.push_back(fmt::format("root '{}' must be a top-level state", GetState(mRoot)->GetName()));
        }
        for (StateId top : TopLevelStates())
        {
            if (top != mRoot)
            {
                errors.push_back(
                        fmt::format("state '{}' is not reachable (not under the root)", GetState(top)->GetName()));
            }
        }

        for (const auto& s : mStates)
        {
            if (!IsIdentifier(s->GetName()))
            {
                errors.push_back(
                        fmt::format("state name '{}' is not an identifier (letters, digits, '_')", s->GetName()));
            }
            switch (s->GetKind())
            {
                case StateKind::Single:
                {
                    const auto* single = static_cast<const SingleState*>(s.get());
                    if (s->GetChildren().empty())
                    {
                        errors.push_back(fmt::format("SingleState '{}' has no children", s->GetName()));
                    }
                    const StateId init = single->GetInitialChild();
                    if (init == kInvalidStateId || GetState(init) == nullptr ||
                        GetState(init)->GetParent() != s->GetId())
                    {
                        errors.push_back(fmt::format(
                                "SingleState '{}' needs an initial child that is one of its children", s->GetName()));
                    }
                    break;
                }
                case StateKind::Parallel:
                    if (s->GetChildren().empty())
                    {
                        errors.push_back(fmt::format("ParallelState '{}' has no regions", s->GetName()));
                    }
                    break;
                case StateKind::Leaf:
                default:
                    break;
            }
        }

        for (const Transition& t : mTransitions)
        {
            const SMState* source = GetState(t.source);
            const SMState* target = GetState(t.target);
            if (source == nullptr || target == nullptr)
            {
                errors.push_back(fmt::format("transition '{}' references an unknown state", t.name));
                continue;
            }
            if (t.trigger != kInvalidEventId && t.trigger >= mEventNames.size())
            {
                errors.push_back(fmt::format("transition '{}' uses an unregistered event", t.name));
            }
            // A transition may not cross between two regions of the same ParallelState: both regions are
            // active at once, so "leave region 1, enter region 2" has no meaning. Fire an event and let the
            // other region switch itself instead.
            const StateId lca = LowestCommonAncestor(t.source, t.target);
            const SMState* scope = GetState(lca);
            if (scope != nullptr && scope->GetKind() == StateKind::Parallel && lca != t.source && lca != t.target)
            {
                errors.push_back(fmt::format("transition '{}' crosses two regions of ParallelState '{}' ({} -> {})",
                                             t.name,
                                             scope->GetName(),
                                             source->GetName(),
                                             target->GetName()));
            }
            if (!t.guardName.empty() && !t.guard)
            {
                errors.push_back(
                        fmt::format("transition '{}' names guard '{}' but has no callable", t.name, t.guardName));
            }
            if (!t.actionName.empty() && !t.action)
            {
                errors.push_back(
                        fmt::format("transition '{}' names action '{}' but has no callable", t.name, t.actionName));
            }
        }
        return errors;
    }

    bool StateMachineDefinition::Finalize(std::vector<std::string>* outErrors)
    {
        std::vector<std::string> errors = Validate();
        if (outErrors != nullptr)
        {
            *outErrors = errors;
        }
        if (!errors.empty())
        {
            return false;
        }

        for (auto& s : mStates)
        {
            s->mTransitions.clear();
        }
        for (const Transition& t : mTransitions)
        {
            mStates[t.source]->mTransitions.push_back(t.id);
        }
        for (auto& s : mStates)
        {
            std::stable_sort(s->mTransitions.begin(),
                             s->mTransitions.end(),
                             [this](TransitionId a, TransitionId b)
                             { return mTransitions[a].priority > mTransitions[b].priority; });
        }
        mFinalized = true;
        return true;
    }
} // namespace RAnimation
