#pragma once
#include <memory>

#include <StateMachine/StateMachineTypes.h>

namespace RAnimation
{
    class StateContext;

    // Optional per-instance data of a state. The definition (and therefore the state objects) is shared
    // by every SMManager, so anything mutable per character lives here; it is created lazily through
    // SMState::CreateRuntimeData the first time a state asks for it. States without extra data need none.
    class IStateInstanceData
    {
    public:
        virtual ~IStateInstanceData() = default;
        // Runtime snapshot support (StateMachineSerializer::SerializeRuntime / DeserializeRuntime).
        virtual void Serialize(ParamBag& out) const { (void) out; }
        virtual void Deserialize(const ParamBag& in) { (void) in; }
    };

    // Business callbacks of a state. All of them receive a short-lived StateContext (which manager, which
    // state, access to the instance data / user data / transition requests). Entering and exiting whole
    // sub-trees is orchestrated by the SMManager; a state only ever sees its own OnEnter / OnExit.
    class IState
    {
    public:
        virtual ~IState() = default;

        virtual void OnEnter(StateContext& ctx) { (void) ctx; }
        virtual void OnUpdate(StateContext& ctx, float deltaTime)
        {
            (void) ctx;
            (void) deltaTime;
        }
        virtual void OnExit(StateContext& ctx) { (void) ctx; }
        // Returns true when the event is consumed (bubbling stops; the state's own event transitions are
        // not evaluated either). The default does not consume.
        virtual bool OnEvent(StateContext& ctx, const Event& event)
        {
            (void) ctx;
            (void) event;
            return false;
        }
        // Completion test for trigger-less transitions without a guard ("when this state is done").
        virtual bool IsDone(StateContext& ctx) const
        {
            (void) ctx;
            return false;
        }

        virtual std::unique_ptr<IStateInstanceData> CreateRuntimeData() const { return nullptr; }
    };
} // namespace RAnimation
