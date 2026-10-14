#include <StateMachine/StateTypeRegistry.h>

namespace RAnimation
{
    StateTypeRegistry::StateTypeRegistry()
    {
        RegisterStateType("Leaf",
                          [](const std::string& name, const ParamBag&) { return std::make_unique<SMState>(name); });
        RegisterStateType("Single",
                          [](const std::string& name, const ParamBag&) { return std::make_unique<SingleState>(name); });
        RegisterStateType("Parallel",
                          [](const std::string& name, const ParamBag&)
                          { return std::make_unique<ParallelState>(name); });
    }

    void StateTypeRegistry::RegisterStateType(const std::string& typeName, StateFactory factory)
    {
        mStateTypes[typeName] = std::move(factory);
    }

    void StateTypeRegistry::RegisterGuard(const std::string& name, GuardFn guard)
    {
        mGuards[name] = std::move(guard);
    }

    void StateTypeRegistry::RegisterAction(const std::string& name, ActionFn action)
    {
        mActions[name] = std::move(action);
    }

    bool StateTypeRegistry::HasStateType(const std::string& typeName) const
    {
        return mStateTypes.count(typeName) != 0;
    }

    bool StateTypeRegistry::HasGuard(const std::string& name) const
    {
        return mGuards.count(name) != 0;
    }

    bool StateTypeRegistry::HasAction(const std::string& name) const
    {
        return mActions.count(name) != 0;
    }

    std::unique_ptr<SMState> StateTypeRegistry::CreateState(const std::string& typeName,
                                                            const std::string& name,
                                                            const ParamBag& params) const
    {
        auto it = mStateTypes.find(typeName);
        if (it == mStateTypes.end())
        {
            return nullptr;
        }
        std::unique_ptr<SMState> state = it->second(name, params);
        if (state)
        {
            state->SetTypeName(typeName);
            state->Params() = params;
        }
        return state;
    }

    GuardFn StateTypeRegistry::CreateGuard(const std::string& name) const
    {
        auto it = mGuards.find(name);
        return it != mGuards.end() ? it->second : GuardFn{};
    }

    ActionFn StateTypeRegistry::CreateAction(const std::string& name) const
    {
        auto it = mActions.find(name);
        return it != mActions.end() ? it->second : ActionFn{};
    }
} // namespace RAnimation
