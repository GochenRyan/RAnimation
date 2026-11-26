// USD translation unit for the state machine: definition (.statemachine.usda) and runtime snapshot
// (.smstate.usda). Keep every USD header here (see AnimGraphUsdIo.cpp for the same rule).
#include <StateMachine/StateMachineUsdIo.h>

#include <algorithm>
#include <map>
#include <vector>

#include <fmt/format.h>

#include "../Model/UsdPluginRegistration.h"

#include <pxr/pxr.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/scope.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace RAnimation
{
    namespace
    {
        const char* kDefinitionRoot = "/StateMachine";
        const char* kRuntimeRoot = "/StateMachineRuntime";
        const char* kParamPrefix = "ranim:param:";
        const char* kDataPrefix = "ranim:data:";

        template <typename T>
        void SetCustomAttr(UsdPrim& prim, const std::string& name, const SdfValueTypeName& type, const T& value)
        {
            UsdAttribute attr = prim.CreateAttribute(TfToken(name), type, /*custom*/ true);
            attr.Set(VtValue(value));
        }

        template <typename T> bool GetAttr(const UsdPrim& prim, const std::string& name, T& out)
        {
            UsdAttribute attr = prim.GetAttribute(TfToken(name));
            return attr && attr.Get(&out);
        }

        void SetRel(UsdPrim& prim, const char* name, const SdfPath& target)
        {
            UsdRelationship rel = prim.CreateRelationship(TfToken(name), /*custom*/ true);
            rel.SetTargets({target});
        }

        bool GetRelTarget(const UsdPrim& prim, const char* name, SdfPath& out)
        {
            UsdRelationship rel = prim.GetRelationship(TfToken(name));
            if (!rel)
            {
                return false;
            }
            SdfPathVector targets;
            rel.GetTargets(&targets);
            if (targets.empty())
            {
                return false;
            }
            out = targets.front();
            return true;
        }

        // ParamBag <-> prefixed custom attributes (one attribute per entry, typed by the variant).
        void WriteBag(UsdPrim& prim, const char* prefix, const ParamBag& bag)
        {
            for (const auto& [key, value] : bag.Values())
            {
                const std::string name = std::string(prefix) + key;
                std::visit(
                        [&](const auto& v)
                        {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, bool>)
                            {
                                SetCustomAttr(prim, name, SdfValueTypeNames->Bool, v);
                            }
                            else if constexpr (std::is_same_v<T, int>)
                            {
                                SetCustomAttr(prim, name, SdfValueTypeNames->Int, v);
                            }
                            else if constexpr (std::is_same_v<T, float>)
                            {
                                SetCustomAttr(prim, name, SdfValueTypeNames->Float, v);
                            }
                            else
                            {
                                SetCustomAttr(prim, name, SdfValueTypeNames->String, v);
                            }
                        },
                        value);
            }
        }

        void ReadBag(const UsdPrim& prim, const char* prefix, ParamBag& bag)
        {
            const std::string pre(prefix);
            for (const UsdAttribute& attr : prim.GetAttributes())
            {
                const std::string name = attr.GetName().GetString();
                if (name.rfind(pre, 0) != 0)
                {
                    continue;
                }
                VtValue value;
                if (!attr.Get(&value))
                {
                    continue;
                }
                const std::string key = name.substr(pre.size());
                if (value.IsHolding<bool>())
                {
                    bag.Set(key, value.Get<bool>());
                }
                else if (value.IsHolding<int>())
                {
                    bag.Set(key, value.Get<int>());
                }
                else if (value.IsHolding<float>())
                {
                    bag.Set(key, value.Get<float>());
                }
                else if (value.IsHolding<double>())
                {
                    bag.Set(key, static_cast<float>(value.Get<double>()));
                }
                else if (value.IsHolding<std::string>())
                {
                    bag.Set(key, value.Get<std::string>());
                }
            }
        }

        SdfPath StatePrimPath(const StateMachineDefinition& def, StateId id)
        {
            return SdfPath(std::string(kDefinitionRoot) + "/States/" + def.StatePath(id));
        }

        void WriteStateRecursive(UsdStageRefPtr& stage, const StateMachineDefinition& def, StateId id)
        {
            const SMState* s = def.GetState(id);
            UsdPrim prim = UsdGeomScope::Define(stage, StatePrimPath(def, id)).GetPrim();
            SetCustomAttr(prim, "ranim:kind", SdfValueTypeNames->String, std::string(StateKindName(s->GetKind())));
            SetCustomAttr(prim,
                          "ranim:stateType",
                          SdfValueTypeNames->String,
                          s->GetTypeName().empty() ? std::string(StateKindName(s->GetKind())) : s->GetTypeName());
            // Region / child order matters (parallel update order, event delivery); do not rely on prim order.
            const SMState* parent = def.GetState(s->GetParent());
            int order = 0;
            if (parent != nullptr)
            {
                const auto& siblings = parent->GetChildren();
                order = static_cast<int>(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
            }
            SetCustomAttr(prim, "ranim:order", SdfValueTypeNames->Int, order);
            if (s->GetKind() == StateKind::Single)
            {
                const StateId init = static_cast<const SingleState*>(s)->GetInitialChild();
                if (init != kInvalidStateId)
                {
                    SetRel(prim, "ranim:initialChild", StatePrimPath(def, init));
                }
            }
            if (s->GetKind() == StateKind::Parallel)
            {
                SetCustomAttr(prim,
                              "ranim:completionPolicy",
                              SdfValueTypeNames->String,
                              std::string(CompletionPolicyName(
                                      static_cast<const ParallelState*>(s)->GetCompletionPolicy())));
            }
            WriteBag(prim, kParamPrefix, s->GetParams());
            for (StateId child : s->GetChildren())
            {
                WriteStateRecursive(stage, def, child);
            }
        }

        struct PendingInitial
        {
            StateId state;
            SdfPath child;
        };

        bool ReadStateRecursive(const UsdPrim& prim,
                                StateId parent,
                                const StateTypeRegistry& registry,
                                StateMachineDefinition& def,
                                std::map<SdfPath, StateId>& idByPath,
                                std::vector<PendingInitial>& initials,
                                std::string& outError)
        {
            std::string kindName = "Leaf";
            GetAttr(prim, "ranim:kind", kindName);
            StateKind kind;
            if (!ParseStateKind(kindName, kind))
            {
                outError = fmt::format("state '{}' has unknown kind '{}'", prim.GetName().GetString(), kindName);
                return false;
            }
            std::string typeName = StateKindName(kind);
            GetAttr(prim, "ranim:stateType", typeName);
            ParamBag params;
            ReadBag(prim, kParamPrefix, params);

            std::unique_ptr<SMState> state = registry.CreateState(typeName, prim.GetName().GetString(), params);
            if (!state)
            {
                outError = fmt::format("state '{}' has unregistered type '{}'", prim.GetName().GetString(), typeName);
                return false;
            }
            if (state->GetKind() != kind)
            {
                outError = fmt::format("state '{}': type '{}' creates a {} state but the file says {}",
                                       prim.GetName().GetString(),
                                       typeName,
                                       StateKindName(state->GetKind()),
                                       kindName);
                return false;
            }
            if (kind == StateKind::Parallel)
            {
                std::string policyName;
                CompletionPolicy policy = CompletionPolicy::All;
                if (GetAttr(prim, "ranim:completionPolicy", policyName) && !ParseCompletionPolicy(policyName, policy))
                {
                    outError = fmt::format("state '{}' has unknown completion policy '{}'",
                                           prim.GetName().GetString(),
                                           policyName);
                    return false;
                }
                static_cast<ParallelState*>(state.get())->SetCompletionPolicy(policy);
            }
            SdfPath initial;
            const bool hasInitial = kind == StateKind::Single && GetRelTarget(prim, "ranim:initialChild", initial);

            const StateId id = def.AddState(std::move(state), parent);
            if (id == kInvalidStateId)
            {
                outError = fmt::format("state '{}' could not be added (duplicate name or leaf parent)",
                                       prim.GetName().GetString());
                return false;
            }
            idByPath[prim.GetPath()] = id;
            if (hasInitial)
            {
                initials.push_back({id, initial});
            }

            std::vector<UsdPrim> children;
            for (const UsdPrim& child : prim.GetChildren())
            {
                children.push_back(child);
            }
            std::stable_sort(children.begin(),
                             children.end(),
                             [](const UsdPrim& a, const UsdPrim& b)
                             {
                                 int oa = 0, ob = 0;
                                 GetAttr(a, "ranim:order", oa);
                                 GetAttr(b, "ranim:order", ob);
                                 return oa < ob;
                             });
            for (const UsdPrim& child : children)
            {
                if (!ReadStateRecursive(child, id, registry, def, idByPath, initials, outError))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool StateMachineSerializer::SerializeDefinition(const StateMachineDefinition& definition,
                                                     const std::string& doc,
                                                     const std::string& path,
                                                     std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        if (definition.GetRoot() == kInvalidStateId)
        {
            outError = "definition has no root";
            return false;
        }

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        if (!stage)
        {
            outError = "could not create a USD stage";
            return false;
        }
        UsdPrim root = UsdGeomScope::Define(stage, SdfPath(kDefinitionRoot)).GetPrim();
        stage->SetDefaultPrim(root);

        VtDictionary ranim;
        ranim["stateMachineVersion"] = VtValue(kStateMachineFormatVersion);
        ranim["doc"] = VtValue(doc);
        VtDictionary layerData;
        layerData["ranim"] = VtValue(ranim);
        stage->GetRootLayer()->SetCustomLayerData(layerData);

        SetCustomAttr(root, "ranim:name", SdfValueTypeNames->String, definition.Name);
        VtStringArray events;
        for (EventId i = 0; i < definition.EventCount(); ++i)
        {
            events.push_back(definition.EventName(i));
        }
        SetCustomAttr(root, "ranim:events", SdfValueTypeNames->StringArray, events);
        SetRel(root, "ranim:root", StatePrimPath(definition, definition.GetRoot()));

        UsdGeomScope::Define(stage, SdfPath(std::string(kDefinitionRoot) + "/States"));
        WriteStateRecursive(stage, definition, definition.GetRoot());

        UsdGeomScope::Define(stage, SdfPath(std::string(kDefinitionRoot) + "/Transitions"));
        for (TransitionId i = 0; i < definition.TransitionCount(); ++i)
        {
            const Transition& t = *definition.GetTransition(i);
            UsdPrim prim = UsdGeomScope::Define(stage, SdfPath(fmt::format("{}/Transitions/T_{}", kDefinitionRoot, i)))
                                   .GetPrim();
            SetCustomAttr(prim, "ranim:name", SdfValueTypeNames->String, t.name);
            SetRel(prim, "ranim:source", StatePrimPath(definition, t.source));
            SetRel(prim, "ranim:target", StatePrimPath(definition, t.target));
            if (t.trigger != kInvalidEventId)
            {
                SetCustomAttr(prim, "ranim:trigger", SdfValueTypeNames->String, definition.EventName(t.trigger));
            }
            if (!t.guardName.empty())
            {
                SetCustomAttr(prim, "ranim:guard", SdfValueTypeNames->String, t.guardName);
            }
            if (!t.actionName.empty())
            {
                SetCustomAttr(prim, "ranim:action", SdfValueTypeNames->String, t.actionName);
            }
            SetCustomAttr(prim, "ranim:priority", SdfValueTypeNames->Int, t.priority);
        }

        if (!stage->GetRootLayer()->Export(path))
        {
            outError = fmt::format("could not write '{}'", path);
            return false;
        }
        return true;
    }

    std::shared_ptr<StateMachineDefinition> StateMachineSerializer::DeserializeDefinition(
            const std::string& path, const StateTypeRegistry& registry, std::string& outDoc, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        outDoc.clear();

        UsdStageRefPtr stage = UsdStage::Open(path);
        if (!stage)
        {
            outError = fmt::format("could not open '{}'", path);
            return nullptr;
        }
        const VtDictionary layerData = stage->GetRootLayer()->GetCustomLayerData();
        const VtValue* version = layerData.GetValueAtPath("ranim:stateMachineVersion");
        if (version == nullptr || !version->IsHolding<int>())
        {
            outError = fmt::format("'{}' is not a statemachine layer (missing ranim.stateMachineVersion)", path);
            return nullptr;
        }
        if (version->Get<int>() != kStateMachineFormatVersion)
        {
            outError = fmt::format("'{}' has statemachine version {} (expected {})",
                                   path,
                                   version->Get<int>(),
                                   kStateMachineFormatVersion);
            return nullptr;
        }
        if (const VtValue* doc = layerData.GetValueAtPath("ranim:doc"); doc != nullptr && doc->IsHolding<std::string>())
        {
            outDoc = doc->Get<std::string>();
        }

        UsdPrim root = stage->GetPrimAtPath(SdfPath(kDefinitionRoot));
        if (!root)
        {
            outError = fmt::format("'{}' has no /StateMachine prim", path);
            return nullptr;
        }

        auto def = std::make_shared<StateMachineDefinition>();
        GetAttr(root, "ranim:name", def->Name);
        VtStringArray events;
        if (GetAttr(root, "ranim:events", events))
        {
            for (const std::string& e : events)
            {
                def->RegisterEvent(e);
            }
        }

        UsdPrim statesScope = stage->GetPrimAtPath(SdfPath(std::string(kDefinitionRoot) + "/States"));
        if (!statesScope)
        {
            outError = fmt::format("'{}' has no /StateMachine/States prim", path);
            return nullptr;
        }
        std::map<SdfPath, StateId> idByPath;
        std::vector<PendingInitial> initials;
        for (const UsdPrim& top : statesScope.GetChildren())
        {
            if (!ReadStateRecursive(top, kInvalidStateId, registry, *def, idByPath, initials, outError))
            {
                return nullptr;
            }
        }
        for (const PendingInitial& p : initials)
        {
            auto it = idByPath.find(p.child);
            if (it == idByPath.end())
            {
                outError = fmt::format("state '{}' names an unknown initial child '{}'",
                                       def->GetState(p.state)->GetName(),
                                       p.child.GetString());
                return nullptr;
            }
            static_cast<SingleState*>(def->GetState(p.state))->SetInitialChild(it->second);
        }

        SdfPath rootPath;
        if (GetRelTarget(root, "ranim:root", rootPath))
        {
            auto it = idByPath.find(rootPath);
            if (it == idByPath.end())
            {
                outError = fmt::format("'{}' names an unknown root state", path);
                return nullptr;
            }
            def->SetRoot(it->second);
        }
        else if (!def->TopLevelStates().empty())
        {
            def->SetRoot(def->TopLevelStates().front());
        }

        UsdPrim transitions = stage->GetPrimAtPath(SdfPath(std::string(kDefinitionRoot) + "/Transitions"));
        if (transitions)
        {
            // T_<i> prims: restore authoring order by index (prim order may be alphabetical).
            std::map<int, UsdPrim> byIndex;
            for (const UsdPrim& child : transitions.GetChildren())
            {
                const std::string name = child.GetName().GetString();
                if (name.rfind("T_", 0) == 0)
                {
                    byIndex[std::atoi(name.c_str() + 2)] = child;
                }
            }
            for (const auto& [index, prim] : byIndex)
            {
                Transition t;
                GetAttr(prim, "ranim:name", t.name);
                SdfPath source, target;
                if (!GetRelTarget(prim, "ranim:source", source) || !GetRelTarget(prim, "ranim:target", target) ||
                    idByPath.count(source) == 0 || idByPath.count(target) == 0)
                {
                    outError = fmt::format("transition '{}' references an unknown state", t.name);
                    return nullptr;
                }
                t.source = idByPath[source];
                t.target = idByPath[target];
                std::string trigger;
                if (GetAttr(prim, "ranim:trigger", trigger) && !trigger.empty())
                {
                    t.trigger = def->RegisterEvent(trigger);
                }
                GetAttr(prim, "ranim:guard", t.guardName);
                GetAttr(prim, "ranim:action", t.actionName);
                GetAttr(prim, "ranim:priority", t.priority);
                if (!t.guardName.empty())
                {
                    t.guard = registry.CreateGuard(t.guardName);
                    if (!t.guard)
                    {
                        outError = fmt::format("transition '{}' uses unregistered guard '{}'", t.name, t.guardName);
                        return nullptr;
                    }
                }
                if (!t.actionName.empty())
                {
                    t.action = registry.CreateAction(t.actionName);
                    if (!t.action)
                    {
                        outError = fmt::format("transition '{}' uses unregistered action '{}'", t.name, t.actionName);
                        return nullptr;
                    }
                }
                def->AddTransition(std::move(t));
            }
        }

        std::vector<std::string> errors;
        if (!def->Finalize(&errors))
        {
            outError = fmt::format("'{}' is not a valid state machine: {}", path, errors.front());
            return nullptr;
        }
        return def;
    }

    bool StateMachineSerializer::SerializeRuntime(const SMManager& manager,
                                                  const std::string& path,
                                                  std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        const StateMachineDefinition& def = manager.Definition();

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        if (!stage)
        {
            outError = "could not create a USD stage";
            return false;
        }
        UsdPrim root = UsdGeomScope::Define(stage, SdfPath(kRuntimeRoot)).GetPrim();
        stage->SetDefaultPrim(root);

        VtDictionary ranim;
        ranim["stateMachineVersion"] = VtValue(kStateMachineFormatVersion);
        VtDictionary layerData;
        layerData["ranim"] = VtValue(ranim);
        stage->GetRootLayer()->SetCustomLayerData(layerData);

        SetCustomAttr(root, "ranim:definition", SdfValueTypeNames->String, def.Name);
        SetCustomAttr(root, "ranim:running", SdfValueTypeNames->Bool, manager.IsRunning());
        VtStringArray leaves;
        for (StateId leaf : manager.ActiveLeaves())
        {
            leaves.push_back(def.GetState(leaf)->GetName());
        }
        SetCustomAttr(root, "ranim:activeLeaves", SdfValueTypeNames->StringArray, leaves);

        UsdGeomScope::Define(stage, SdfPath(std::string(kRuntimeRoot) + "/Data"));
        for (const auto& [stateId, data] : manager.Runtime().stateData)
        {
            ParamBag bag;
            data->Serialize(bag);
            if (bag.Values().empty())
            {
                continue;
            }
            UsdPrim prim = UsdGeomScope::Define(stage,
                                                SdfPath(std::string(kRuntimeRoot) + "/Data/" +
                                                        def.GetState(stateId)->GetName()))
                                   .GetPrim();
            WriteBag(prim, kDataPrefix, bag);
        }

        if (!stage->GetRootLayer()->Export(path))
        {
            outError = fmt::format("could not write '{}'", path);
            return false;
        }
        return true;
    }

    bool StateMachineSerializer::DeserializeRuntime(const std::string& path, SMManager& manager, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        const StateMachineDefinition& def = manager.Definition();

        UsdStageRefPtr stage = UsdStage::Open(path);
        if (!stage)
        {
            outError = fmt::format("could not open '{}'", path);
            return false;
        }
        UsdPrim root = stage->GetPrimAtPath(SdfPath(kRuntimeRoot));
        if (!root)
        {
            outError = fmt::format("'{}' has no /StateMachineRuntime prim", path);
            return false;
        }
        std::string definitionName;
        GetAttr(root, "ranim:definition", definitionName);
        if (definitionName != def.Name)
        {
            outError = fmt::format(
                    "'{}' was saved for definition '{}', the manager runs '{}'", path, definitionName, def.Name);
            return false;
        }

        bool running = true;
        GetAttr(root, "ranim:running", running);
        if (!running)
        {
            manager.Stop();
            return true;
        }

        VtStringArray leafNames;
        GetAttr(root, "ranim:activeLeaves", leafNames);
        std::vector<StateId> leaves;
        for (const std::string& name : leafNames)
        {
            const StateId id = def.FindState(name);
            if (id == kInvalidStateId)
            {
                outError = fmt::format("'{}' names an unknown state '{}'", path, name);
                return false;
            }
            leaves.push_back(id);
        }
        if (!manager.RestoreActiveLeaves(leaves, outError))
        {
            return false;
        }

        UsdPrim dataScope = stage->GetPrimAtPath(SdfPath(std::string(kRuntimeRoot) + "/Data"));
        if (dataScope)
        {
            for (const UsdPrim& prim : dataScope.GetChildren())
            {
                const StateId id = def.FindState(prim.GetName().GetString());
                if (id == kInvalidStateId)
                {
                    continue;
                }
                ParamBag bag;
                ReadBag(prim, kDataPrefix, bag);
                if (IStateInstanceData* data = manager.GetStateData(id))
                {
                    data->Deserialize(bag);
                }
            }
        }
        return true;
    }
} // namespace RAnimation
