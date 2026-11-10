#pragma once
#include <memory>
#include <string>

#include <StateMachine/SMManager.h>
#include <StateMachine/StateMachineDefinition.h>
#include <StateMachine/StateTypeRegistry.h>

// .statemachine.usda read/write (definition) and .smstate.usda (runtime snapshot). pxr-free header; the
// implementation is a USD translation unit like AnimGraphUsdIo.cpp.
//
// Definition - the prim hierarchy is the state hierarchy:
//   #usda 1.0
//   ( defaultPrim = "StateMachine"
//     customLayerData = { dictionary ranim = { int stateMachineVersion = 1  string doc = "..." } } )
//   def Scope "StateMachine" {
//     custom string ranim:name = "character"
//     custom string[] ranim:events = ["Jump", "Punch"]
//     custom rel ranim:root = </StateMachine/States/Root>
//     def Scope "States" {
//       def Scope "Root" { custom string ranim:kind = "Parallel"  custom string ranim:stateType = "Parallel"
//                          custom string ranim:completionPolicy = "All"
//         def Scope "Locomotion" { custom string ranim:kind = "Single"  custom rel ranim:initialChild = <.../Idle>
//           def Scope "Idle" { custom string ranim:kind = "Leaf"  custom string ranim:stateType = "LocomotionState"
//                              custom float ranim:param:blendInput = 0 } ... } } }
//     def Scope "Transitions" {
//       def Scope "T_0" { custom string ranim:name = "Idle_to_Walk"  custom rel ranim:source = <...>
//                         custom rel ranim:target = <...>  custom string ranim:trigger = "Jump"
//                         custom string ranim:guard = "WantMove"  custom string ranim:action = ""
//                         custom int ranim:priority = 0 } } }
//
// Runtime snapshot - which leaves are active plus each state's serialized instance data:
//   def Scope "StateMachineRuntime" { custom string ranim:definition = "character"  custom bool ranim:running = 1
//     custom string[] ranim:activeLeaves = ["Walk", "None"]
//     def Scope "Data" { def Scope "Punch" { custom float ranim:data:elapsed = 0.4 } } }
// Restoring never runs OnEnter / OnExit: the states are made active exactly as they were.

namespace RAnimation
{
    constexpr int kStateMachineFormatVersion = 1;

    class StateMachineSerializer
    {
    public:
        static bool SerializeDefinition(const StateMachineDefinition& definition,
                                        const std::string& doc,
                                        const std::string& path,
                                        std::string& outError);

        // Rebuilds and finalizes the definition; states / guards / actions come from the registry.
        static std::shared_ptr<StateMachineDefinition> DeserializeDefinition(const std::string& path,
                                                                             const StateTypeRegistry& registry,
                                                                             std::string& outDoc,
                                                                             std::string& outError);

        static bool SerializeRuntime(const SMManager& manager, const std::string& path, std::string& outError);

        // The manager must already own the matching (finalized) definition; checks its name.
        static bool DeserializeRuntime(const std::string& path, SMManager& manager, std::string& outError);
    };
} // namespace RAnimation
