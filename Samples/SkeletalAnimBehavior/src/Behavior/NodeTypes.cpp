#include <Behavior/NodeTypes.h>

#include <algorithm>
#include <bit>
#include <cmath>

#include <fmt/format.h>

#include <Agent/CharacterSignals.h>
#include <Behavior/BehaviorTreeBrain.h>

namespace RAnimation
{
    const char* NodeCategoryName(NodeCategory category)
    {
        switch (category)
        {
            case NodeCategory::Composite: return "Composite";
            case NodeCategory::Decorator: return "Decorator";
            case NodeCategory::Leaf: return "Leaf";
        }
        return "?";
    }

    int NodeType::ParamIndex(const std::string& paramName) const
    {
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (params[i].name == paramName)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    const std::vector<std::string>& CompareOps()
    {
        static const std::vector<std::string> ops = {"==", "!=", "<", "<=", ">", ">=", "IsSet", "IsNotSet"};
        return ops;
    }

    bool EvaluateCompare(const std::string& op, const BbValue& left, bool leftSet, const BbValue& right)
    {
        if (op == "IsSet")
        {
            return leftSet;
        }
        if (op == "IsNotSet")
        {
            return !leftSet;
        }
        const BbType type = TypeOf(left);
        BbValue r;
        if (!ConvertValue(right, type, r))
        {
            if (TypeOf(right) == BbType::String && ValueFromString(type, std::get<std::string>(right), r))
            {
            }
            else
            {
                return false;
            }
        }
        if (op == "==")
            return ValuesEqual(left, r);
        if (op == "!=")
            return !ValuesEqual(left, r);
        if (type == BbType::Int || type == BbType::Float || type == BbType::Bool)
        {
            const float a = ValueAsFloat(left);
            const float b = ValueAsFloat(r);
            if (op == "<")
                return a < b;
            if (op == "<=")
                return a <= b;
            if (op == ">")
                return a > b;
            if (op == ">=")
                return a >= b;
        }
        if (type == BbType::String)
        {
            const std::string& a = std::get<std::string>(left);
            const std::string& b = std::get<std::string>(r);
            if (op == "<")
                return a < b;
            if (op == "<=")
                return a <= b;
            if (op == ">")
                return a > b;
            if (op == ">=")
                return a >= b;
        }
        return false;
    }

    namespace
    {
        constexpr int kMaxLoopsPerTick = 64;
        constexpr float kBlockedGraceSec = 1.0f; // MoveTo ignores "blocked" before it ran this long

        ParamSpec P(const char* name, BbType type, BbValue def, const char* doc = "")
        {
            ParamSpec p;
            p.name = name;
            p.type = type;
            p.defaultValue = std::move(def);
            p.doc = doc;
            return p;
        }
        ParamSpec K(const char* name, std::vector<BbType> types, const char* doc = "")
        {
            ParamSpec p;
            p.name = name;
            p.kind = ParamKind::Key;
            p.type = types.empty() ? BbType::Float : types.front();
            p.keyTypes = std::move(types);
            p.defaultValue = std::string();
            p.doc = doc;
            return p;
        }
        ParamSpec E(const char* name, std::vector<std::string> options, const char* doc = "")
        {
            ParamSpec p;
            p.name = name;
            p.kind = ParamKind::Enum;
            p.type = BbType::String;
            p.defaultValue = options.front();
            p.options = std::move(options);
            p.doc = doc;
            return p;
        }
        ParamSpec Same(const char* name, int as, const char* doc = "")
        {
            ParamSpec p;
            p.name = name;
            p.type = BbType::String;
            p.defaultValue = std::string();
            p.sameTypeAs = as;
            p.doc = doc;
            return p;
        }

        std::unique_ptr<NodeType> Make(const char* name, NodeCategory category, const char* doc)
        {
            auto t = std::make_unique<NodeType>();
            t->name = name;
            t->category = category;
            t->doc = doc;
            return t;
        }

        void HaltOthers(TreeContext& c, uint32_t node, uint32_t except)
        {
            for (uint32_t child : c.Node(node).children)
            {
                if (child != except && c.instance.status[child] == NodeStatus::Running)
                {
                    c.Halt(child);
                }
            }
        }

        // ---- composites ----------------------------------------------------------------------------------

        NodeStatus TickReactiveSequence(TreeContext& c, uint32_t n)
        {
            for (uint32_t child : c.Node(n).children)
            {
                const NodeStatus s = c.Tick(child);
                if (s == NodeStatus::Running)
                {
                    HaltOthers(c, n, child); // a different child ran before: preempted
                    return NodeStatus::Running;
                }
                if (s == NodeStatus::Failure)
                {
                    HaltOthers(c, n, child);
                    return NodeStatus::Failure;
                }
            }
            return NodeStatus::Success;
        }

        NodeStatus TickReactiveSelector(TreeContext& c, uint32_t n)
        {
            for (uint32_t child : c.Node(n).children)
            {
                const NodeStatus s = c.Tick(child);
                if (s == NodeStatus::Running || s == NodeStatus::Success)
                {
                    HaltOthers(c, n, child);
                    return s;
                }
            }
            return NodeStatus::Failure;
        }

        NodeStatus TickMem(TreeContext& c, uint32_t n, bool sequence)
        {
            const std::vector<uint32_t>& children = c.Node(n).children;
            NodeMemory& m = c.Mem(n);
            const NodeStatus advanceOn = sequence ? NodeStatus::Success : NodeStatus::Failure;
            while (m.index < static_cast<int>(children.size()))
            {
                const NodeStatus s = c.Tick(children[static_cast<size_t>(m.index)]);
                if (s == NodeStatus::Running)
                {
                    return NodeStatus::Running;
                }
                if (s != advanceOn)
                {
                    m.index = 0;
                    return s;
                }
                ++m.index; // same tick, next child
            }
            m.index = 0;
            return advanceOn;
        }

        NodeStatus TickParallel(TreeContext& c, uint32_t n)
        {
            const std::vector<uint32_t>& children = c.Node(n).children;
            NodeMemory& m = c.Mem(n);
            const int total = static_cast<int>(children.size());
            const int needed = std::clamp(c.ParamInt(n, 0), 1, std::max(1, total));
            for (size_t k = 0; k < children.size(); ++k)
            {
                const uint64_t bit = uint64_t{1} << k;
                if (m.doneMask & bit)
                {
                    continue;
                }
                const NodeStatus s = c.Tick(children[k]);
                if (s == NodeStatus::Running)
                {
                    continue;
                }
                m.doneMask |= bit;
                if (s == NodeStatus::Success)
                {
                    m.successMask |= bit;
                }
            }
            const int successes = std::popcount(m.successMask);
            const int failures = std::popcount(m.doneMask & ~m.successMask);
            NodeStatus result = NodeStatus::Running;
            if (successes >= needed)
                result = NodeStatus::Success;
            else if (failures > total - needed)
                result = NodeStatus::Failure;
            if (result != NodeStatus::Running)
            {
                HaltOthers(c, n, ~0u);
                m.doneMask = 0;
                m.successMask = 0;
            }
            return result;
        }

        // ---- decorators ----------------------------------------------------------------------------------

        uint32_t Child(TreeContext& c, uint32_t n) { return c.Node(n).children.front(); }

        NodeStatus TickRepeatRetry(TreeContext& c, uint32_t n, bool repeat)
        {
            NodeMemory& m = c.Mem(n);
            const int count = c.ParamInt(n, 0);
            const NodeStatus loopOn = repeat ? NodeStatus::Success : NodeStatus::Failure;
            for (int loops = 0; loops < kMaxLoopsPerTick; ++loops)
            {
                const NodeStatus s = c.Tick(Child(c, n));
                if (s == NodeStatus::Running)
                {
                    return NodeStatus::Running;
                }
                if (s != loopOn)
                {
                    m.counter = 0;
                    return s;
                }
                ++m.counter;
                if (count >= 0 && m.counter >= count)
                {
                    m.counter = 0;
                    // Repeat: done after count successes; Retry: out of attempts
                    return repeat ? NodeStatus::Success : NodeStatus::Failure;
                }
            }
            c.instance.immediateWake = true; // continue next frame
            return NodeStatus::Running;
        }

        NodeStatus TickTimeout(TreeContext& c, uint32_t n)
        {
            NodeMemory& m = c.Mem(n);
            const float seconds = c.ParamFloat(n, 0);
            if (!m.started)
            {
                m.started = true;
                m.startTime = c.Now();
                m.wakeAt = c.Now() + seconds; // D9: wake exactly at the deadline
            }
            if (c.Now() >= m.startTime + seconds)
            {
                if (c.instance.status[Child(c, n)] == NodeStatus::Running)
                {
                    c.Halt(Child(c, n));
                }
                m.started = false;
                m.wakeAt = std::numeric_limits<float>::infinity();
                c.Log(n, "timed out");
                return NodeStatus::Failure;
            }
            const NodeStatus s = c.Tick(Child(c, n));
            if (s != NodeStatus::Running)
            {
                m.started = false;
                m.wakeAt = std::numeric_limits<float>::infinity();
            }
            return s;
        }

        NodeStatus TickCooldown(TreeContext& c, uint32_t n)
        {
            NodeMemory& m = c.Mem(n);
            if (c.Now() < m.readyTime)
            {
                m.wakeAt = m.readyTime;
                return NodeStatus::Failure;
            }
            const NodeStatus s = c.Tick(Child(c, n));
            if (s != NodeStatus::Running)
            {
                m.readyTime = c.Now() + c.ParamFloat(n, 0);
                m.wakeAt = m.readyTime;
            }
            return s;
        }

        bool Condition(TreeContext& c, uint32_t n)
        {
            return EvaluateCompare(c.ParamString(n, 1), c.Param(n, 0), c.ParamIsSet(n, 0), c.Param(n, 2));
        }

        NodeStatus TickCondition(TreeContext& c, uint32_t n)
        {
            if (!Condition(c, n))
            {
                if (c.instance.status[Child(c, n)] == NodeStatus::Running)
                {
                    c.Halt(Child(c, n));
                }
                return NodeStatus::Failure;
            }
            return c.Tick(Child(c, n));
        }

        // ---- leaves --------------------------------------------------------------------------------------

        // target param: Vec3 key -> position; Entity key -> entity (resolved through the world each tick)
        bool ResolveTarget(TreeContext& c, uint32_t n, size_t p, glm::vec3& position, EntityId& entity)
        {
            entity = kInvalidEntityId;
            const BbValue v = c.Param(n, p);
            if (const EntityId* e = std::get_if<EntityId>(&v))
            {
                if (*e == kInvalidEntityId || c.decision.world == nullptr)
                {
                    return false;
                }
                const std::optional<glm::vec3> pos = c.decision.world->Position(*e);
                if (!pos)
                {
                    return false;
                }
                entity = *e;
                position = *pos;
                return true;
            }
            if (const glm::vec3* pos = std::get_if<glm::vec3>(&v))
            {
                if (!c.ParamIsSet(n, p) && c.Node(n).params[p].bound)
                {
                    return false; // an unset vec3 key is not a target
                }
                position = *pos;
                return true;
            }
            return false;
        }

        Gait GaitFrom(const std::string& s)
        {
            if (s == "Jog")
                return Gait::Jog;
            if (s == "Sprint")
                return Gait::Sprint;
            return Gait::Walk;
        }

        float HorizontalDistance(const glm::vec3& a, const glm::vec3& b)
        {
            return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
        }

        NodeStatus TickMoveTo(TreeContext& c, uint32_t n)
        {
            NodeMemory& m = c.Mem(n);
            glm::vec3 target(0.0f);
            EntityId entity = kInvalidEntityId;
            if (!ResolveTarget(c, n, 0, target, entity))
            {
                c.ReleaseLocomotion(n);
                m.started = false;
                c.Log(n, "target missing");
                return NodeStatus::Failure;
            }
            const float arrive = std::max(0.05f, c.ParamFloat(n, 2));
            const glm::vec3 position = c.decision.feedback != nullptr ? c.decision.feedback->motion.position : glm::vec3(0.0f);
            if (HorizontalDistance(position, target) <= arrive)
            {
                c.ReleaseLocomotion(n);
                m.started = false;
                return NodeStatus::Success;
            }
            if (!m.started)
            {
                m.started = true;
                m.startTime = c.Now();
            }
            else if (c.ParamBool(n, 3) && c.decision.feedback != nullptr && c.decision.feedback->blocked &&
                     c.Now() - m.startTime >= kBlockedGraceSec)
            {
                c.ReleaseLocomotion(n);
                m.started = false;
                c.Log(n, "blocked");
                return NodeStatus::Failure;
            }
            c.ClaimLocomotion(n);
            LocomotionIntent& loco = c.intent.locomotion;
            loco.mode = LocomotionMode::MoveTo;
            loco.target = target;
            loco.targetEntity = entity;
            loco.gait = GaitFrom(c.ParamString(n, 1));
            loco.arriveRadius = arrive;
            loco.crouch = false;
            return NodeStatus::Running;
        }

        NodeStatus TickFaceTo(TreeContext& c, uint32_t n)
        {
            glm::vec3 target(0.0f);
            EntityId entity = kInvalidEntityId;
            if (!ResolveTarget(c, n, 0, target, entity) || c.decision.feedback == nullptr)
            {
                c.ReleaseLocomotion(n);
                return NodeStatus::Failure;
            }
            const MotionState& motion = c.decision.feedback->motion;
            const glm::vec3 delta(target.x - motion.position.x, 0.0f, target.z - motion.position.z);
            if (glm::length(delta) < 1e-3f ||
                std::abs(WrapDeltaDeg(YawFromDirection(delta), motion.yawDeg)) <= std::max(1.0f, c.ParamFloat(n, 1)))
            {
                c.ReleaseLocomotion(n);
                return NodeStatus::Success;
            }
            c.ClaimLocomotion(n);
            LocomotionIntent& loco = c.intent.locomotion;
            loco = LocomotionIntent{};
            loco.mode = LocomotionMode::FaceTo;
            loco.target = target;
            loco.targetEntity = entity;
            return NodeStatus::Running;
        }

        NodeStatus TickPlayAction(TreeContext& c, uint32_t n)
        {
            NodeMemory& m = c.Mem(n);
            const bool wait = c.ParamBool(n, 1);
            if (!m.started)
            {
                IntentCommand cmd;
                cmd.type = CommandType::Action;
                cmd.montage = c.ParamString(n, 0);
                cmd.id = c.decision.requests != nullptr ? c.decision.requests->Allocate() : 0;
                c.intent.commands.push_back(cmd);
                m.request = cmd.id;
                if (!wait)
                {
                    return NodeStatus::Success;
                }
                m.started = true;
                c.Log(n, fmt::format("request #{} '{}'", cmd.id, cmd.montage));
                return NodeStatus::Running;
            }
            const RequestStatus s = c.decision.feedback != nullptr ? c.decision.feedback->Query(m.request) : RequestStatus::Cancelled;
            if (s == RequestStatus::Completed)
            {
                m.started = false;
                return NodeStatus::Success;
            }
            if (s == RequestStatus::Rejected || s == RequestStatus::Cancelled)
            {
                m.started = false;
                c.Log(n, fmt::format("request #{} {}", m.request, RequestStatusName(s)));
                return NodeStatus::Failure;
            }
            return NodeStatus::Running;
        }

        void HaltPlayAction(TreeContext& c, uint32_t n)
        {
            NodeMemory& m = c.Mem(n);
            if (m.started && c.ParamBool(n, 2))
            {
                IntentCommand cmd;
                cmd.type = CommandType::CancelAction;
                cmd.cancelTarget = m.request;
                c.intent.commands.push_back(cmd);
                c.Log(n, fmt::format("cancel #{}", m.request));
            }
        }

        NodeStatus TickWait(TreeContext& c, uint32_t n, float seconds)
        {
            NodeMemory& m = c.Mem(n);
            if (!m.started)
            {
                m.started = true;
                m.wakeAt = c.Now() + std::max(0.0f, seconds);
            }
            if (c.Now() >= m.wakeAt)
            {
                m.started = false;
                m.wakeAt = std::numeric_limits<float>::infinity();
                return NodeStatus::Success;
            }
            return NodeStatus::Running;
        }

        // deterministic per (agent, node, start time): reproducible runs under --fixed-dt
        float Random01(EntityId self, uint32_t nodeId, float time)
        {
            uint64_t x = self * 0x9E3779B97F4A7C15ull ^ (uint64_t{nodeId} << 32) ^ static_cast<uint64_t>(time * 1000.0f);
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
            return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
        }
    } // namespace

    NodeTypeRegistry::NodeTypeRegistry()
    {
        const auto any = std::vector<BbType>{};
        const auto numeric = std::vector<BbType>{BbType::Int, BbType::Float};
        const auto target = std::vector<BbType>{BbType::Vec3, BbType::Entity};
        const auto inherit = [](const CompiledNode&) { return false; };

        // ---- composites (D5) ----
        {
            auto t = Make("ReactiveSequence", NodeCategory::Composite,
                          "Re-evaluates from the first child every tick; a failing condition halts the running child. "
                          "Every child but the last must be synchronous (D6).");
            t->tick = TickReactiveSequence;
            Add(std::move(t));
        }
        {
            auto t = Make("ReactiveSelector", NodeCategory::Composite,
                          "Re-evaluates from the first child every tick; a higher-priority child that starts running or "
                          "succeeds halts the lower one. Every child but the last must be synchronous (D6).");
            t->tick = TickReactiveSelector;
            Add(std::move(t));
        }
        {
            auto t = Make("MemSequence", NodeCategory::Composite, "Resumes at the remembered child; Failure resets to the first.");
            t->builtins = {"currentIndex"};
            t->tick = [](TreeContext& c, uint32_t n) { return TickMem(c, n, true); };
            Add(std::move(t));
        }
        {
            auto t = Make("MemSelector", NodeCategory::Composite, "Resumes at the remembered child; Success resets to the first.");
            t->builtins = {"currentIndex"};
            t->tick = [](TreeContext& c, uint32_t n) { return TickMem(c, n, false); };
            Add(std::move(t));
        }
        {
            auto t = Make("Parallel", NodeCategory::Composite,
                          "Ticks every unfinished child; Success once N succeeded, Failure once N can no longer be reached.");
            t->params = {P("successThreshold", BbType::Int, 1, "N (1 <= N <= number of children)")};
            t->builtins = {"successCount", "failureCount"};
            t->tick = TickParallel;
            Add(std::move(t));
        }

        // ---- decorators (D7) ----
        {
            auto t = Make("Inverter", NodeCategory::Decorator, "Success <-> Failure, Running passes through.");
            t->tick = [](TreeContext& c, uint32_t n)
            {
                const NodeStatus s = c.Tick(Child(c, n));
                return s == NodeStatus::Success ? NodeStatus::Failure : (s == NodeStatus::Failure ? NodeStatus::Success : s);
            };
            Add(std::move(t));
        }
        {
            auto t = Make("Repeat", NodeCategory::Decorator,
                          "Runs the child again after each Success; Failure ends it. count -1 = forever. At most 64 "
                          "synchronous loops per tick.");
            t->params = {P("count", BbType::Int, -1)};
            t->builtins = {"iteration"};
            t->selfAsync = [](const CompiledNode& node)
            {
                const BbValue& v = node.params[0].literal;
                return node.params[0].bound || (std::holds_alternative<int>(v) && std::get<int>(v) < 0);
            };
            t->tick = [](TreeContext& c, uint32_t n) { return TickRepeatRetry(c, n, true); };
            Add(std::move(t));
        }
        {
            auto t = Make("Retry", NodeCategory::Decorator,
                          "Runs the child again after each Failure, up to count attempts in total; Success ends it.");
            t->params = {P("count", BbType::Int, 3)};
            t->builtins = {"attempt"};
            t->tick = [](TreeContext& c, uint32_t n) { return TickRepeatRetry(c, n, false); };
            Add(std::move(t));
        }
        {
            auto t = Make("Timeout", NodeCategory::Decorator, "Halts the child and fails once it ran for `seconds`; wakes at the deadline.");
            t->params = {P("seconds", BbType::Float, 5.0f)};
            t->builtins = {"remaining"};
            t->selfAsync = [](const CompiledNode&) { return true; };
            t->tick = TickTimeout;
            Add(std::move(t));
        }
        {
            auto t = Make("Cooldown", NodeCategory::Decorator,
                          "After the child finished, fails without ticking it for `seconds`; wakes when ready again.");
            t->params = {P("seconds", BbType::Float, 2.0f)};
            t->builtins = {"remaining"};
            t->selfAsync = [](const CompiledNode&) { return true; };
            t->tick = TickCooldown;
            Add(std::move(t));
        }
        {
            auto t = Make("ForceSuccess", NodeCategory::Decorator, "A finished child counts as Success.");
            t->tick = [](TreeContext& c, uint32_t n)
            {
                const NodeStatus s = c.Tick(Child(c, n));
                return s == NodeStatus::Running ? s : NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("ForceFailure", NodeCategory::Decorator, "A finished child counts as Failure.");
            t->tick = [](TreeContext& c, uint32_t n)
            {
                const NodeStatus s = c.Tick(Child(c, n));
                return s == NodeStatus::Running ? s : NodeStatus::Failure;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("BlackboardCondition", NodeCategory::Decorator,
                          "Checked whenever it is ticked (no observer abort, D7): false halts a running child and fails.");
            t->params = {K("key", any), E("op", CompareOps()), Same("value", 0)};
            t->tick = TickCondition;
            Add(std::move(t));
        }

        // ---- leaves (§5.5) ----
        {
            auto t = Make("BlackboardCompare", NodeCategory::Leaf, "Condition: key op value (value may be another key).");
            t->params = {K("key", any), E("op", CompareOps()), Same("value", 0)};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n) { return Condition(c, n) ? NodeStatus::Success : NodeStatus::Failure; };
            Add(std::move(t));
        }
        {
            auto t = Make("SetBlackboard", NodeCategory::Leaf, "Writes value into key.");
            t->params = {K("key", any), Same("value", 0)};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                c.SetParamKey(n, 0, c.Param(n, 1));
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("ClearBlackboard", NodeCategory::Leaf, "Resets key to its default and marks it unset.");
            t->params = {K("key", any)};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                c.ClearParamKey(n, 0);
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("AddBlackboard", NodeCategory::Leaf, "key += delta (Int / Float keys), e.g. the next waypoint index.");
            t->params = {K("key", numeric), P("delta", BbType::Float, 1.0f)};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                const float sum = ValueAsFloat(c.Param(n, 0)) + c.ParamFloat(n, 1);
                c.SetParamKey(n, 0, sum);
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("MoveTo", NodeCategory::Leaf,
                          "Walks straight to a Vec3 / Entity key. Success on arrival; Failure when blocked (failOnBlocked) "
                          "or when the target entity is gone. Owns the locomotion channel.");
            t->params = {K("target", target), E("gait", {"Walk", "Jog", "Sprint"}), P("arriveRadius", BbType::Float, 0.4f),
                         P("failOnBlocked", BbType::Bool, true)};
            t->async = [](const CompiledNode&) { return true; };
            t->tick = TickMoveTo;
            t->halt = [](TreeContext& c, uint32_t n) { c.ReleaseLocomotion(n); };
            Add(std::move(t));
        }
        {
            auto t = Make("FaceTo", NodeCategory::Leaf, "Turns until the target is within toleranceDeg. Owns the locomotion channel.");
            t->params = {K("target", target), P("toleranceDeg", BbType::Float, 10.0f)};
            t->async = [](const CompiledNode&) { return true; };
            t->tick = TickFaceTo;
            t->halt = [](TreeContext& c, uint32_t n) { c.ReleaseLocomotion(n); };
            Add(std::move(t));
        }
        {
            auto t = Make("Stop", NodeCategory::Leaf, "Takes the locomotion channel and stands still.");
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                c.ClaimLocomotion(n);
                c.intent.locomotion = LocomotionIntent{};
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("PlayAction", NodeCategory::Leaf,
                          "Requests a montage. With waitForCompletion: Completed -> Success, Rejected / Cancelled -> Failure; "
                          "Halt sends CancelAction when cancelOnHalt (D4).");
            t->params = {P("montage", BbType::String, std::string("wave")), P("waitForCompletion", BbType::Bool, true),
                         P("cancelOnHalt", BbType::Bool, true)};
            t->builtins = {"requestId"};
            t->async = [](const CompiledNode& node)
            {
                const CompiledParam& wait = node.params[1];
                return wait.bound || (std::holds_alternative<bool>(wait.literal) && std::get<bool>(wait.literal));
            };
            t->tick = TickPlayAction;
            t->halt = HaltPlayAction;
            Add(std::move(t));
        }
        {
            auto t = Make("SetStance", NodeCategory::Leaf, "Writes the stance channel (aim, pitch, armed - declarative).");
            t->params = {P("aim", BbType::Bool, false), P("aimPitch", BbType::Float, 0.0f), P("armed", BbType::Bool, false)};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                c.ClaimStance(n);
                c.intent.stance.aim = c.ParamBool(n, 0);
                c.intent.stance.aimPitch = c.ParamFloat(n, 1);
                c.intent.stance.wantArmed = c.ParamBool(n, 2);
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("Jump", NodeCategory::Leaf, "Queues a jump.");
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t)
            {
                IntentCommand cmd;
                cmd.type = CommandType::Jump;
                c.intent.commands.push_back(cmd);
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("Wait", NodeCategory::Leaf, "Running for `seconds` (registers a timer, D9).");
            t->params = {P("seconds", BbType::Float, 1.0f)};
            t->builtins = {"remaining"};
            t->async = [](const CompiledNode&) { return true; };
            t->tick = [](TreeContext& c, uint32_t n) { return TickWait(c, n, c.ParamFloat(n, 0)); };
            Add(std::move(t));
        }
        {
            auto t = Make("WaitRandom", NodeCategory::Leaf, "Running for a random time in [min, max].");
            t->params = {P("min", BbType::Float, 0.5f), P("max", BbType::Float, 2.0f)};
            t->builtins = {"remaining"};
            t->async = [](const CompiledNode&) { return true; };
            t->tick = [](TreeContext& c, uint32_t n)
            {
                const NodeMemory& m = c.Mem(n);
                float seconds = 0.0f;
                if (!m.started)
                {
                    const float lo = c.ParamFloat(n, 0);
                    const float hi = std::max(lo, c.ParamFloat(n, 1));
                    seconds = lo + (hi - lo) * Random01(c.Self(), c.Node(n).id, c.Now());
                }
                return TickWait(c, n, seconds);
            };
            Add(std::move(t));
        }
        {
            auto t = Make("FindMarker", NodeCategory::Leaf,
                          "Looks a level marker up (WorldQuery) and writes its position into out. Waypoint: name = route, "
                          "index wraps; Zone: zone centre on the floor; Spawn: spawn point.");
            t->params = {E("kind", {"Waypoint", "Zone", "Spawn"}), P("name", BbType::String, std::string("patrol")),
                         P("index", BbType::Int, 0), K("out", {BbType::Vec3})};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                if (c.decision.world == nullptr)
                {
                    return NodeStatus::Failure;
                }
                const std::string kind = c.ParamString(n, 0);
                const MarkerKind k = kind == "Zone" ? MarkerKind::Zone : (kind == "Spawn" ? MarkerKind::Spawn : MarkerKind::Waypoint);
                const std::optional<glm::vec3> p = c.decision.world->FindMarker(k, c.ParamString(n, 1), c.ParamInt(n, 2));
                if (!p)
                {
                    c.Log(n, "marker not found");
                    return NodeStatus::Failure;
                }
                c.SetParamKey(n, 3, *p);
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("ProjectPoint", NodeCategory::Leaf,
                          "out = own position + normalize(direction) * distance (e.g. away from a wall along "
                          "perception.boundary.normal); facing = use the character's own forward instead of direction.");
            t->params = {P("direction", BbType::Vec3, glm::vec3(0.0f, 0.0f, -1.0f)), P("distance", BbType::Float, 3.0f),
                         K("out", {BbType::Vec3}), P("facing", BbType::Bool, false)};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                const BbValue d = c.Param(n, 0);
                glm::vec3 dir = std::holds_alternative<glm::vec3>(d) ? std::get<glm::vec3>(d) : glm::vec3(0.0f);
                if (c.ParamBool(n, 3) && c.decision.feedback != nullptr)
                {
                    dir = ForwardFromYaw(c.decision.feedback->motion.yawDeg);
                }
                dir.y = 0.0f;
                if (glm::length(dir) < 1e-4f || c.decision.feedback == nullptr)
                {
                    return NodeStatus::Failure;
                }
                const glm::vec3 p = c.decision.feedback->motion.position + glm::normalize(dir) * c.ParamFloat(n, 1);
                c.SetParamKey(n, 2, p);
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
        {
            auto t = Make("Log", NodeCategory::Leaf, "Writes text into the agent's tick log.");
            t->params = {P("text", BbType::String, std::string("hello"))};
            t->async = inherit;
            t->tick = [](TreeContext& c, uint32_t n)
            {
                c.Log(n, c.ParamString(n, 0));
                return NodeStatus::Success;
            };
            Add(std::move(t));
        }
    }

    NodeType& NodeTypeRegistry::Add(std::unique_ptr<NodeType> type)
    {
        mTypes.push_back(std::move(type));
        return *mTypes.back();
    }

    const NodeType* NodeTypeRegistry::Find(const std::string& name) const
    {
        for (const auto& t : mTypes)
        {
            if (t->name == name)
            {
                return t.get();
            }
        }
        return nullptr;
    }
} // namespace RAnimation
