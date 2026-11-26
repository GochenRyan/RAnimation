// USD translation unit of the behaviour module: .behaviortree.usda (§8.1), global.blackboard.usda and the
// checkpoint file .behaviorsave.usda (§8.4). Keep every pxr header here.
//
// Key names may contain '.', which USD identifiers do not allow: prim / attribute names use an escaped form
// (any character outside [A-Za-z0-9_] becomes _xHH_) and key prims also carry their original name.

#include <Behavior/BehaviorSystem.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>

#include <fmt/format.h>

#include "../Model/UsdPluginRegistration.h"

#include <pxr/pxr.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
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
        constexpr int kBehaviorTreeVersion = 1;
        constexpr int kSaveVersion = 1;

        std::string Escape(const std::string& name)
        {
            std::string out;
            for (unsigned char c : name)
            {
                if (std::isalnum(c) || c == '_')
                {
                    out += static_cast<char>(c);
                }
                else
                {
                    out += fmt::format("_x{:02X}_", static_cast<unsigned>(c));
                }
            }
            if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())))
            {
                out = "_" + out;
            }
            return out;
        }

        std::string Unescape(const std::string& name)
        {
            std::string out;
            size_t i = (name.size() > 1 && name[0] == '_' && std::isdigit(static_cast<unsigned char>(name[1]))) ? 1 : 0;
            for (; i < name.size(); ++i)
            {
                if (name[i] == '_' && i + 4 < name.size() && name[i + 1] == 'x' && name[i + 4] == '_' &&
                    std::isxdigit(static_cast<unsigned char>(name[i + 2])) && std::isxdigit(static_cast<unsigned char>(name[i + 3])))
                {
                    out += static_cast<char>(std::stoi(name.substr(i + 2, 2), nullptr, 16));
                    i += 4;
                }
                else
                {
                    out += name[i];
                }
            }
            return out;
        }

        void SetAttr(const UsdPrim& prim, const std::string& name, const SdfValueTypeName& type, const VtValue& value)
        {
            prim.CreateAttribute(TfToken(name), type, /*custom*/ true).Set(value);
        }

        template <typename T> bool GetAttr(const UsdPrim& prim, const std::string& name, T& out)
        {
            const UsdAttribute attr = prim.GetAttribute(TfToken(name));
            return attr && attr.Get(&out);
        }

        // BbValue <-> typed attribute
        void SetValueAttr(const UsdPrim& prim, const std::string& name, const BbValue& v)
        {
            switch (TypeOf(v))
            {
                case BbType::Bool: SetAttr(prim, name, SdfValueTypeNames->Bool, VtValue(std::get<bool>(v))); break;
                case BbType::Int: SetAttr(prim, name, SdfValueTypeNames->Int, VtValue(std::get<int>(v))); break;
                case BbType::Float: SetAttr(prim, name, SdfValueTypeNames->Float, VtValue(std::get<float>(v))); break;
                case BbType::Vec3:
                {
                    const glm::vec3& p = std::get<glm::vec3>(v);
                    SetAttr(prim, name, SdfValueTypeNames->Float3, VtValue(GfVec3f(p.x, p.y, p.z)));
                    break;
                }
                case BbType::String: SetAttr(prim, name, SdfValueTypeNames->String, VtValue(std::get<std::string>(v))); break;
                case BbType::Entity:
                    SetAttr(prim, name, SdfValueTypeNames->UInt64, VtValue(static_cast<uint64_t>(std::get<EntityId>(v))));
                    break;
            }
        }

        bool ValueFromVt(const VtValue& v, BbValue& out)
        {
            if (v.IsHolding<bool>())
                out = v.Get<bool>();
            else if (v.IsHolding<int>())
                out = v.Get<int>();
            else if (v.IsHolding<float>())
                out = v.Get<float>();
            else if (v.IsHolding<double>())
                out = static_cast<float>(v.Get<double>());
            else if (v.IsHolding<GfVec3f>())
            {
                const GfVec3f g = v.Get<GfVec3f>();
                out = glm::vec3(g[0], g[1], g[2]);
            }
            else if (v.IsHolding<std::string>())
                out = v.Get<std::string>();
            else if (v.IsHolding<uint64_t>())
                out = static_cast<EntityId>(v.Get<uint64_t>());
            else if (v.IsHolding<int64_t>())
                out = static_cast<EntityId>(v.Get<int64_t>());
            else if (v.IsHolding<unsigned int>())
                out = static_cast<int>(v.Get<unsigned int>());
            else
                return false;
            return true;
        }

        void WriteKeys(const UsdStageRefPtr& stage, const SdfPath& scopePath, const std::vector<BlackboardKeyDecl>& keys)
        {
            UsdGeomScope::Define(stage, scopePath);
            for (const BlackboardKeyDecl& k : keys)
            {
                UsdPrim p = UsdGeomScope::Define(stage, scopePath.AppendChild(TfToken(Escape(k.name)))).GetPrim();
                SetAttr(p, "ranim:name", SdfValueTypeNames->String, VtValue(k.name));
                SetAttr(p, "ranim:type", SdfValueTypeNames->String, VtValue(std::string(BbTypeName(k.type))));
                SetAttr(p, "ranim:scope", SdfValueTypeNames->String, VtValue(std::string(BbScopeName(k.scope))));
                BbValue def;
                if (!ConvertValue(k.defaultValue, k.type, def))
                {
                    def = DefaultValueFor(k.type);
                }
                SetValueAttr(p, "ranim:default", def);
                SetAttr(p, "ranim:wakeOnChange", SdfValueTypeNames->Bool, VtValue(k.wakeOnChange));
            }
        }

        bool ReadKeys(const UsdPrim& scope, BbScope defaultScope, std::vector<BlackboardKeyDecl>& out, std::string& outError)
        {
            if (!scope)
            {
                return true;
            }
            for (const UsdPrim& p : scope.GetChildren())
            {
                BlackboardKeyDecl k;
                if (!GetAttr(p, "ranim:name", k.name))
                {
                    k.name = Unescape(p.GetName().GetString());
                }
                std::string type = "Float";
                std::string scopeName;
                GetAttr(p, "ranim:type", type);
                if (!ParseBbType(type, k.type))
                {
                    outError = fmt::format("key '{}': unknown type '{}'", k.name, type);
                    return false;
                }
                k.scope = defaultScope;
                if (GetAttr(p, "ranim:scope", scopeName) && !ParseBbScope(scopeName, k.scope))
                {
                    outError = fmt::format("key '{}': unknown scope '{}'", k.name, scopeName);
                    return false;
                }
                VtValue def;
                BbValue v;
                const UsdAttribute attr = p.GetAttribute(TfToken("ranim:default"));
                if (attr && attr.Get(&def) && ValueFromVt(def, v) && ConvertValue(v, k.type, k.defaultValue))
                {
                }
                else
                {
                    k.defaultValue = DefaultValueFor(k.type);
                }
                GetAttr(p, "ranim:wakeOnChange", k.wakeOnChange);
                out.push_back(std::move(k));
            }
            return true;
        }

        void SetCustomLayerData(const UsdStageRefPtr& stage, const char* versionKey, int version, const std::string& doc)
        {
            VtDictionary ranim;
            ranim[versionKey] = VtValue(version);
            if (!doc.empty())
            {
                ranim["doc"] = VtValue(doc);
            }
            VtDictionary layerData;
            layerData["ranim"] = VtValue(ranim);
            stage->GetRootLayer()->SetCustomLayerData(layerData);
        }

        bool Export(const UsdStageRefPtr& stage, const std::string& path, std::string& outError)
        {
            std::error_code ec;
            const std::filesystem::path dir = std::filesystem::path(path).parent_path();
            if (!dir.empty())
            {
                std::filesystem::create_directories(dir, ec);
            }
            if (!stage->GetRootLayer()->Export(path))
            {
                outError = fmt::format("could not write '{}'", path);
                return false;
            }
            return true;
        }
    } // namespace

    // ---- .behaviortree.usda ------------------------------------------------------------------------------

    bool WriteTreeDesc(const TreeDesc& desc, const NodeTypeRegistry& types, const std::string& path, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        const SdfPath root("/BehaviorTree");
        UsdPrim treePrim = UsdGeomScope::Define(stage, root).GetPrim();
        stage->SetDefaultPrim(treePrim);
        SetCustomLayerData(stage, "behaviorTreeVersion", kBehaviorTreeVersion, desc.doc);
        SetAttr(treePrim, "ranim:name", SdfValueTypeNames->String, VtValue(desc.name));
        SetAttr(treePrim, "ranim:tickHz", SdfValueTypeNames->Float, VtValue(desc.tickHz));
        SetAttr(treePrim, "ranim:nextId", SdfValueTypeNames->UInt, VtValue(static_cast<unsigned int>(desc.nextId)));
        const SdfPath nodesPath = root.AppendChild(TfToken("Nodes"));
        auto nodePath = [&nodesPath](uint32_t id) { return nodesPath.AppendChild(TfToken("N" + std::to_string(id))); };
        treePrim.CreateRelationship(TfToken("ranim:root"), true).SetTargets({nodePath(desc.rootId)});

        WriteKeys(stage, root.AppendChild(TfToken("Blackboard")), desc.keys);
        UsdGeomScope::Define(stage, nodesPath);
        for (const BtNodeDesc& n : desc.nodes)
        {
            UsdPrim p = UsdGeomScope::Define(stage, nodePath(n.id)).GetPrim();
            SetAttr(p, "ranim:type", SdfValueTypeNames->String, VtValue(n.type));
            SetAttr(p, "ranim:id", SdfValueTypeNames->UInt, VtValue(static_cast<unsigned int>(n.id)));
            if (!n.label.empty())
            {
                SetAttr(p, "ranim:label", SdfValueTypeNames->String, VtValue(n.label));
            }
            SetAttr(p, "ranim:editorPos", SdfValueTypeNames->Float2, VtValue(GfVec2f(n.editorPos.x, n.editorPos.y)));
            if (!n.children.empty())
            {
                SdfPathVector targets;
                for (uint32_t c : n.children)
                {
                    targets.push_back(nodePath(c));
                }
                p.CreateRelationship(TfToken("ranim:children"), true).SetTargets(targets);
            }
            const NodeType* type = types.Find(n.type);
            for (const ParamDesc& param : n.params)
            {
                // drop parameters the type no longer has
                if (type != nullptr && type->ParamIndex(param.name) < 0)
                {
                    continue;
                }
                const std::string name = "ranim:param:" + param.name;
                if (param.bound)
                {
                    SetAttr(p, name, SdfValueTypeNames->String, VtValue("@" + param.key));
                }
                else if (const std::string* s = std::get_if<std::string>(&param.literal); s != nullptr && !s->empty() && s->front() == '@')
                {
                    SetAttr(p, name, SdfValueTypeNames->String, VtValue("@" + *s)); // "@@..." = literal starting with @
                }
                else
                {
                    // store a fixed-type literal with its declared type ("true" -> bool), others as authored
                    BbValue literal = param.literal;
                    const int index = type != nullptr ? type->ParamIndex(param.name) : -1;
                    if (index >= 0)
                    {
                        const ParamSpec& spec = type->params[static_cast<size_t>(index)];
                        BbValue converted;
                        if (spec.kind == ParamKind::Value && spec.sameTypeAs < 0 &&
                            (ConvertValue(literal, spec.type, converted) ||
                             (std::holds_alternative<std::string>(literal) && ValueFromString(spec.type, std::get<std::string>(literal), converted))))
                        {
                            literal = converted;
                        }
                    }
                    SetValueAttr(p, name, literal);
                }
            }
            if (!n.nodeKeys.empty())
            {
                WriteKeys(stage, nodePath(n.id).AppendChild(TfToken("NodeKeys")), n.nodeKeys);
            }
        }
        return Export(stage, path, outError);
    }

    bool ReadTreeDesc(const std::string& path, TreeDesc& out, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        UsdStageRefPtr stage = UsdStage::Open(path);
        if (!stage)
        {
            outError = fmt::format("could not open '{}'", path);
            return false;
        }
        const UsdPrim treePrim = stage->GetPrimAtPath(SdfPath("/BehaviorTree"));
        if (!treePrim)
        {
            outError = fmt::format("'{}' has no /BehaviorTree prim", path);
            return false;
        }
        out = TreeDesc{};
        const VtDictionary layerData = stage->GetRootLayer()->GetCustomLayerData();
        if (auto it = layerData.find("ranim"); it != layerData.end() && it->second.IsHolding<VtDictionary>())
        {
            const VtDictionary& ranim = it->second.Get<VtDictionary>();
            if (auto d = ranim.find("doc"); d != ranim.end() && d->second.IsHolding<std::string>())
            {
                out.doc = d->second.Get<std::string>();
            }
        }
        GetAttr(treePrim, "ranim:name", out.name);
        if (out.name.empty())
        {
            out.name = std::filesystem::path(path).stem().stem().generic_string();
        }
        GetAttr(treePrim, "ranim:tickHz", out.tickHz);
        unsigned int nextId = 0;
        GetAttr(treePrim, "ranim:nextId", nextId);
        if (!ReadKeys(stage->GetPrimAtPath(SdfPath("/BehaviorTree/Blackboard")), BbScope::Instance, out.keys, outError))
        {
            return false;
        }
        std::map<SdfPath, uint32_t> idByPath;
        const UsdPrim nodes = stage->GetPrimAtPath(SdfPath("/BehaviorTree/Nodes"));
        if (!nodes)
        {
            outError = fmt::format("'{}' has no /BehaviorTree/Nodes", path);
            return false;
        }
        for (const UsdPrim& p : nodes.GetChildren())
        {
            unsigned int id = 0;
            if (!GetAttr(p, "ranim:id", id))
            {
                outError = fmt::format("'{}': node {} has no ranim:id", path, p.GetPath().GetString());
                return false;
            }
            idByPath[p.GetPath()] = id;
        }
        for (const UsdPrim& p : nodes.GetChildren())
        {
            BtNodeDesc n;
            n.id = idByPath[p.GetPath()];
            GetAttr(p, "ranim:type", n.type);
            GetAttr(p, "ranim:label", n.label);
            GfVec2f pos(0.0f);
            if (GetAttr(p, "ranim:editorPos", pos))
            {
                n.editorPos = glm::vec2(pos[0], pos[1]);
            }
            if (const UsdRelationship rel = p.GetRelationship(TfToken("ranim:children")))
            {
                SdfPathVector targets;
                rel.GetTargets(&targets);
                for (const SdfPath& t : targets)
                {
                    const auto it = idByPath.find(t);
                    if (it == idByPath.end())
                    {
                        outError = fmt::format("'{}': node {} links to missing {}", path, n.id, t.GetString());
                        return false;
                    }
                    n.children.push_back(it->second);
                }
            }
            for (const UsdAttribute& attr : p.GetAttributes())
            {
                const std::string name = attr.GetName().GetString();
                const std::string prefix = "ranim:param:";
                if (name.rfind(prefix, 0) != 0)
                {
                    continue;
                }
                ParamDesc param;
                param.name = name.substr(prefix.size());
                VtValue v;
                if (!attr.Get(&v) || !ValueFromVt(v, param.literal))
                {
                    continue;
                }
                if (const std::string* s = std::get_if<std::string>(&param.literal); s != nullptr && !s->empty() && s->front() == '@')
                {
                    if (s->size() > 1 && (*s)[1] == '@')
                    {
                        param.literal = s->substr(1);
                    }
                    else
                    {
                        param.bound = true;
                        param.key = s->substr(1);
                    }
                }
                n.params.push_back(std::move(param));
            }
            if (!ReadKeys(p.GetChild(TfToken("NodeKeys")), BbScope::Node, n.nodeKeys, outError))
            {
                return false;
            }
            for (BlackboardKeyDecl& k : n.nodeKeys)
            {
                k.scope = BbScope::Node;
                k.ownerNode = n.id;
            }
            nextId = std::max(nextId, n.id + 1);
            out.nodes.push_back(std::move(n));
        }
        out.nextId = std::max(nextId, 1u);
        if (const UsdRelationship rootRel = treePrim.GetRelationship(TfToken("ranim:root")))
        {
            SdfPathVector targets;
            rootRel.GetTargets(&targets);
            if (!targets.empty() && idByPath.count(targets.front()) != 0)
            {
                out.rootId = idByPath[targets.front()];
                return true;
            }
        }
        outError = fmt::format("'{}' has no valid ranim:root", path);
        return false;
    }

    // ---- global.blackboard.usda --------------------------------------------------------------------------

    bool WriteBlackboardSchema(const BlackboardSchema& schema, const std::string& path, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdPrim root = UsdGeomScope::Define(stage, SdfPath("/GlobalBlackboard")).GetPrim();
        stage->SetDefaultPrim(root);
        SetCustomLayerData(stage, "blackboardVersion", 1, "Global blackboard schema (BbScope::Global), shared by every tree.");
        WriteKeys(stage, SdfPath("/GlobalBlackboard/Blackboard"), schema.keys);
        return Export(stage, path, outError);
    }

    bool ReadBlackboardSchema(const std::string& path, BlackboardSchema& out, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        UsdStageRefPtr stage = UsdStage::Open(path);
        if (!stage)
        {
            outError = fmt::format("could not open '{}'", path);
            return false;
        }
        out = BlackboardSchema{};
        return ReadKeys(stage->GetPrimAtPath(SdfPath("/GlobalBlackboard/Blackboard")), BbScope::Global, out.keys, outError);
    }

    // ---- .behaviorsave.usda ------------------------------------------------------------------------------

    namespace
    {
        void WriteSaveNode(const UsdStageRefPtr& stage, const SdfPath& path, const SaveNode& node)
        {
            UsdPrim p = UsdGeomScope::Define(stage, path).GetPrim();
            if (Escape(node.name) != path.GetName())
            {
                SetAttr(p, "ranim:__name", SdfValueTypeNames->String, VtValue(node.name));
            }
            for (const auto& [key, value] : node.values)
            {
                const std::string name = "ranim:" + Escape(key);
                std::visit(
                        [&](const auto& v)
                        {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, bool>)
                                SetAttr(p, name, SdfValueTypeNames->Bool, VtValue(v));
                            else if constexpr (std::is_same_v<T, int>)
                                SetAttr(p, name, SdfValueTypeNames->Int, VtValue(v));
                            else if constexpr (std::is_same_v<T, uint64_t>)
                                SetAttr(p, name, SdfValueTypeNames->UInt64, VtValue(v));
                            else if constexpr (std::is_same_v<T, float>)
                                SetAttr(p, name, SdfValueTypeNames->Float, VtValue(v));
                            else if constexpr (std::is_same_v<T, glm::vec3>)
                                SetAttr(p, name, SdfValueTypeNames->Float3, VtValue(GfVec3f(v.x, v.y, v.z)));
                            else if constexpr (std::is_same_v<T, std::string>)
                                SetAttr(p, name, SdfValueTypeNames->String, VtValue(v));
                            else
                            {
                                VtStringArray arr(v.begin(), v.end());
                                SetAttr(p, name, SdfValueTypeNames->StringArray, VtValue(arr));
                            }
                        },
                        value);
            }
            for (const SaveNode& child : node.children)
            {
                WriteSaveNode(stage, path.AppendChild(TfToken(Escape(child.name))), child);
            }
        }

        void ReadSaveNode(const UsdPrim& p, SaveNode& node)
        {
            node.name = Unescape(p.GetName().GetString());
            GetAttr(p, "ranim:__name", node.name);
            for (const UsdAttribute& attr : p.GetAttributes())
            {
                const std::string name = attr.GetName().GetString();
                if (name.rfind("ranim:", 0) != 0 || name == "ranim:__name")
                {
                    continue;
                }
                const std::string key = Unescape(name.substr(6));
                VtValue v;
                if (!attr.Get(&v))
                {
                    continue;
                }
                if (v.IsHolding<bool>())
                    node.Set(key, v.Get<bool>());
                else if (v.IsHolding<int>())
                    node.Set(key, v.Get<int>());
                else if (v.IsHolding<uint64_t>())
                    node.Set(key, v.Get<uint64_t>());
                else if (v.IsHolding<float>())
                    node.Set(key, v.Get<float>());
                else if (v.IsHolding<double>())
                    node.Set(key, static_cast<float>(v.Get<double>()));
                else if (v.IsHolding<GfVec3f>())
                {
                    const GfVec3f g = v.Get<GfVec3f>();
                    node.Set(key, glm::vec3(g[0], g[1], g[2]));
                }
                else if (v.IsHolding<std::string>())
                    node.Set(key, v.Get<std::string>());
                else if (v.IsHolding<VtStringArray>())
                {
                    const VtStringArray arr = v.Get<VtStringArray>();
                    node.Set(key, std::vector<std::string>(arr.begin(), arr.end()));
                }
            }
            for (const UsdPrim& child : p.GetChildren())
            {
                SaveNode c;
                ReadSaveNode(child, c);
                node.children.push_back(std::move(c));
            }
        }
    } // namespace

    bool WriteSaveTree(const SaveNode& root, const std::string& path, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        SetCustomLayerData(stage, "saveVersion", kSaveVersion, "Behaviour checkpoint (SkeletalAnimBehavior)");
        const SdfPath rootPath("/" + Escape(root.name.empty() ? std::string("BehaviorSave") : root.name));
        WriteSaveNode(stage, rootPath, root);
        stage->SetDefaultPrim(stage->GetPrimAtPath(rootPath));
        return Export(stage, path, outError);
    }

    bool ReadSaveTree(const std::string& path, SaveNode& out, std::string& outError)
    {
        detail::RegisterUsdPluginsOnce();
        UsdStageRefPtr stage = UsdStage::Open(path);
        if (!stage || !stage->GetDefaultPrim())
        {
            outError = fmt::format("could not open '{}'", path);
            return false;
        }
        out = SaveNode{};
        ReadSaveNode(stage->GetDefaultPrim(), out);
        return true;
    }
} // namespace RAnimation
