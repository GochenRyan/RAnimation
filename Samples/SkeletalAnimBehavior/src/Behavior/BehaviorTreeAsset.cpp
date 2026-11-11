#include <Behavior/BehaviorTreeAsset.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include <fmt/format.h>

#include <Agent/PerceptionKeys.h>
#include <Behavior/NodeTypes.h>

namespace RAnimation
{
    const char* NodeStatusName(NodeStatus status)
    {
        switch (status)
        {
            case NodeStatus::Idle: return "Idle";
            case NodeStatus::Running: return "Running";
            case NodeStatus::Success: return "Success";
            case NodeStatus::Failure: return "Failure";
        }
        return "?";
    }

    bool ParseNodeStatus(const std::string& name, NodeStatus& out)
    {
        for (NodeStatus s : {NodeStatus::Idle, NodeStatus::Running, NodeStatus::Success, NodeStatus::Failure})
        {
            if (name == NodeStatusName(s))
            {
                out = s;
                return true;
            }
        }
        return false;
    }

    const ParamDesc* BtNodeDesc::FindParam(const std::string& paramName) const
    {
        for (const ParamDesc& p : params)
        {
            if (p.name == paramName)
            {
                return &p;
            }
        }
        return nullptr;
    }

    ParamDesc& BtNodeDesc::Param(const std::string& paramName)
    {
        for (ParamDesc& p : params)
        {
            if (p.name == paramName)
            {
                return p;
            }
        }
        params.push_back(ParamDesc{paramName, false, {}, 0.0f});
        return params.back();
    }

    BtNodeDesc* TreeDesc::Find(uint32_t id)
    {
        for (BtNodeDesc& n : nodes)
        {
            if (n.id == id)
            {
                return &n;
            }
        }
        return nullptr;
    }

    const BtNodeDesc* TreeDesc::Find(uint32_t id) const
    {
        return const_cast<TreeDesc*>(this)->Find(id);
    }

    uint64_t ComputeContentHash(const TreeDesc& desc)
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const std::string& s)
        {
            for (unsigned char ch : s)
            {
                h ^= ch;
                h *= 1099511628211ull;
            }
            h ^= 0xff;
            h *= 1099511628211ull;
        };
        auto mixKey = [&mix](const BlackboardKeyDecl& k)
        {
            mix(k.name);
            mix(BbTypeName(k.type));
            mix(BbScopeName(k.scope));
            mix(ValueToString(k.defaultValue));
            mix(k.wakeOnChange ? "w" : "-");
        };
        mix(fmt::format("{:g}", desc.tickHz));
        mix(std::to_string(desc.rootId));
        std::vector<const BtNodeDesc*> nodes;
        for (const BtNodeDesc& n : desc.nodes)
        {
            nodes.push_back(&n);
        }
        std::sort(nodes.begin(), nodes.end(), [](const BtNodeDesc* a, const BtNodeDesc* b) { return a->id < b->id; });
        for (const BtNodeDesc* n : nodes)
        {
            mix(std::to_string(n->id));
            mix(n->type);
            for (uint32_t c : n->children)
            {
                mix(std::to_string(c));
            }
            std::vector<const ParamDesc*> params;
            for (const ParamDesc& p : n->params)
            {
                params.push_back(&p);
            }
            std::sort(params.begin(), params.end(), [](const ParamDesc* a, const ParamDesc* b) { return a->name < b->name; });
            for (const ParamDesc* p : params)
            {
                mix(p->name);
                mix(p->bound ? "@" + p->key : ValueToString(p->literal));
            }
            for (const BlackboardKeyDecl& k : n->nodeKeys)
            {
                mixKey(k);
            }
        }
        for (const BlackboardKeyDecl& k : desc.keys)
        {
            mixKey(k);
        }
        return h;
    }

    void AutoLayoutTree(TreeDesc& desc, float spacingX, float spacingY)
    {
        std::set<uint32_t> placed;
        float nextLeafX = 0.0f;
        std::function<float(uint32_t, int)> place = [&](uint32_t id, int depth) -> float
        {
            BtNodeDesc* n = desc.Find(id);
            if (n == nullptr || !placed.insert(id).second)
            {
                return nextLeafX;
            }
            float x = 0.0f;
            if (n->children.empty())
            {
                x = nextLeafX;
                nextLeafX += spacingX;
            }
            else
            {
                float first = 0.0f, last = 0.0f;
                for (size_t i = 0; i < n->children.size(); ++i)
                {
                    const float cx = place(n->children[i], depth + 1);
                    if (i == 0)
                        first = cx;
                    last = cx;
                }
                x = 0.5f * (first + last);
            }
            n->editorPos = glm::vec2(x, static_cast<float>(depth) * spacingY);
            return x;
        };
        place(desc.rootId, 0);
        float y = 0.0f;
        for (BtNodeDesc& n : desc.nodes)
        {
            if (placed.count(n.id) == 0)
            {
                n.editorPos = glm::vec2(nextLeafX + spacingX, y);
                y += spacingY;
            }
        }
    }

    namespace
    {
        bool NumericType(BbType t) { return t == BbType::Int || t == BbType::Float || t == BbType::Bool; }

        bool LiteralTo(const BbValue& in, BbType type, BbValue& out)
        {
            if (ConvertValue(in, type, out))
            {
                return true;
            }
            if (const std::string* s = std::get_if<std::string>(&in))
            {
                return ValueFromString(type, *s, out);
            }
            return false;
        }
    } // namespace

    std::shared_ptr<const CompiledTree> CompileTree(std::shared_ptr<const TreeDesc> descPtr,
                                                    const std::string& path,
                                                    const NodeTypeRegistry& types,
                                                    const BlackboardSchema& globalSchema,
                                                    std::vector<ValidationError>& errors)
    {
        errors.clear();
        const TreeDesc& desc = *descPtr;
        auto fail = [&errors](uint32_t id, std::string message) { errors.push_back({id, std::move(message)}); };

        // ---- structure (rules 1, 2) ----
        std::map<uint32_t, const BtNodeDesc*> byId;
        for (const BtNodeDesc& n : desc.nodes)
        {
            if (!byId.emplace(n.id, &n).second)
            {
                fail(n.id, fmt::format("duplicate node id {}", n.id));
            }
            if (types.Find(n.type) == nullptr)
            {
                fail(n.id, fmt::format("unknown node type '{}'", n.type));
            }
        }
        if (byId.find(desc.rootId) == byId.end())
        {
            fail(~0u, "the tree has no root node");
            return nullptr;
        }
        std::map<uint32_t, int> parentCount;
        for (const BtNodeDesc& n : desc.nodes)
        {
            for (uint32_t c : n.children)
            {
                if (byId.find(c) == byId.end())
                {
                    fail(n.id, fmt::format("child {} does not exist", c));
                    continue;
                }
                if (++parentCount[c] == 2)
                {
                    fail(c, "node has more than one parent");
                }
            }
        }
        if (parentCount[desc.rootId] > 0)
        {
            fail(desc.rootId, "the root node has a parent (cycle)");
        }
        // depth-first from the root: cycle detection + order
        std::vector<uint32_t> order;
        std::set<uint32_t> onStack;
        std::set<uint32_t> seen;
        std::function<void(uint32_t)> visit = [&](uint32_t id)
        {
            if (onStack.count(id) != 0)
            {
                fail(id, "cycle");
                return;
            }
            if (!seen.insert(id).second)
            {
                return;
            }
            onStack.insert(id);
            order.push_back(id);
            for (uint32_t c : byId[id]->children)
            {
                if (byId.count(c) != 0)
                {
                    visit(c);
                }
            }
            onStack.erase(id);
        };
        visit(desc.rootId);
        for (const BtNodeDesc& n : desc.nodes)
        {
            if (seen.count(n.id) == 0)
            {
                fail(n.id, "node is not connected to the root (a tree has exactly one root)");
            }
        }
        if (!errors.empty())
        {
            return nullptr;
        }

        auto tree = std::make_shared<CompiledTree>();
        tree->path = path;
        tree->desc = descPtr;
        tree->tickHz = desc.tickHz;
        tree->nodes.resize(order.size());
        for (size_t i = 0; i < order.size(); ++i)
        {
            tree->indexById[order[i]] = static_cast<uint32_t>(i);
        }
        for (size_t i = 0; i < order.size(); ++i)
        {
            const BtNodeDesc& d = *byId[order[i]];
            CompiledNode& node = tree->nodes[i];
            node.id = d.id;
            node.type = types.Find(d.type);
            node.label = d.label.empty() ? d.type : d.label;
            for (uint32_t c : d.children)
            {
                const uint32_t ci = tree->indexById[c];
                node.children.push_back(ci);
                tree->nodes[ci].parent = static_cast<uint32_t>(i);
            }
        }
        // paths (parents come first in depth-first order)
        for (size_t i = 0; i < tree->nodes.size(); ++i)
        {
            CompiledNode& node = tree->nodes[i];
            if (i == 0)
            {
                node.path = node.label;
                continue;
            }
            const CompiledNode& parent = tree->nodes[node.parent];
            const auto pos = std::find(parent.children.begin(), parent.children.end(), static_cast<uint32_t>(i)) - parent.children.begin();
            node.path = fmt::format("{}/{}:{}", parent.path, pos, node.label);
        }
        for (const CompiledNode& node : tree->nodes)
        {
            const int count = static_cast<int>(node.children.size());
            if (count < node.type->MinChildren() || count > node.type->MaxChildren())
            {
                const char* rule = node.type->category == NodeCategory::Leaf        ? "a leaf has no children"
                                   : node.type->category == NodeCategory::Decorator ? "a decorator has exactly one child"
                                                                                    : "a composite has 1 to 64 children";
                fail(node.id, fmt::format("{} ({}: {} children)", rule, node.path, count));
            }
        }

        // ---- keys (rules 5, 7) ----
        const BlackboardSchema& perception = PerceptionSchema();
        tree->instanceSchema = perception;
        std::map<std::string, std::pair<BbScope, uint32_t>> treeKeys; // name -> scope, slot
        std::set<std::string> names;
        for (const BlackboardKeyDecl& k : desc.keys)
        {
            if (!names.insert(k.name).second)
            {
                fail(~0u, fmt::format("key '{}' is declared twice", k.name));
                continue;
            }
            const int perceptionSlot = perception.Find(k.name);
            if (perceptionSlot >= 0)
            {
                if (k.scope != BbScope::Instance || k.type != perception.keys[static_cast<size_t>(perceptionSlot)].type)
                {
                    fail(~0u, fmt::format("'{}' redeclares a perception key with another type / scope", k.name));
                    continue;
                }
                tree->instanceSchema.keys[static_cast<size_t>(perceptionSlot)].wakeOnChange = k.wakeOnChange;
                treeKeys[k.name] = {BbScope::Instance, static_cast<uint32_t>(perceptionSlot)};
                continue;
            }
            switch (k.scope)
            {
                case BbScope::Instance:
                    treeKeys[k.name] = {BbScope::Instance, static_cast<uint32_t>(tree->instanceSchema.keys.size())};
                    tree->instanceSchema.keys.push_back(k);
                    break;
                case BbScope::TreeShared:
                    treeKeys[k.name] = {BbScope::TreeShared, static_cast<uint32_t>(tree->treeSharedSchema.keys.size())};
                    tree->treeSharedSchema.keys.push_back(k);
                    break;
                case BbScope::Global:
                {
                    const int g = globalSchema.Find(k.name);
                    if (g < 0)
                        fail(~0u, fmt::format("global key '{}' is not in global.blackboard.usda", k.name));
                    else if (globalSchema.keys[static_cast<size_t>(g)].type != k.type)
                        fail(~0u, fmt::format("global key '{}' is {} in the global schema, {} here", k.name,
                                              BbTypeName(globalSchema.keys[static_cast<size_t>(g)].type), BbTypeName(k.type)));
                    else
                        treeKeys[k.name] = {BbScope::Global, static_cast<uint32_t>(g)};
                    break;
                }
                case BbScope::Node:
                    fail(~0u, fmt::format("'{}': node keys are declared on their node", k.name));
                    break;
            }
        }
        tree->nodeKeys.resize(tree->nodes.size());
        std::map<std::string, std::pair<uint32_t, uint32_t>> nodeKeyOwner; // name -> node index, slot
        for (size_t i = 0; i < tree->nodes.size(); ++i)
        {
            const BtNodeDesc& d = *byId[tree->nodes[i].id];
            for (BlackboardKeyDecl k : d.nodeKeys)
            {
                if (!names.insert(k.name).second || perception.Find(k.name) >= 0)
                {
                    fail(d.id, fmt::format("key '{}' is declared twice", k.name));
                    continue;
                }
                k.scope = BbScope::Node;
                k.ownerNode = d.id;
                nodeKeyOwner[k.name] = {static_cast<uint32_t>(i), static_cast<uint32_t>(tree->nodeKeys[i].size())};
                tree->nodeKeys[i].push_back(k);
            }
        }

        // D12: a node key of X is visible to X and to the unbroken decorator chain right above X
        auto nodeKeyVisible = [&tree](uint32_t owner, uint32_t user)
        {
            if (owner == user)
            {
                return true;
            }
            uint32_t p = tree->nodes[owner].parent;
            while (p != ~0u && tree->nodes[p].type->category == NodeCategory::Decorator)
            {
                if (p == user)
                {
                    return true;
                }
                p = tree->nodes[p].parent;
            }
            return false;
        };

        // ---- parameters ----
        std::set<uint32_t> globalSlots;
        for (size_t i = 0; i < tree->nodes.size(); ++i)
        {
            CompiledNode& node = tree->nodes[i];
            const BtNodeDesc& d = *byId[node.id];
            const std::vector<ParamSpec>& specs = node.type->params;
            node.params.resize(specs.size());
            for (size_t p = 0; p < specs.size(); ++p)
            {
                const ParamSpec& spec = specs[p];
                const ParamDesc* pd = d.FindParam(spec.name);
                CompiledParam& cp = node.params[p];
                if (spec.kind == ParamKind::Key && (pd == nullptr || !pd->bound || pd->key.empty()))
                {
                    fail(node.id, fmt::format("{}: parameter '{}' must be bound to a key", node.path, spec.name));
                    continue;
                }
                if (pd != nullptr && pd->bound)
                {
                    cp.bound = true;
                    if (auto it = nodeKeyOwner.find(pd->key); it != nodeKeyOwner.end())
                    {
                        if (!nodeKeyVisible(it->second.first, static_cast<uint32_t>(i)))
                        {
                            fail(node.id, fmt::format("{}: node key '{}' belongs to node {} and is not visible here (D12)", node.path,
                                                      pd->key, tree->nodes[it->second.first].id));
                            continue;
                        }
                        cp.scope = BbScope::Node;
                        cp.ownerIndex = it->second.first;
                        cp.slot = it->second.second;
                        cp.keyType = tree->nodeKeys[cp.ownerIndex][cp.slot].type;
                    }
                    else if (auto tk = treeKeys.find(pd->key); tk != treeKeys.end())
                    {
                        cp.scope = tk->second.first;
                        cp.slot = tk->second.second;
                        cp.keyType = cp.scope == BbScope::Instance     ? tree->instanceSchema.keys[cp.slot].type
                                     : cp.scope == BbScope::TreeShared ? tree->treeSharedSchema.keys[cp.slot].type
                                                                       : globalSchema.keys[cp.slot].type;
                    }
                    else if (const int ps = perception.Find(pd->key); ps >= 0)
                    {
                        cp.scope = BbScope::Instance;
                        cp.slot = static_cast<uint32_t>(ps);
                        cp.keyType = perception.keys[static_cast<size_t>(ps)].type;
                    }
                    else if (const int gs = globalSchema.Find(pd->key); gs >= 0)
                    {
                        cp.scope = BbScope::Global;
                        cp.slot = static_cast<uint32_t>(gs);
                        cp.keyType = globalSchema.keys[static_cast<size_t>(gs)].type;
                    }
                    else
                    {
                        fail(node.id, fmt::format("{}: parameter '{}' binds unknown key '{}'", node.path, spec.name, pd->key));
                        continue;
                    }
                    if (cp.scope == BbScope::Global)
                    {
                        globalSlots.insert(cp.slot);
                    }
                    // type compatibility
                    bool ok = true;
                    if (spec.kind == ParamKind::Key)
                    {
                        ok = spec.keyTypes.empty() || std::find(spec.keyTypes.begin(), spec.keyTypes.end(), cp.keyType) != spec.keyTypes.end();
                    }
                    else if (spec.sameTypeAs >= 0)
                    {
                        const CompiledParam& other = node.params[static_cast<size_t>(spec.sameTypeAs)];
                        ok = other.keyType == cp.keyType || (NumericType(other.keyType) && NumericType(cp.keyType));
                    }
                    else if (spec.kind == ParamKind::Value)
                    {
                        ok = spec.type == cp.keyType || (NumericType(spec.type) && NumericType(cp.keyType));
                    }
                    else
                    {
                        ok = cp.keyType == BbType::String;
                    }
                    if (!ok)
                    {
                        fail(node.id, fmt::format("{}: parameter '{}' cannot bind {} key '{}'", node.path, spec.name, BbTypeName(cp.keyType), pd->key));
                    }
                    continue;
                }
                // literal
                BbType target = spec.type;
                if (spec.sameTypeAs >= 0)
                {
                    target = node.params[static_cast<size_t>(spec.sameTypeAs)].keyType;
                }
                const BbValue& authored = pd != nullptr ? pd->literal : spec.defaultValue;
                if (spec.kind == ParamKind::Enum)
                {
                    const std::string* s = std::get_if<std::string>(&authored);
                    if (s == nullptr || std::find(spec.options.begin(), spec.options.end(), *s) == spec.options.end())
                    {
                        if (pd != nullptr)
                            fail(node.id, fmt::format("{}: '{}' must be one of the listed options", node.path, spec.name));
                        cp.literal = spec.defaultValue;
                    }
                    else
                    {
                        cp.literal = *s;
                    }
                    cp.keyType = BbType::String;
                    continue;
                }
                if (!LiteralTo(authored, target, cp.literal))
                {
                    if (pd != nullptr && !(std::holds_alternative<std::string>(authored) && std::get<std::string>(authored).empty()))
                    {
                        fail(node.id, fmt::format("{}: '{}' = '{}' is not a {}", node.path, spec.name, ValueToString(authored), BbTypeName(target)));
                    }
                    cp.literal = DefaultValueFor(target);
                }
                cp.keyType = target;
            }
            // Parallel: 1 <= N <= M (rule 3)
            if (node.type->name == "Parallel" && !node.params.empty() && !node.params[0].bound)
            {
                const int n = std::get<int>(node.params[0].literal);
                if (n < 1 || n > static_cast<int>(node.children.size()))
                {
                    fail(node.id, fmt::format("{}: Parallel needs 1 <= N <= {} (N = {})", node.path, node.children.size(), n));
                }
            }
        }
        tree->globalSlotsUsed.assign(globalSlots.begin(), globalSlots.end());

        // ---- sync / async (D6) ----
        for (size_t k = tree->nodes.size(); k-- > 0;)
        {
            CompiledNode& node = tree->nodes[k];
            if (node.type->category == NodeCategory::Leaf)
            {
                node.async = node.type->async ? node.type->async(node) : false;
                continue;
            }
            bool async = node.type->selfAsync ? node.type->selfAsync(node) : false;
            for (uint32_t c : node.children)
            {
                async = async || tree->nodes[c].async;
            }
            node.async = async;
        }
        // rule 4: reactive composites - every child but the last must be synchronous
        for (const CompiledNode& node : tree->nodes)
        {
            if (node.type->name != "ReactiveSequence" && node.type->name != "ReactiveSelector")
            {
                continue;
            }
            for (size_t c = 0; c + 1 < node.children.size(); ++c)
            {
                const CompiledNode& child = tree->nodes[node.children[c]];
                if (child.async)
                {
                    fail(child.id, fmt::format("{}: only the last child of a {} may be asynchronous (D6)", child.path, node.type->name));
                }
            }
        }

        if (!errors.empty())
        {
            return nullptr;
        }
        tree->contentHash = ComputeContentHash(desc);
        return tree;
    }
} // namespace RAnimation
