#include <Behavior/BehaviorSelfTest.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/format.h>

#include <Agent/CharacterSystem.h>
#include <Agent/PerceptionKeys.h>
#include <Agent/Steering.h>
#include <Behavior/BehaviorSystem.h>
#include <Behavior/BehaviorTreeBrain.h>
#include <Model/ModelAndInstanceData.h>

namespace RAnimation
{
    namespace
    {
        int gChecks = 0;
        int gFails = 0;

        void Check(bool condition, const std::string& what)
        {
            ++gChecks;
            if (!condition)
            {
                ++gFails;
            }
            fmt::print("  [{}] {}\n", condition ? " ok " : "FAIL", what);
        }

        bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

        // ---- tree building --------------------------------------------------------------------------------

        struct Builder
        {
            TreeDesc d;
            explicit Builder(float hz = 10.0f) { d.name = "test"; d.tickHz = hz; }

            // params: name -> "@key" (binding) or a literal written as text
            uint32_t Add(const char* type, std::vector<uint32_t> children = {}, std::vector<std::pair<std::string, std::string>> params = {})
            {
                BtNodeDesc n;
                n.id = d.nextId++;
                n.type = type;
                n.children = std::move(children);
                for (auto& [name, value] : params)
                {
                    ParamDesc p;
                    p.name = name;
                    if (!value.empty() && value.front() == '@')
                    {
                        p.bound = true;
                        p.key = value.substr(1);
                    }
                    else
                    {
                        p.literal = value;
                    }
                    n.params.push_back(std::move(p));
                }
                d.nodes.push_back(std::move(n));
                return d.nodes.back().id;
            }
            void Key(const char* name, BbType type, BbScope scope, BbValue def, bool wake = false)
            {
                BlackboardKeyDecl k;
                k.name = name;
                k.type = type;
                k.scope = scope;
                k.defaultValue = std::move(def);
                k.wakeOnChange = wake;
                d.keys.push_back(std::move(k));
            }
            void NodeKey(uint32_t node, const char* name, BbType type, BbValue def)
            {
                BlackboardKeyDecl k;
                k.name = name;
                k.type = type;
                k.scope = BbScope::Node;
                k.defaultValue = std::move(def);
                k.ownerNode = node;
                d.Find(node)->nodeKeys.push_back(std::move(k));
            }
            void Root(uint32_t id) { d.rootId = id; }
        };

        std::shared_ptr<const CompiledTree> Compile(BehaviorSystem& sys, const Builder& b, std::vector<ValidationError>& errors,
                                                    const std::string& path = "")
        {
            auto tree = CompileTree(std::make_shared<TreeDesc>(b.d), path, sys.Types(), sys.GlobalSchema(), errors);
            if (tree && !path.empty())
            {
                sys.ReplaceTree(path, tree);
            }
            return tree;
        }

        bool HasError(const std::vector<ValidationError>& errors, const std::string& fragment)
        {
            for (const ValidationError& e : errors)
            {
                if (e.message.find(fragment) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        // One brain driven by hand: fake feedback, world and clock.
        struct Harness final : public WorldQuery
        {
            BehaviorSystem& sys;
            std::shared_ptr<const CompiledTree> tree;
            std::unique_ptr<BehaviorTreeBrain> brain;
            Blackboard bb;
            AgentIntent intent;
            ExecutionFeedback fb;
            AgentRequestAllocator requests;
            float time = 0.0f;

            Harness(BehaviorSystem& s, std::shared_ptr<const CompiledTree> t, EntityId self = 1) : sys(s), tree(std::move(t))
            {
                brain = std::make_unique<BehaviorTreeBrain>(&sys, tree, self, 0.0f);
                bb.Init(tree->instanceSchema);
                brain->BindInstanceBlackboard(&bb);
            }
            NodeStatus TickAt(float t)
            {
                time = t;
                DecisionContext c;
                c.worldTime = t;
                c.self = brain->Self();
                c.feedback = &fb;
                c.instanceBlackboard = &bb;
                c.globalBlackboard = &sys.Global();
                c.requests = &requests;
                c.world = this;
                brain->Tick(c, intent);
                return brain->RootStatus();
            }
            NodeStatus Status(uint32_t id) const { return brain->Instance().status[static_cast<size_t>(tree->IndexOf(id))]; }
            const NodeMemory& Mem(uint32_t id) const { return brain->Instance().memory[static_cast<size_t>(tree->IndexOf(id))]; }
            BbValue Get(const char* key) const { return bb.Get(static_cast<uint32_t>(bb.Find(key))); }
            void Set(const char* key, BbValue v) { bb.SetByName(key, std::move(v), kInvalidEntityId); }

            std::optional<glm::vec3> Position(EntityId id) const override
            {
                if (id == 42)
                    return glm::vec3(5.0f, 0.0f, 0.0f);
                return std::nullopt;
            }
            std::optional<float> YawDeg(EntityId) const override { return std::nullopt; }
            EntityId PlayerEntity() const override { return 42; }
            std::optional<glm::vec3> FindMarker(MarkerKind kind, const std::string& name, int index) const override
            {
                if (kind == MarkerKind::Waypoint && name == "patrol")
                    return glm::vec3(static_cast<float>(((index % 3) + 3) % 3), 0.0f, 0.0f);
                return std::nullopt;
            }
            int RouteLength(const std::string& route) const override { return route == "patrol" ? 3 : 0; }
        };

        // ---- composites -----------------------------------------------------------------------------------

        void TestComposites(BehaviorSystem& sys)
        {
            fmt::print("\n-- composites (D5) --\n");
            std::vector<ValidationError> errors;
            {
                Builder b;
                b.Key("go", BbType::Bool, BbScope::Instance, true);
                const uint32_t cond = b.Add("BlackboardCompare", {}, {{"key", "@go"}, {"op", "=="}, {"value", "true"}});
                const uint32_t wait = b.Add("Wait", {}, {{"seconds", "10"}});
                b.Root(b.Add("ReactiveSequence", {cond, wait}));
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Running && h.Status(wait) == NodeStatus::Running, "ReactiveSequence: condition true -> last child runs");
                h.Set("go", false);
                Check(h.TickAt(0.1f) == NodeStatus::Failure, "ReactiveSequence: condition turns false -> Failure the same tick");
                Check(h.Status(wait) == NodeStatus::Idle && std::isinf(h.Mem(wait).wakeAt), "  ... and the running Wait was halted (timer cleared)");
            }
            {
                Builder b;
                b.Key("alarm", BbType::Bool, BbScope::Instance, false);
                const uint32_t cond = b.Add("BlackboardCompare", {}, {{"key", "@alarm"}, {"op", "=="}, {"value", "true"}});
                const uint32_t wait = b.Add("Wait", {}, {{"seconds", "10"}});
                b.Root(b.Add("ReactiveSelector", {cond, wait}));
                Harness h(sys, Compile(sys, b, errors));
                h.TickAt(0.0f);
                h.Set("alarm", true);
                Check(h.TickAt(0.1f) == NodeStatus::Success && h.Status(wait) == NodeStatus::Idle,
                      "ReactiveSelector: higher-priority child succeeds -> lower running child halted");
            }
            {
                Builder b;
                b.Key("x", BbType::Int, BbScope::Instance, 0);
                const uint32_t wait = b.Add("Wait", {}, {{"seconds", "1"}});
                const uint32_t set = b.Add("SetBlackboard", {}, {{"key", "@x"}, {"value", "7"}});
                const uint32_t seq = b.Add("MemSequence", {wait, set});
                b.Root(seq);
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Running && h.Mem(seq).index == 0, "MemSequence: first child running, index 0");
                Check(h.TickAt(0.5f) == NodeStatus::Running, "MemSequence: still running at 0.5 s");
                Check(h.TickAt(1.0f) == NodeStatus::Success && std::get<int>(h.Get("x")) == 7 && h.Mem(seq).index == 0,
                      "MemSequence: Wait done -> next child in the same tick -> Success, index back to 0");
            }
            {
                Builder b;
                b.Key("x", BbType::Int, BbScope::Instance, 0);
                const uint32_t ok = b.Add("SetBlackboard", {}, {{"key", "@x"}, {"value", "1"}});
                const uint32_t bad = b.Add("BlackboardCompare", {}, {{"key", "@x"}, {"op", ">"}, {"value", "5"}});
                const uint32_t seq = b.Add("MemSequence", {ok, bad});
                b.Root(seq);
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Failure && h.Mem(seq).index == 0, "MemSequence: Failure resets the index");
            }
            {
                Builder b;
                const uint32_t w1 = b.Add("Wait", {}, {{"seconds", "1"}});
                const uint32_t w2 = b.Add("Wait", {}, {{"seconds", "2"}});
                const uint32_t f = b.Add("ForceFailure", {b.Add("Log", {}, {{"text", "x"}})});
                const uint32_t par = b.Add("Parallel", {w1, w2, f}, {{"successThreshold", "2"}});
                b.Root(par);
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Running, "Parallel(2 of 3): one failure is still recoverable -> Running");
                Check(h.TickAt(1.0f) == NodeStatus::Running && h.Status(w1) == NodeStatus::Success, "Parallel: finished children are not ticked again");
                Check(h.TickAt(2.0f) == NodeStatus::Success && h.Mem(par).doneMask == 0, "Parallel: 2 successes -> Success, counts reset");
            }
            {
                Builder b;
                const uint32_t f1 = b.Add("ForceFailure", {b.Add("Log", {}, {{"text", "a"}})});
                const uint32_t f2 = b.Add("ForceFailure", {b.Add("Log", {}, {{"text", "b"}})});
                const uint32_t w = b.Add("Wait", {}, {{"seconds", "5"}});
                b.Root(b.Add("Parallel", {w, f1, f2}, {{"successThreshold", "2"}}));
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Failure && h.Status(w) == NodeStatus::Idle,
                      "Parallel(2 of 3): two failures -> N unreachable -> Failure, running child halted");
            }
        }

        // ---- decorators -----------------------------------------------------------------------------------

        void TestDecorators(BehaviorSystem& sys)
        {
            fmt::print("\n-- decorators (D7) --\n");
            std::vector<ValidationError> errors;
            {
                Builder b;
                b.Key("n", BbType::Int, BbScope::Instance, 0);
                const uint32_t add = b.Add("AddBlackboard", {}, {{"key", "@n"}, {"delta", "1"}});
                b.Root(b.Add("Repeat", {add}, {{"count", "3"}}));
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Success && std::get<int>(h.Get("n")) == 3, "Repeat(3) over a synchronous child: 3 loops, Success in one tick");
            }
            {
                Builder b;
                b.Key("n", BbType::Int, BbScope::Instance, 0);
                const uint32_t add = b.Add("AddBlackboard", {}, {{"key", "@n"}, {"delta", "1"}});
                const uint32_t rep = b.Add("Repeat", {add}, {{"count", "-1"}});
                b.Root(rep);
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Running && std::get<int>(h.Get("n")) == 64, "Repeat(-1): stops at 64 loops per tick, Running");
                Check(h.brain->NextWakeTime() <= 0.0f, "  ... and asks to be ticked again right away");
                Check(h.tree->nodes[0].async, "  Repeat(-1) counts as asynchronous");
            }
            {
                Builder b;
                b.Key("n", BbType::Int, BbScope::Instance, 0);
                const uint32_t add = b.Add("AddBlackboard", {}, {{"key", "@n"}, {"delta", "1"}});
                const uint32_t f = b.Add("ForceFailure", {add});
                b.Root(b.Add("Retry", {f}, {{"count", "3"}}));
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Failure && std::get<int>(h.Get("n")) == 3, "Retry(3): three attempts, then Failure");
            }
            {
                Builder b(10.0f);
                const uint32_t wait = b.Add("Wait", {}, {{"seconds", "10"}});
                b.Root(b.Add("Timeout", {wait}, {{"seconds", "0.25"}}));
                Harness h(sys, Compile(sys, b, errors));
                h.TickAt(0.0f);
                Check(Near(h.brain->NextWakeTime(), 0.25f), "Timeout: wake time is the exact deadline (0.25 s), not the next 10 Hz tick");
                Check(h.TickAt(0.25f) == NodeStatus::Failure && h.Status(wait) == NodeStatus::Idle, "Timeout: at the deadline the child is halted and it fails");
            }
            {
                Builder b;
                b.Key("n", BbType::Int, BbScope::Instance, 0);
                const uint32_t add = b.Add("AddBlackboard", {}, {{"key", "@n"}, {"delta", "1"}});
                b.Root(b.Add("Cooldown", {add}, {{"seconds", "2"}}));
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Success, "Cooldown: first run passes");
                Check(h.TickAt(1.0f) == NodeStatus::Failure && std::get<int>(h.Get("n")) == 1, "Cooldown: during the cooldown it fails without ticking the child");
                Check(Near(h.brain->NextWakeTime(), 2.0f), "Cooldown: registers the ready time as wake time");
                Check(h.TickAt(2.0f) == NodeStatus::Success && std::get<int>(h.Get("n")) == 2, "Cooldown: ready again at 2 s");
            }
            {
                Builder b;
                b.Key("ok", BbType::Bool, BbScope::Instance, true);
                const uint32_t wait = b.Add("Wait", {}, {{"seconds", "5"}});
                b.Root(b.Add("BlackboardCondition", {wait}, {{"key", "@ok"}, {"op", "=="}, {"value", "true"}}));
                Harness h(sys, Compile(sys, b, errors));
                h.TickAt(0.0f);
                h.Set("ok", false);
                Check(h.TickAt(0.1f) == NodeStatus::Failure && h.Status(wait) == NodeStatus::Idle, "BlackboardCondition: false when ticked -> child halted, Failure");
            }
            {
                Builder b;
                const uint32_t inv = b.Add("Inverter", {b.Add("Log", {}, {{"text", "x"}})});
                const uint32_t fs = b.Add("ForceSuccess", {b.Add("ForceFailure", {b.Add("Log", {}, {{"text", "y"}})})});
                b.Root(b.Add("MemSelector", {inv, fs}));
                Harness h(sys, Compile(sys, b, errors));
                Check(h.TickAt(0.0f) == NodeStatus::Success && h.Status(inv) == NodeStatus::Failure && h.Status(fs) == NodeStatus::Success,
                      "Inverter / ForceSuccess / ForceFailure / MemSelector");
            }
        }

        // ---- blackboard scopes and reset rules ------------------------------------------------------------

        void TestBlackboard(BehaviorSystem& sys)
        {
            fmt::print("\n-- blackboard (D11, D12, §6.4) --\n");
            BlackboardStore s;
            BlackboardKeyDecl d;
            d.name = "p";
            d.type = BbType::Vec3;
            d.defaultValue = glm::vec3(0.0f);
            s.Init(std::vector<BlackboardKeyDecl>{d});
            s.Set(0, glm::vec3(1.0f, 2.0f, 3.0f), 0);
            Check(!s.Set(0, glm::vec3(1.00005f, 2.0f, 3.0f), 0), "vec3 change below 1e-4 is not a change");
            Check(s.Set(0, glm::vec3(1.001f, 2.0f, 3.0f), 0), "vec3 change above 1e-4 is a change");

            std::vector<ValidationError> errors;
            Builder b;
            b.Key("mine", BbType::Int, BbScope::Instance, 0);
            b.Key("shared", BbType::Int, BbScope::TreeShared, 0);
            const uint32_t setMine = b.Add("SetBlackboard", {}, {{"key", "@mine"}, {"value", "5"}});
            const uint32_t addShared = b.Add("AddBlackboard", {}, {{"key", "@shared"}, {"delta", "1"}});
            const uint32_t addNode = b.Add("AddBlackboard", {}, {{"key", "@visits"}, {"delta", "1"}});
            const uint32_t wait = b.Add("Wait", {}, {{"seconds", "5"}});
            b.NodeKey(addNode, "visits", BbType::Int, 0);
            b.Root(b.Add("MemSequence", {setMine, addShared, addNode, wait}));
            auto tree = Compile(sys, b, errors, "selftest/scopes");
            Check(tree != nullptr, "scope test tree compiles" + (errors.empty() ? std::string() : ": " + errors.front().message));
            if (!tree)
                return;
            Harness a(sys, tree, 1);
            Harness c(sys, tree, 2);
            a.TickAt(0.0f);
            c.TickAt(0.0f);
            BlackboardStore* shared = sys.TreeShared("selftest/scopes");
            Check(std::get<int>(shared->Get(0)) == 2, "TreeShared: one store for both agents (2 increments)");
            Check(std::get<int>(a.Get("mine")) == 5 && std::get<int>(c.Get("mine")) == 5 && &a.bb != &c.bb, "Instance: each agent has its own value");
            const int nodeIndex = tree->IndexOf(addNode);
            Check(std::get<int>(a.brain->Instance().nodeKeys[static_cast<size_t>(nodeIndex)].Get(0)) == 1 &&
                          std::get<int>(c.brain->Instance().nodeKeys[static_cast<size_t>(nodeIndex)].Get(0)) == 1,
                  "Node keys: per agent, per node");
            a.Set(PerceptionKeys::kPlayerDistance, 3.0f);
            a.brain->Halt(a.intent);
            Check(std::get<int>(a.brain->Instance().nodeKeys[static_cast<size_t>(nodeIndex)].Get(0)) == 1 && std::get<int>(a.Get("mine")) == 5,
                  "Halt: user node keys and instance keys are kept");
            Check(a.Status(wait) == NodeStatus::Idle, "Halt: running nodes become Idle");
            a.brain->Reset();
            Check(std::get<int>(a.brain->Instance().nodeKeys[static_cast<size_t>(nodeIndex)].Get(0)) == 0, "Reset: user node keys back to default");
            Check(std::get<int>(a.Get("mine")) == 0 && !a.bb.IsSet(static_cast<uint32_t>(a.bb.Find("mine"))), "Reset: the tree's instance keys back to default");
            Check(Near(std::get<float>(a.Get(PerceptionKeys::kPlayerDistance)), 3.0f), "Reset: perception keys are kept");
            Check(std::get<int>(shared->Get(0)) == 2, "Reset: TreeShared is kept");
        }

        // ---- validator (§9.2) -----------------------------------------------------------------------------

        void TestValidator(BehaviorSystem& sys)
        {
            fmt::print("\n-- validator (one counter-example per rule) --\n");
            std::vector<ValidationError> errors;
            {
                Builder b;
                const uint32_t a = b.Add("Log");
                b.Add("Log"); // orphan
                b.Root(a);
                Check(!Compile(sys, b, errors) && HasError(errors, "not connected"), "rule 1: a second root (unconnected node) is rejected");
            }
            {
                Builder b;
                const uint32_t s = b.Add("MemSequence");
                const uint32_t inv = b.Add("Inverter", {s});
                b.d.Find(s)->children = {inv};
                b.Root(s);
                Check(!Compile(sys, b, errors) && (HasError(errors, "cycle") || HasError(errors, "parent")), "rule 1: a cycle is rejected");
            }
            {
                Builder b;
                const uint32_t leaf = b.Add("Log");
                const uint32_t s1 = b.Add("MemSequence", {leaf});
                const uint32_t s2 = b.Add("MemSequence", {leaf});
                b.Root(b.Add("MemSelector", {s1, s2}));
                Check(!Compile(sys, b, errors) && HasError(errors, "more than one parent"), "rule 1: two parents are rejected");
            }
            {
                Builder b;
                b.Root(b.Add("Inverter", {b.Add("Log"), b.Add("Log")}));
                Check(!Compile(sys, b, errors) && HasError(errors, "exactly one child"), "rule 2: a decorator with two children is rejected");
            }
            {
                Builder b;
                b.Root(b.Add("MemSequence"));
                Check(!Compile(sys, b, errors) && HasError(errors, "1 to 64"), "rule 2: an empty composite is rejected");
            }
            {
                Builder b;
                b.Root(b.Add("Log", {b.Add("Log")}));
                Check(!Compile(sys, b, errors) && HasError(errors, "leaf"), "rule 2: a leaf with a child is rejected");
            }
            {
                Builder b;
                b.Root(b.Add("Parallel", {b.Add("Log"), b.Add("Log")}, {{"successThreshold", "3"}}));
                Check(!Compile(sys, b, errors) && HasError(errors, "Parallel needs"), "rule 3: Parallel with N > M is rejected");
            }
            {
                Builder b;
                b.Root(b.Add("ReactiveSequence", {b.Add("Wait", {}, {{"seconds", "1"}}), b.Add("Log")}));
                Check(!Compile(sys, b, errors) && HasError(errors, "only the last child"), "rule 4 (D6): an asynchronous non-last child of a reactive node is rejected");
            }
            {
                Builder b;
                b.Root(b.Add("ReactiveSelector", {b.Add("Log"), b.Add("Wait", {}, {{"seconds", "1"}})}));
                Check(Compile(sys, b, errors) != nullptr, "rule 4: an asynchronous LAST child is fine");
            }
            {
                Builder b;
                b.Root(b.Add("SetBlackboard", {}, {{"key", "@nope"}, {"value", "1"}}));
                Check(!Compile(sys, b, errors) && HasError(errors, "unknown key"), "rule 5: an unresolved binding is rejected");
            }
            {
                Builder b;
                b.Key("name", BbType::String, BbScope::Instance, std::string());
                b.Root(b.Add("MoveTo", {}, {{"target", "@name"}}));
                Check(!Compile(sys, b, errors) && HasError(errors, "cannot bind"), "rule 5: a type mismatch (String key as MoveTo target) is rejected");
            }
            {
                Builder b;
                b.Key("alarmLevel", BbType::Int, BbScope::Global, 0);
                b.Root(b.Add("Log"));
                Check(!Compile(sys, b, errors) && HasError(errors, "not in global"), "rule 5: a global key missing from the global schema is rejected");
            }
            {
                Builder b;
                const uint32_t owner = b.Add("AddBlackboard", {}, {{"key", "@count"}, {"delta", "1"}});
                b.NodeKey(owner, "count", BbType::Int, 0);
                const uint32_t other = b.Add("SetBlackboard", {}, {{"key", "@count"}, {"value", "3"}});
                b.Root(b.Add("MemSequence", {owner, other}));
                Check(!Compile(sys, b, errors) && HasError(errors, "not visible"), "rule 6 (D12): another node binding a node key is rejected");
            }
            {
                Builder b;
                const uint32_t owner = b.Add("Wait", {}, {{"seconds", "1"}});
                b.NodeKey(owner, "tries", BbType::Int, 0);
                const uint32_t cond = b.Add("BlackboardCondition", {owner}, {{"key", "@tries"}, {"op", "<"}, {"value", "3"}});
                b.Root(b.Add("ForceSuccess", {cond}));
                Check(Compile(sys, b, errors) != nullptr, "rule 6: the decorator directly above the owner may use its node key");
            }
            {
                Builder b;
                b.Key("dup", BbType::Int, BbScope::Instance, 0);
                b.Key("dup", BbType::Float, BbScope::TreeShared, 0.0f);
                b.Root(b.Add("Log"));
                Check(!Compile(sys, b, errors) && HasError(errors, "declared twice"), "rule 7: duplicate key names are rejected");
            }
        }

        // ---- intent ownership (D14) -----------------------------------------------------------------------

        void TestIntent(BehaviorSystem& sys)
        {
            fmt::print("\n-- intent channels (D14) --\n");
            std::vector<ValidationError> errors;
            Builder b;
            b.Key("go", BbType::Bool, BbScope::Instance, true);
            b.Key("dest", BbType::Vec3, BbScope::Instance, glm::vec3(10.0f, 0.0f, 0.0f));
            const uint32_t stance = b.Add("SetStance", {}, {{"aim", "true"}, {"aimPitch", "0"}, {"armed", "true"}});
            const uint32_t cond = b.Add("BlackboardCompare", {}, {{"key", "@go"}, {"op", "=="}, {"value", "true"}});
            const uint32_t set = b.Add("SetBlackboard", {}, {{"key", "@dest"}, {"value", "10, 0, 0"}});
            const uint32_t move = b.Add("MoveTo", {}, {{"target", "@dest"}, {"gait", "Jog"}});
            const uint32_t inner = b.Add("MemSequence", {set, move});
            const uint32_t reactive = b.Add("ReactiveSequence", {cond, inner});
            b.Root(b.Add("MemSequence", {stance, b.Add("ForceSuccess", {reactive})}));
            Harness h(sys, Compile(sys, b, errors));
            h.TickAt(0.0f);
            Check(h.intent.locomotion.mode == LocomotionMode::MoveTo && h.intent.locomotionOwner == static_cast<uint32_t>(h.tree->IndexOf(move)) &&
                          h.intent.locomotion.gait == Gait::Jog,
                  "MoveTo owns the locomotion channel");
            Check(h.intent.stance.wantArmed && h.intent.stanceOwner == static_cast<uint32_t>(h.tree->IndexOf(stance)), "SetStance owns the stance channel");
            h.Set("go", false);
            h.TickAt(0.1f);
            Check(h.intent.locomotion.mode == LocomotionMode::None && h.intent.locomotionOwner == kNoOwner, "preempted MoveTo hands its channel back");
            Check(h.intent.stance.wantArmed, "  ... but only its own: the stance written by another node stays");
            h.fb.motion.position = glm::vec3(9.8f, 0.0f, 0.0f);
            h.Set("go", true);
            h.TickAt(0.2f);
            Check(h.Status(move) == NodeStatus::Success, "MoveTo: within arriveRadius -> Success");
        }

        // ---- PlayAction / requests ------------------------------------------------------------------------

        void TestRequests(BehaviorSystem& sys)
        {
            fmt::print("\n-- PlayAction and request status (§3.3, D4, D15) --\n");
            ExecutionFeedback empty;
            Check(empty.Query(57) == RequestStatus::Cancelled, "an unknown request id reads Cancelled (what a load leaves behind)");
            std::vector<ValidationError> errors;
            Builder b;
            b.Key("stop", BbType::Bool, BbScope::Instance, false);
            const uint32_t cond = b.Add("BlackboardCompare", {}, {{"key", "@stop"}, {"op", "=="}, {"value", "false"}});
            const uint32_t play = b.Add("PlayAction", {}, {{"montage", "talk"}, {"waitForCompletion", "true"}, {"cancelOnHalt", "true"}});
            b.Root(b.Add("ReactiveSequence", {cond, play}));
            Harness h(sys, Compile(sys, b, errors));
            h.TickAt(0.0f);
            Check(h.intent.commands.size() == 1 && h.intent.commands[0].type == CommandType::Action && h.intent.commands[0].id == 1,
                  "PlayAction queues an Action command with a fresh request id");
            const RequestId id = h.intent.commands[0].id;
            h.intent.commands.clear();
            h.fb.requests[id] = RequestStatus::Accepted;
            Check(h.TickAt(0.1f) == NodeStatus::Running, "Accepted -> Running");
            h.fb.requests[id] = RequestStatus::Completed;
            Check(h.TickAt(0.2f) == NodeStatus::Success, "Completed -> Success");
            h.TickAt(0.3f);
            const RequestId second = h.intent.commands.back().id;
            h.intent.commands.clear();
            h.fb.requests[second] = RequestStatus::Accepted;
            h.Set("stop", true);
            h.TickAt(0.4f);
            Check(!h.intent.commands.empty() && h.intent.commands.back().type == CommandType::CancelAction && h.intent.commands.back().cancelTarget == second,
                  "halted PlayAction (cancelOnHalt) sends CancelAction for its request");
            h.intent.commands.clear();
            h.Set("stop", false);
            h.TickAt(0.5f);
            const RequestId third = h.intent.commands.back().id;
            h.fb.requests.clear(); // a load: the table is empty
            Check(h.TickAt(0.6f) == NodeStatus::Failure && third > second, "request missing from the table (after a load) -> Cancelled -> Failure");
        }

        // ---- scheduling (D9, D10) -------------------------------------------------------------------------

        void TestScheduling(BehaviorSystem& sys)
        {
            fmt::print("\n-- scheduling (D9, D10) --\n");
            std::vector<ValidationError> errors;
            Builder b(10.0f);
            b.Key("flag", BbType::Bool, BbScope::Instance, false, /*wake*/ true);
            b.Key("note", BbType::Int, BbScope::TreeShared, 0, /*wake*/ true);
            b.Key("own", BbType::Int, BbScope::Instance, 0, /*wake*/ true);
            const uint32_t self = b.Add("AddBlackboard", {}, {{"key", "@own"}, {"delta", "1"}});
            const uint32_t wait = b.Add("Wait", {}, {{"seconds", "0.25"}});
            b.Root(b.Add("MemSequence", {self, wait}));
            auto tree = Compile(sys, b, errors, "selftest/sched");
            if (!tree)
            {
                Check(false, "scheduling tree compiles: " + errors.front().message);
                return;
            }
            ModelAndInstanceData scene;
            CharacterSystem cs;
            sys.wake = [&cs](EntityId id) { cs.WakeNextFrame(id); };
            auto brainFor = [&](EntityId id) { return std::make_unique<BehaviorTreeBrain>(&sys, tree, id, 0.0f); };
            Agent* a = cs.AddHeadlessAgent(3, brainFor(3));
            Agent* c = cs.AddHeadlessAgent(5, brainFor(5));
            Check(a->schedule.accumulator != c->schedule.accumulator && a->schedule.accumulator >= 0.0f && a->schedule.accumulator < 0.1f,
                  "phase offsets differ per id and lie within one interval");
            CharacterSystem cs2;
            Agent* a2 = cs2.AddHeadlessAgent(3, brainFor(3));
            Check(a2->schedule.accumulator == a->schedule.accumulator, "phase offset is reproducible (derived from the stable id)");
            cs2.Clear();

            const float dt = 1.0f / 60.0f;
            PerceptionInputs none;
            cs.Update(dt, scene, false, none); // first frame: both tick (fresh brains are woken)
            const uint64_t t0 = a->schedule.ticks;
            Check(t0 == 1 && c->schedule.ticks == 1, "a new agent ticks on its first frame");
            int frames = 0;
            while (cs.WorldTime() < 0.24f)
            {
                cs.Update(dt, scene, false, none);
                ++frames;
            }
            Check(a->schedule.ticks >= 2 && a->schedule.ticks <= 4, fmt::format("10 Hz over ~0.24 s at 60 fps: {} ticks", a->schedule.ticks));
            Check(std::get<int>(a->instanceBlackboard.Get(static_cast<uint32_t>(a->instanceBlackboard.Find("own")))) == 1,
                  "its own wake-key writes did not wake it (one AddBlackboard, MemSequence still waiting)");
            // timer: Wait(0.25) started at t = dt -> due exactly when worldTime >= 0.25 + dt
            const uint64_t before = a->schedule.ticks;
            while (cs.WorldTime() < 0.25f + dt - 1e-5f)
            {
                cs.Update(dt, scene, false, none);
            }
            Check(a->schedule.ticks > before && a->schedule.lastTickTime >= 0.25f, "timer wake: ticked at the Wait deadline");

            // key change between frames -> due on the next update (perception-like)
            uint64_t ticksA = a->schedule.ticks;
            a->schedule.accumulator = 0.0f;
            a->instanceBlackboard.SetByName("flag", true, kInvalidEntityId);
            cs.Update(dt, scene, false, none);
            Check(a->schedule.ticks == ticksA + 1 && a->schedule.lastTickWasWake, "key change wake: ticked on the next frame");

            // tree shared write by agent 3 during its tick wakes agent 5 next frame, not this frame
            Builder w(0.0f); // every frame; writes only while its instance key doWrite is true
            w.Key("note", BbType::Int, BbScope::TreeShared, 0, true);
            w.Key("doWrite", BbType::Bool, BbScope::Instance, false);
            const uint32_t add = w.Add("AddBlackboard", {}, {{"key", "@note"}, {"delta", "1"}});
            w.Root(w.Add("ForceSuccess", {w.Add("BlackboardCondition", {add}, {{"key", "@doWrite"}, {"op", "=="}, {"value", "true"}})}));
            auto writer = Compile(sys, w, errors, "selftest/shared");
            Builder rd(0.0f);
            rd.Key("note", BbType::Int, BbScope::TreeShared, 0, true);
            rd.Key("doWrite", BbType::Bool, BbScope::Instance, false);
            rd.Root(rd.Add("Log", {}, {{"text", "reader"}}));
            auto reader = CompileTree(std::make_shared<TreeDesc>(rd.d), "selftest/shared", sys.Types(), sys.GlobalSchema(), errors);
            cs.SetBrain(*a, std::make_unique<BehaviorTreeBrain>(&sys, writer, 3, 0.0f));
            cs.SetBrain(*c, std::make_unique<BehaviorTreeBrain>(&sys, reader, 5, 0.5f)); // 0.5 Hz: only wake-ups make it tick
            cs.Update(dt, scene, false, none); // both fresh: tick, nobody writes
            cs.Update(dt, scene, false, none);
            ticksA = a->schedule.ticks;
            const uint64_t ticksC = c->schedule.ticks;
            c->schedule.accumulator = 0.0f;
            a->instanceBlackboard.SetByName("doWrite", true, 3);
            cs.Update(dt, scene, false, none); // a writes note while being ticked -> c woken for the NEXT frame
            Check(c->schedule.ticks == ticksC && c->schedule.wakeFlag, "a wake raised during scheduling is deferred to the next frame");
            a->instanceBlackboard.SetByName("doWrite", false, 3);
            cs.Update(dt, scene, false, none);
            Check(c->schedule.ticks == ticksC + 1, "  ... and runs there");
            Check(a->schedule.ticks == ticksA + 2, "an agent ticks at most once per frame");
            cs.Update(dt, scene, true, none);
            Check(a->schedule.ticks == ticksA + 2, "paused (Edit mode): nobody ticks, world time stops");
            sys.wake = nullptr;
            cs.Clear();
        }

        // ---- steering (§3.2) ------------------------------------------------------------------------------

        void TestSteering()
        {
            fmt::print("\n-- steering (§3.2) --\n");
            SteeringSettings s;
            SteeringMemory mem;
            ExecutionFeedback fb;
            MotionState m;
            m.yawDeg = 90.0f; // facing -X
            AgentIntent intent;
            intent.locomotion.mode = LocomotionMode::MoveTo;
            intent.locomotion.arriveRadius = 0.5f;
            SteeringResult r;
            Steer(intent, m, fb, glm::vec3(-10.0f, 0.0f, 0.0f), s, mem, r);
            Check(Near(r.input.moveY, 1.0f, 1e-3f) && Near(r.input.moveX, 0.0f, 1e-3f) && r.input.turn == 0.0f,
                  "target straight ahead in the local basis (yaw 90 -> -X): moveY = 1, no turn");
            Steer(intent, m, fb, glm::vec3(0.0f, 0.0f, -10.0f), s, mem, r);
            Check(Near(r.input.moveX, 1.0f, 1e-3f) && r.input.turn > 0.0f, "target on the right: strafe right (+moveX) and turn right (turn > 0)");
            Steer(intent, m, fb, glm::vec3(-1.0f, 0.0f, 0.0f), s, mem, r);
            Check(r.input.MoveMagnitude() < 1.0f && r.input.MoveMagnitude() > 0.0f, "inside arriveRadius x slowdownFactor: slowed down");
            Steer(intent, m, fb, glm::vec3(-0.4f, 0.0f, 0.0f), s, mem, r);
            Check(r.arrived && r.input.MoveMagnitude() == 0.0f, "inside arriveRadius: stop, arrived");
            intent.locomotion.mode = LocomotionMode::FaceTo;
            const glm::vec3 slightlyLeft = glm::vec3(-10.0f, 0.0f, 0.0f) + 10.0f * glm::vec3(0.0f, 0.0f, std::tan(glm::radians(2.0f)));
            Steer(intent, m, fb, slightlyLeft, s, mem, r);
            Check(r.input.turn == 0.0f, "yaw error inside the dead zone: no turn (no jitter)");
            intent.locomotion.mode = LocomotionMode::None;
            intent.stance.wantArmed = true;
            Steer(intent, m, fb, std::nullopt, s, mem, r);
            const bool sent = r.commands.size() == 1 && r.commands[0].type == CommandType::ToggleWeapon;
            Steer(intent, m, fb, std::nullopt, s, mem, r);
            Check(sent && r.commands.empty(), "wantArmed: one ToggleWeapon, not repeated while the feedback has not flipped");
            fb.armed = true;
            Steer(intent, m, fb, std::nullopt, s, mem, r);
            Check(r.commands.empty() && !mem.toggleSent, "feedback armed: nothing more to send");
            intent.direct = CharacterInput{};
            intent.direct->moveX = 0.3f;
            Steer(intent, m, fb, glm::vec3(5.0f), s, mem, r);
            Check(Near(r.input.moveX, 0.3f), "direct input (player / script) is passed through");
        }

        // ---- files ----------------------------------------------------------------------------------------

        void TestFiles(BehaviorSystem& sys, const std::filesystem::path& dir)
        {
            fmt::print("\n-- tree asset / global blackboard / checkpoint round trips (§8) --\n");
            std::vector<ValidationError> errors;
            Builder b;
            b.d.name = "roundtrip";
            b.d.doc = "round trip";
            b.Key("cover.point", BbType::Vec3, BbScope::Instance, glm::vec3(1.0f, 0.0f, 2.0f), true);
            b.Key("alarm", BbType::Bool, BbScope::TreeShared, false, true);
            const uint32_t move = b.Add("MoveTo", {}, {{"target", "@cover.point"}, {"gait", "Jog"}, {"arriveRadius", "0.5"}});
            b.NodeKey(move, "visits", BbType::Int, 3);
            const uint32_t log = b.Add("Log", {}, {{"text", "@literal with at"}});
            b.d.Find(log)->params[0] = ParamDesc{"text", false, {}, std::string("@literal with at")};
            b.Root(b.Add("MemSequence", {move, log}));
            b.d.Find(move)->editorPos = glm::vec2(12.0f, 34.0f);
            const std::string path = (dir / "roundtrip.behaviortree.usda").generic_string();
            std::string error;
            Check(WriteTreeDesc(b.d, sys.Types(), path, error), "write .behaviortree.usda");
            TreeDesc back;
            Check(ReadTreeDesc(path, back, error), "read it back" + (error.empty() ? std::string() : ": " + error));
            Check(ComputeContentHash(back) == ComputeContentHash(b.d), "content hash survives the round trip");
            Check(back.Find(move) != nullptr && back.Find(move)->editorPos == glm::vec2(12.0f, 34.0f) && back.Find(move)->nodeKeys.size() == 1,
                  "editor position and node keys survive");
            const ParamDesc* text = back.Find(log) ? back.Find(log)->FindParam("text") : nullptr;
            Check(text != nullptr && !text->bound && std::get<std::string>(text->literal) == "@literal with at", "a literal starting with '@' stays a literal");
            Check(back.keys.size() == 2 && back.keys[0].name == "cover.point" && back.keys[0].wakeOnChange, "key names with dots survive (escaped prims)");

            BlackboardSchema g;
            BlackboardKeyDecl k;
            k.name = "world.alarm";
            k.type = BbType::Int;
            k.scope = BbScope::Global;
            k.defaultValue = 2;
            k.wakeOnChange = true;
            g.keys.push_back(k);
            const std::string gpath = (dir / "global.blackboard.usda").generic_string();
            BlackboardSchema gback;
            Check(WriteBlackboardSchema(g, gpath, error) && ReadBlackboardSchema(gpath, gback, error) && gback.keys.size() == 1 &&
                          gback.keys[0].name == "world.alarm" && std::get<int>(gback.keys[0].defaultValue) == 2 && gback.keys[0].wakeOnChange,
                  "global blackboard schema round trip");

            SaveNode save;
            save.name = "BehaviorSave";
            save.Set("worldTime", 812.5f);
            save.Set("possessed", uint64_t{3});
            SaveNode& agent = save.Child("Agents").Child("E_17");
            agent.Set("bb:cover.point", glm::vec3(4.0f, 0.0f, -2.0f));
            agent.Set("leaves", std::vector<std::string>{"Stand", "Free"});
            agent.Set("status", std::string("Running"));
            const std::string spath = (dir / "test.behaviorsave.usda").generic_string();
            SaveNode sback;
            Check(WriteSaveTree(save, spath, error) && ReadSaveTree(spath, sback, error), "write / read a checkpoint tree");
            const SaveNode* e17 = sback.FindChild("Agents") ? sback.FindChild("Agents")->FindChild("E_17") : nullptr;
            glm::vec3 p(0.0f);
            std::vector<std::string> leaves;
            Check(e17 != nullptr && e17->Get("bb:cover.point", p) && p == glm::vec3(4.0f, 0.0f, -2.0f) && e17->Get("leaves", leaves) && leaves.size() == 2 &&
                          sback.GetOr<uint64_t>("possessed", 0) == 3 && Near(sback.GetOr<float>("worldTime", 0.0f), 812.5f),
                  "checkpoint values (vec3, string array, uint64, float) survive");

            // brain runtime round trip
            auto tree = CompileTree(std::make_shared<TreeDesc>(back), "", sys.Types(), sys.GlobalSchema(), errors);
            if (!tree)
            {
                Check(false, "round-trip tree compiles");
                return;
            }
            Harness h(sys, tree);
            h.Set("cover.point", glm::vec3(8.0f, 0.0f, 0.0f));
            h.TickAt(0.0f);
            SaveNode runtime;
            h.brain->SaveRuntime(runtime);
            Harness h2(sys, tree);
            std::string loadError;
            const bool loaded = h2.brain->LoadRuntime(runtime, loadError);
            Check(loaded && h2.Status(move) == NodeStatus::Running && h2.brain->Instance().memory[static_cast<size_t>(tree->IndexOf(move))].started,
                  "brain runtime (node status, built-in memory) round trip");
            Check(h2.bb.IsSet(static_cast<uint32_t>(h2.bb.Find("cover.point"))) && std::get<glm::vec3>(h2.Get("cover.point")) == glm::vec3(8.0f, 0.0f, 0.0f),
                  "the tree's instance keys come back");
            SaveNode wrongHash = runtime;
            wrongHash.Set("contentHash", uint64_t{1});
            Check(!h2.brain->LoadRuntime(wrongHash, loadError), "a save of a changed tree (content hash) is refused");
        }
    } // namespace

    int RunBehaviorSelfTests()
    {
        gChecks = 0;
        gFails = 0;
        fmt::print("\n== Behaviour self tests ==\n");
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ranim_behavior_selftest";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        BehaviorSystem sys;
        std::string error;
        sys.Init(dir.generic_string(), error);

        TestComposites(sys);
        TestDecorators(sys);
        TestBlackboard(sys);
        TestValidator(sys);
        TestIntent(sys);
        TestRequests(sys);
        TestScheduling(sys);
        TestSteering();
        TestFiles(sys, dir);

        fmt::print("\nBehaviour result: {}/{} passed\n", gChecks - gFails, gChecks);
        return gFails;
    }
} // namespace RAnimation
