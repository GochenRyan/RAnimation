#include <Behavior/BehaviorTreeBrain.h>

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include <Agent/PerceptionKeys.h>
#include <Behavior/BehaviorSystem.h>

namespace RAnimation
{
    // ---- TreeContext -------------------------------------------------------------------------------------

    BlackboardStore* TreeContext::Store(const CompiledParam& param) const
    {
        switch (param.scope)
        {
            case BbScope::Global: return decision.globalBlackboard;
            case BbScope::TreeShared: return treeShared;
            case BbScope::Instance: return decision.instanceBlackboard;
            case BbScope::Node: return param.ownerIndex < instance.nodeKeys.size() ? &instance.nodeKeys[param.ownerIndex] : nullptr;
        }
        return nullptr;
    }

    BbValue TreeContext::Param(uint32_t node, size_t p) const
    {
        const CompiledParam& cp = tree.nodes[node].params[p];
        if (!cp.bound)
        {
            return cp.literal;
        }
        const BlackboardStore* store = Store(cp);
        if (store == nullptr || cp.slot >= store->Size())
        {
            return DefaultValueFor(cp.keyType);
        }
        return store->Get(cp.slot);
    }

    float TreeContext::ParamFloat(uint32_t node, size_t p) const { return ValueAsFloat(Param(node, p)); }
    bool TreeContext::ParamBool(uint32_t node, size_t p) const { return ValueAsFloat(Param(node, p)) != 0.0f; }
    int TreeContext::ParamInt(uint32_t node, size_t p) const { return static_cast<int>(std::lround(ValueAsFloat(Param(node, p)))); }

    std::string TreeContext::ParamString(uint32_t node, size_t p) const
    {
        const BbValue v = Param(node, p);
        if (const std::string* s = std::get_if<std::string>(&v))
        {
            return *s;
        }
        return ValueToString(v);
    }

    bool TreeContext::ParamIsSet(uint32_t node, size_t p) const
    {
        const CompiledParam& cp = tree.nodes[node].params[p];
        if (!cp.bound)
        {
            return true;
        }
        const BlackboardStore* store = Store(cp);
        return store != nullptr && cp.slot < store->Size() && store->IsSet(cp.slot);
    }

    bool TreeContext::SetParamKey(uint32_t node, size_t p, const BbValue& value)
    {
        const CompiledParam& cp = tree.nodes[node].params[p];
        BlackboardStore* store = cp.bound ? Store(cp) : nullptr;
        return store != nullptr && store->Set(cp.slot, value, Self());
    }

    bool TreeContext::ClearParamKey(uint32_t node, size_t p)
    {
        const CompiledParam& cp = tree.nodes[node].params[p];
        BlackboardStore* store = cp.bound ? Store(cp) : nullptr;
        return store != nullptr && store->Clear(cp.slot, Self());
    }

    void TreeContext::ReleaseLocomotion(uint32_t node)
    {
        if (intent.locomotionOwner == node)
        {
            intent.locomotion = LocomotionIntent{};
            intent.locomotionOwner = kNoOwner;
        }
    }

    void TreeContext::Log(uint32_t node, std::string text)
    {
        if (mLog == nullptr)
        {
            return;
        }
        TickLogEntry e;
        e.time = Now();
        e.nodeId = tree.nodes[node].id;
        e.from = e.to = instance.status[node];
        e.text = std::move(text);
        mLog->push_back(std::move(e));
        while (mLog->size() > BehaviorTreeBrain::kLogCapacity)
        {
            mLog->pop_front();
        }
    }

    NodeStatus TreeContext::Tick(uint32_t node)
    {
        const NodeStatus before = instance.status[node];
        const NodeStatus after = tree.nodes[node].type->tick(*this, node);
        instance.status[node] = after;
        instance.lastTickTime[node] = Now();
        if (after != before && mLog != nullptr)
        {
            TickLogEntry e;
            e.time = Now();
            e.nodeId = tree.nodes[node].id;
            e.from = before;
            e.to = after;
            mLog->push_back(std::move(e));
            while (mLog->size() > BehaviorTreeBrain::kLogCapacity)
            {
                mLog->pop_front();
            }
        }
        return after;
    }

    // Recursive on Running nodes: leaves clean up (channels, CancelAction), every halted node's built-in data
    // is reset (Mem progress, Parallel counts, timers; a Cooldown's ready time survives). User node keys stay.
    void TreeContext::Halt(uint32_t node)
    {
        if (instance.status[node] != NodeStatus::Running)
        {
            return;
        }
        const CompiledNode& n = tree.nodes[node];
        if (n.type->halt)
        {
            n.type->halt(*this, node);
        }
        for (uint32_t child : n.children)
        {
            Halt(child);
        }
        const float ready = instance.memory[node].readyTime;
        instance.memory[node] = NodeMemory{};
        instance.memory[node].readyTime = ready;
        if (mLog != nullptr)
        {
            TickLogEntry e;
            e.time = Now();
            e.nodeId = n.id;
            e.from = NodeStatus::Running;
            e.to = NodeStatus::Idle;
            e.text = "halted";
            mLog->push_back(std::move(e));
            while (mLog->size() > BehaviorTreeBrain::kLogCapacity)
            {
                mLog->pop_front();
            }
        }
        instance.status[node] = NodeStatus::Idle;
    }

    // ---- BehaviorTreeBrain -------------------------------------------------------------------------------

    BehaviorTreeBrain::BehaviorTreeBrain(BehaviorSystem* system, std::shared_ptr<const CompiledTree> tree, EntityId self, float tickHzOverride)
        : mSystem(system), mTree(std::move(tree)), mSelf(self), mTickHzOverride(tickHzOverride)
    {
        if (mSystem != nullptr)
        {
            mTreeShared = mSystem->TreeShared(mTree->path);
            mSystem->Register(this);
        }
        InitInstance();
    }

    BehaviorTreeBrain::~BehaviorTreeBrain()
    {
        if (mSystem != nullptr)
        {
            mSystem->Unregister(this);
        }
    }

    void BehaviorTreeBrain::InitInstance()
    {
        const size_t n = mTree->nodes.size();
        mInstance.status.assign(n, NodeStatus::Idle);
        mInstance.lastTickTime.assign(n, -1.0f);
        mInstance.memory.assign(n, NodeMemory{});
        mInstance.nodeKeys.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            mInstance.nodeKeys[i].Init(mTree->nodeKeys[i]);
        }
        mInstance.immediateWake = false;
    }

    float BehaviorTreeBrain::TickHz() const
    {
        return mTickHzOverride > 0.0f ? mTickHzOverride : mTree->tickHz;
    }

    std::string BehaviorTreeBrain::Describe() const
    {
        return fmt::format("BehaviorTree '{}' ({})", mTree->desc->name, mTree->path.empty() ? "in memory" : mTree->path);
    }

    void BehaviorTreeBrain::Tick(DecisionContext& ctx, AgentIntent& intent)
    {
        mLastContext = ctx;
        mLastInstanceBlackboard = ctx.instanceBlackboard;
        mInstance.immediateWake = false;
        TreeContext tc(*mTree, mInstance, ctx, intent, mTreeShared, &mLog);
        tc.Tick(0);
        mInstance.lastTickWorldTime = ctx.worldTime;
        ++mTicks;
    }

    void BehaviorTreeBrain::Halt(AgentIntent& intent)
    {
        TreeContext tc(*mTree, mInstance, mLastContext, intent, mTreeShared, &mLog);
        tc.Halt(0);
        // hand every channel back (synchronous writers such as SetStance are never "running")
        intent.ResetChannels();
    }

    // §6.4: built-in data and user node keys back to defaults, the tree's instance keys too; perception keys
    // and shared / global stores are kept.
    void BehaviorTreeBrain::Reset()
    {
        InitInstance();
        if (mLastInstanceBlackboard != nullptr)
        {
            const size_t perception = PerceptionSchema().keys.size();
            for (size_t slot = perception; slot < mLastInstanceBlackboard->Size(); ++slot)
            {
                mLastInstanceBlackboard->Clear(static_cast<uint32_t>(slot), mSelf);
            }
        }
        TickLogEntry e;
        e.time = mLastContext.worldTime;
        e.nodeId = mTree->nodes[0].id;
        e.text = "reset (start from the root)";
        mLog.push_back(std::move(e));
    }

    float BehaviorTreeBrain::NextWakeTime() const
    {
        if (mInstance.immediateWake)
        {
            return mInstance.lastTickWorldTime;
        }
        float next = std::numeric_limits<float>::infinity();
        for (const NodeMemory& m : mInstance.memory)
        {
            // a timer at or before the last tick is stale (its node was not ticked since)
            if (m.wakeAt > mInstance.lastTickWorldTime && m.wakeAt < next)
            {
                next = m.wakeAt;
            }
        }
        return next;
    }

    namespace
    {
        SaveValue ToSave(const BbValue& v)
        {
            switch (TypeOf(v))
            {
                case BbType::Bool: return std::get<bool>(v);
                case BbType::Int: return std::get<int>(v);
                case BbType::Float: return std::get<float>(v);
                case BbType::Vec3: return std::get<glm::vec3>(v);
                case BbType::String: return std::get<std::string>(v);
                case BbType::Entity: return static_cast<uint64_t>(std::get<EntityId>(v));
            }
            return 0;
        }

        bool FromSave(const SaveValue& v, BbType type, BbValue& out)
        {
            BbValue raw;
            if (const bool* b = std::get_if<bool>(&v))
                raw = *b;
            else if (const int* i = std::get_if<int>(&v))
                raw = *i;
            else if (const float* f = std::get_if<float>(&v))
                raw = *f;
            else if (const glm::vec3* p = std::get_if<glm::vec3>(&v))
                raw = *p;
            else if (const std::string* s = std::get_if<std::string>(&v))
                raw = *s;
            else if (const uint64_t* u = std::get_if<uint64_t>(&v))
                raw = static_cast<EntityId>(*u);
            else
                return false;
            if (type == BbType::Entity && std::holds_alternative<int>(raw))
            {
                raw = static_cast<EntityId>(std::get<int>(raw));
            }
            return ConvertValue(raw, type, out);
        }
    } // namespace

    // Store values (§8.4 encoding): every key that is set is written as bb:<name>; unset keys are not written
    // and come back at their default.
    void SaveBlackboardValues(const BlackboardStore& store, SaveNode& out, size_t firstSlot)
    {
        for (size_t s = firstSlot; s < store.Size(); ++s)
        {
            const uint32_t slot = static_cast<uint32_t>(s);
            if (store.IsSet(slot))
            {
                out.Set("bb:" + store.Decl(slot).name, ToSave(store.Get(slot)));
            }
        }
    }

    void LoadBlackboardValues(BlackboardStore& store, const SaveNode& in, size_t firstSlot, EntityId writer)
    {
        for (size_t s = firstSlot; s < store.Size(); ++s)
        {
            const uint32_t slot = static_cast<uint32_t>(s);
            const BlackboardKeyDecl& d = store.Decl(slot);
            const SaveValue* v = in.Find("bb:" + d.name);
            BbValue value;
            if (v != nullptr && FromSave(*v, d.type, value))
            {
                store.Set(slot, value, writer);
            }
            else
            {
                store.Clear(slot, writer);
            }
        }
    }

    bool BehaviorTreeBrain::SaveRuntime(SaveNode& out) const
    {
        out.Set("tree", mTree->path);
        out.Set("contentHash", static_cast<uint64_t>(mTree->contentHash));
        if (mLastInstanceBlackboard != nullptr)
        {
            SaveBlackboardValues(*mLastInstanceBlackboard, out.Child("Instance"), PerceptionSchema().keys.size());
        }
        SaveNode& nodes = out.Child("Nodes");
        for (size_t i = 0; i < mTree->nodes.size(); ++i)
        {
            const NodeMemory& m = mInstance.memory[i];
            const NodeStatus status = mInstance.status[i];
            const bool hasKeys = mInstance.nodeKeys[i].Size() > 0;
            const bool interesting = status != NodeStatus::Idle || m.index != 0 || m.doneMask != 0 || m.counter != 0 || m.started ||
                                     std::isfinite(m.wakeAt) || std::isfinite(m.readyTime) || hasKeys;
            if (!interesting)
            {
                continue;
            }
            SaveNode& n = nodes.Child("N" + std::to_string(mTree->nodes[i].id));
            n.Set("status", std::string(NodeStatusName(status)));
            n.Set("mem:index", m.index);
            n.Set("mem:doneMask", static_cast<uint64_t>(m.doneMask));
            n.Set("mem:successMask", static_cast<uint64_t>(m.successMask));
            n.Set("mem:counter", m.counter);
            n.Set("mem:startTime", m.startTime);
            n.Set("mem:started", m.started);
            n.Set("mem:requestId", static_cast<uint64_t>(m.request));
            if (std::isfinite(m.wakeAt))
                n.Set("mem:wakeAt", m.wakeAt);
            if (std::isfinite(m.readyTime))
                n.Set("mem:readyTime", m.readyTime);
            if (hasKeys)
            {
                SaveBlackboardValues(mInstance.nodeKeys[i], n, 0);
            }
        }
        return true;
    }

    bool BehaviorTreeBrain::LoadRuntime(const SaveNode& in, std::string& outError)
    {
        uint64_t hash = 0;
        in.Get("contentHash", hash);
        if (hash != mTree->contentHash)
        {
            outError = fmt::format("tree '{}' changed since the save (content hash)", mTree->path);
            return false;
        }
        InitInstance();
        if (const SaveNode* nodes = in.FindChild("Nodes"))
        {
            for (const SaveNode& n : nodes->children)
            {
                if (n.name.size() < 2)
                    continue;
                const int index = mTree->IndexOf(static_cast<uint32_t>(std::stoul(n.name.substr(1))));
                if (index < 0)
                    continue;
                NodeStatus status = NodeStatus::Idle;
                ParseNodeStatus(n.GetOr<std::string>("status", "Idle"), status);
                mInstance.status[static_cast<size_t>(index)] = status;
                NodeMemory& m = mInstance.memory[static_cast<size_t>(index)];
                n.Get("mem:index", m.index);
                m.doneMask = n.GetOr<uint64_t>("mem:doneMask", 0);
                m.successMask = n.GetOr<uint64_t>("mem:successMask", 0);
                n.Get("mem:counter", m.counter);
                n.Get("mem:startTime", m.startTime);
                n.Get("mem:started", m.started);
                m.request = static_cast<RequestId>(n.GetOr<uint64_t>("mem:requestId", 0));
                n.Get("mem:wakeAt", m.wakeAt);
                n.Get("mem:readyTime", m.readyTime);
                LoadBlackboardValues(mInstance.nodeKeys[static_cast<size_t>(index)], n, 0, mSelf);
            }
        }
        if (const SaveNode* inst = in.FindChild("Instance"); inst != nullptr && mLastInstanceBlackboard != nullptr)
        {
            LoadBlackboardValues(*mLastInstanceBlackboard, *inst, PerceptionSchema().keys.size(), mSelf);
        }
        return true;
    }
} // namespace RAnimation
