#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <StateMachine/StateMachineDefinition.h>

namespace RAnimation
{
    // Name -> factory tables used to rebuild a definition from its serialized form: state types (by the
    // ranim:stateType name), guards and actions (by name). "Leaf", "Single" and "Parallel" are
    // registered by default and create the plain structural states.
    class StateTypeRegistry
    {
    public:
        using StateFactory = std::function<std::unique_ptr<SMState>(const std::string& name, const ParamBag& params)>;

        StateTypeRegistry();

        void RegisterStateType(const std::string& typeName, StateFactory factory);
        void RegisterGuard(const std::string& name, GuardFn guard);
        void RegisterAction(const std::string& name, ActionFn action);

        bool HasStateType(const std::string& typeName) const;
        bool HasGuard(const std::string& name) const;
        bool HasAction(const std::string& name) const;

        // Creates a state of the registered type (its TypeName is set). Null for unknown types.
        std::unique_ptr<SMState> CreateState(const std::string& typeName,
                                             const std::string& name,
                                             const ParamBag& params) const;
        // Empty std::function for unknown names.
        GuardFn CreateGuard(const std::string& name) const;
        ActionFn CreateAction(const std::string& name) const;

    private:
        std::map<std::string, StateFactory> mStateTypes;
        std::map<std::string, GuardFn> mGuards;
        std::map<std::string, ActionFn> mActions;
    };
} // namespace RAnimation
