#include <StateMachine/StateMachineSelfTest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <AnimGraph/AnimGraphAsset.h>
#include <AnimGraph/AnimGraphUsdIo.h>
#include <AnimGraph/BoneMask.h>
#include <AnimGraph/CrossfadeTracker.h>
#include <AnimGraph/MontagePlayback.h>
#include <AnimGraph/PolarGrid.h>
#include <AnimGraph/GraphDesc.h>
#include <Controls/CharacterController.h>
#include <Model/AnimClip.h>
#include <Model/Model.h>
#include <StateMachine/SMManager.h>
#include <StateMachine/StateMachineDefinition.h>
#include <StateMachine/StateMachineUsdIo.h>
#include <StateMachine/StateTypeRegistry.h>

namespace RAnimation
{
    namespace
    {
        int gChecks = 0;
        int gFails = 0;

        void Check(bool condition, const std::string& what)
        {
            ++gChecks;
            fmt::print("  [{}] {}\n", condition ? " ok " : "FAIL", what);
            if (!condition)
            {
                ++gFails;
            }
        }

        std::string Join(const std::vector<std::string>& v)
        {
            std::string s;
            for (const std::string& x : v)
            {
                s += (s.empty() ? "" : " ") + x;
            }
            return s;
        }

        // User data of the test machines: the callback trace plus the flags the guards read.
        struct TestBoard
        {
            std::vector<std::string> trace;
            bool flagA = false;
            bool flagB = false;
            bool done = false;
            int eventsSeen = 0;
        };

        // Records every callback. `consume` makes OnEvent swallow events; `doneWhen` ties IsDone to a flag.
        class TraceState : public SMState
        {
        public:
            TraceState(std::string name, bool consume = false, bool doneWhenFlag = false) :
                SMState(std::move(name)), mConsume(consume), mDoneWhenFlag(doneWhenFlag)
            {
            }
            void OnEnter(StateContext& ctx) override { ctx.UserData<TestBoard>()->trace.push_back("+" + GetName()); }
            void OnExit(StateContext& ctx) override { ctx.UserData<TestBoard>()->trace.push_back("-" + GetName()); }
            void OnUpdate(StateContext& ctx, float) override
            {
                ctx.UserData<TestBoard>()->trace.push_back("u" + GetName());
            }
            bool OnEvent(StateContext& ctx, const Event&) override
            {
                TestBoard* b = ctx.UserData<TestBoard>();
                b->trace.push_back("e" + GetName());
                ++b->eventsSeen;
                return mConsume;
            }
            bool IsDone(StateContext& ctx) const override { return mDoneWhenFlag && ctx.UserData<TestBoard>()->done; }

        private:
            bool mConsume;
            bool mDoneWhenFlag;
        };

        struct CounterData final : public IStateInstanceData
        {
            int count = 0;
            void Serialize(ParamBag& out) const override { out.Set("count", count); }
            void Deserialize(const ParamBag& in) override { count = in.Get<int>("count", 0); }
        };

        class CountingState : public SMState
        {
        public:
            using SMState::SMState;
            std::unique_ptr<IStateInstanceData> CreateRuntimeData() const override
            {
                return std::make_unique<CounterData>();
            }
            void OnUpdate(StateContext& ctx, float) override { ++ctx.GetStateData<CounterData>()->count; }
        };

        GuardFn FlagGuard(bool TestBoard::* flag)
        {
            return [flag](StateContext& ctx, const Event*) { return ctx.UserData<TestBoard>()->*flag; };
        }

        // Root(Single){ Grounded(Single){ Idle, Walk }, InAir(Single){ Jump } }
        struct Hfsm
        {
            std::shared_ptr<StateMachineDefinition> def;
            StateId root, grounded, idle, walk, inAir, jump;
            TransitionId idleToWalk, walkToJump, jumpToIdle, groundedToJump;
            EventId evJump;
        };

        Hfsm BuildHfsm()
        {
            Hfsm h;
            h.def = std::make_shared<StateMachineDefinition>();
            h.def->Name = "hfsm";
            h.root = h.def->AddState(std::make_unique<SingleState>("Root"));
            h.grounded = h.def->AddState(std::make_unique<SingleState>("Grounded"), h.root);
            h.idle = h.def->AddState(std::make_unique<TraceState>("Idle"), h.grounded);
            h.walk = h.def->AddState(std::make_unique<TraceState>("Walk"), h.grounded);
            h.inAir = h.def->AddState(std::make_unique<SingleState>("InAir"), h.root);
            h.jump = h.def->AddState(std::make_unique<TraceState>("Jump", false, /*doneWhenFlag*/ true), h.inAir);
            h.def->SetRoot(h.root);
            static_cast<SingleState*>(h.def->GetState(h.root))->SetInitialChild(h.grounded);
            static_cast<SingleState*>(h.def->GetState(h.grounded))->SetInitialChild(h.idle);
            static_cast<SingleState*>(h.def->GetState(h.inAir))->SetInitialChild(h.jump);
            h.evJump = h.def->RegisterEvent("Jump");

            Transition t;
            t.source = h.idle;
            t.target = h.walk;
            t.guard = FlagGuard(&TestBoard::flagA);
            h.idleToWalk = h.def->AddTransition(t);

            t = Transition{};
            t.source = h.walk;
            t.target = h.jump;
            t.guard = FlagGuard(&TestBoard::flagB);
            t.action = [](StateContext& ctx) { ctx.UserData<TestBoard>()->trace.push_back("action"); };
            h.walkToJump = h.def->AddTransition(t);

            t = Transition{};
            t.source = h.jump;
            t.target = h.idle; // completion transition (no guard, no trigger)
            h.jumpToIdle = h.def->AddTransition(t);

            t = Transition{};
            t.source = h.grounded; // shared event transition on the parent
            t.target = h.jump;
            t.trigger = h.evJump;
            t.priority = 10;
            h.groundedToJump = h.def->AddTransition(t);
            return h;
        }

        void TestHierarchy()
        {
            fmt::print("== 1) hierarchy: enter / exit order, guards, LCA, completion ==\n");
            Hfsm h = BuildHfsm();
            std::vector<std::string> errors;
            Check(h.def->Finalize(&errors), "HFSM definition finalizes");

            SMManager m(h.def);
            m.SetUserData(TestBoard{});
            TestBoard* b = m.UserData<TestBoard>();
            Check(m.Start(), "Start enters the root path");
            Check(Join(b->trace) == "+Idle", "Start: parent composites first, leaf callbacks only (Idle entered)");
            Check(m.IsActive(h.root) && m.IsActive(h.grounded) && m.IsActive(h.idle) && !m.IsActive(h.inAir),
                  "active set = Root/Grounded/Idle");

            b->trace.clear();
            m.Update(0.016f);
            Check(Join(b->trace) == "uIdle", "Update without a passing guard only updates");

            b->flagA = true;
            b->trace.clear();
            m.Update(0.016f);
            Check(Join(b->trace) == "uIdle -Idle +Walk", "guard transition Idle -> Walk: update, exit, enter");
            Check(m.CurrentChild(h.grounded) == h.walk, "Grounded's current child is Walk");

            b->flagB = true;
            b->trace.clear();
            m.Update(0.016f);
            Check(Join(b->trace) == "uWalk -Walk action +Jump",
                  "cross-level Walk -> Jump: exit up to the LCA, action, enter down");
            Check(!m.IsActive(h.grounded) && m.IsActive(h.inAir) && m.IsActive(h.jump),
                  "Grounded exited, InAir/Jump entered");

            b->trace.clear();
            m.Update(0.016f);
            Check(Join(b->trace) == "uJump", "completion transition waits for IsDone");
            b->done = true;
            b->flagA = false;
            b->trace.clear();
            m.Update(0.016f);
            Check(Join(b->trace) == "uJump -Jump +Idle", "completion transition Jump -> Idle fires when IsDone");
            Check(m.TransitionsLastUpdate() == 1, "one transition executed in that Update");

            m.Stop();
            Check(!m.IsRunning() && m.ActiveLeaves().empty() && b->trace.back() == "-Idle", "Stop exits everything");
        }

        void TestEventsAndPriority()
        {
            fmt::print("== 2) events: bubbling, consumption, parent transition, priority ==\n");
            Hfsm h = BuildHfsm();
            h.def->Finalize();
            SMManager m(h.def);
            m.SetUserData(TestBoard{});
            TestBoard* b = m.UserData<TestBoard>();
            m.Start();

            int listenerCalls = 0;
            const EventDispatcher::Handle handle = m.Dispatcher().Subscribe(h.evJump,
                                                                            [&](const Event&) { ++listenerCalls; });
            m.Dispatcher().Subscribe(h.evJump, [&](const Event&) { ++listenerCalls; });

            b->trace.clear();
            m.DispatchEvent("Jump");
            Check(listenerCalls == 2, "dispatcher is multicast (two listeners called)");
            Check(Join(b->trace) == "eIdle",
                  "event bubbles from the leaf (Idle saw it, Grounded's transition matched)");
            Check(m.IsActive(h.idle), "event transitions execute in the next Update, not immediately");

            // Both Idle -> Walk (guard, priority 0) and Grounded -> Jump (event, priority 10) are pending.
            b->flagA = true;
            b->trace.clear();
            m.Update(0.016f);
            Check(m.IsActive(h.jump) && !m.IsActive(h.walk),
                  "priority 10 event transition beats the inner guard transition");
            Check(m.TransitionsLastUpdate() == 1,
                  "the losing request was dropped (one transition per region per Update)");

            m.Dispatcher().Unsubscribe(handle);
            m.DispatchEvent(h.evJump);
            Check(listenerCalls == 3, "Unsubscribe removes exactly one listener");
            Check(m.DispatchEvent("NoSuchEvent") == false, "unknown event names are rejected");
        }

        void TestParallel()
        {
            fmt::print("== 3) parallel regions: independent switching, event consumption per region ==\n");
            auto def = std::make_shared<StateMachineDefinition>();
            def->Name = "parallel";
            const StateId root = def->AddState(std::make_unique<ParallelState>("Root"));
            const StateId loco = def->AddState(std::make_unique<SingleState>("Loco"), root);
            const StateId idle = def->AddState(std::make_unique<TraceState>("Idle"), loco);
            const StateId walk = def->AddState(std::make_unique<TraceState>("Walk", false, true), loco);
            const StateId action = def->AddState(std::make_unique<SingleState>("Action"), root);
            const StateId none = def->AddState(std::make_unique<TraceState>("None", /*consume*/ true), action);
            const StateId punch = def->AddState(std::make_unique<TraceState>("Punch", false, true), action);
            def->SetRoot(root);
            static_cast<SingleState*>(def->GetState(loco))->SetInitialChild(idle);
            static_cast<SingleState*>(def->GetState(action))->SetInitialChild(none);
            const EventId evPunch = def->RegisterEvent("Punch");

            Transition t;
            t.source = idle;
            t.target = walk;
            t.guard = FlagGuard(&TestBoard::flagA);
            def->AddTransition(t);
            t = Transition{};
            t.source = none;
            t.target = punch;
            t.trigger = evPunch;
            def->AddTransition(t);
            t = Transition{};
            t.source = idle;
            t.target = punch; // crosses two regions of Root
            const TransitionId crossing = def->AddTransition(t);

            std::vector<std::string> errors = def->Validate();
            Check(!errors.empty() && errors.front().find("crosses two regions") != std::string::npos,
                  "Validate rejects a transition between two regions of a ParallelState");

            // Rebuild without the crossing transition.
            def = std::make_shared<StateMachineDefinition>();
            def->Name = "parallel";
            const StateId root2 = def->AddState(std::make_unique<ParallelState>("Root"));
            const StateId loco2 = def->AddState(std::make_unique<SingleState>("Loco"), root2);
            const StateId idle2 = def->AddState(std::make_unique<TraceState>("Idle"), loco2);
            const StateId walk2 = def->AddState(std::make_unique<TraceState>("Walk", false, true), loco2);
            const StateId action2 = def->AddState(std::make_unique<SingleState>("Action"), root2);
            const StateId none2 = def->AddState(std::make_unique<TraceState>("None", true), action2);
            const StateId punch2 = def->AddState(std::make_unique<TraceState>("Punch", false, true), action2);
            def->SetRoot(root2);
            static_cast<SingleState*>(def->GetState(loco2))->SetInitialChild(idle2);
            static_cast<SingleState*>(def->GetState(action2))->SetInitialChild(none2);
            const EventId evPunch2 = def->RegisterEvent("Punch");
            const EventId evOther = def->RegisterEvent("Other");
            t = Transition{};
            t.source = idle2;
            t.target = walk2;
            t.guard = FlagGuard(&TestBoard::flagA);
            def->AddTransition(t);
            t = Transition{};
            t.source = none2;
            t.target = punch2;
            t.trigger = evPunch2;
            def->AddTransition(t);
            t = Transition{};
            t.source = punch2;
            t.target = none2; // completion
            def->AddTransition(t);
            t = Transition{};
            t.source = walk2;
            t.target = idle2; // completion
            def->AddTransition(t);
            (void) crossing;
            Check(def->Finalize(&errors), "parallel definition finalizes");
            static_cast<ParallelState*>(def->GetState(root2))->SetCompletionPolicy(CompletionPolicy::All);

            SMManager m(def);
            m.SetUserData(TestBoard{});
            TestBoard* b = m.UserData<TestBoard>();
            m.Start();
            Check(Join(b->trace) == "+Idle +None", "Start enters every region in child order");
            Check(m.ActiveLeaves().size() == 2, "two active leaves");

            b->trace.clear();
            m.DispatchEvent(evOther);
            Check(Join(b->trace) == "eIdle eNone" && b->eventsSeen == 2,
                  "each region bubbles independently (both leaves saw the event)");

            b->trace.clear();
            m.DispatchEvent(evPunch2);
            Check(Join(b->trace) == "eIdle eNone",
                  "None consumed the event: its own Punch transition is not evaluated");
            m.Update(0.016f);
            Check(m.IsActive(none2), "a consumed event triggers no transition");

            // Make Action's leaf non-consuming by restarting with flagA and a direct request instead.
            b->flagA = true;
            b->trace.clear();
            Check(m.RequestTransitionTo(punch2), "direct transition request accepted");
            m.Update(0.016f);
            Check(m.IsActive(walk2) && m.IsActive(punch2), "both regions switched in the same Update (guard + direct)");
            Check(m.TransitionsLastUpdate() == 2, "two transitions executed");

            StateContext ctx(m, root2, 0.0f);
            Check(!def->GetState(root2)->IsDone(ctx), "ParallelState(All) is not done while a region is not done");
            b->done = true;
            Check(def->GetState(root2)->IsDone(ctx), "ParallelState(All) is done when every region is done");
            static_cast<ParallelState*>(def->GetState(root2))->SetCompletionPolicy(CompletionPolicy::Any);
            b->done = false;
            Check(!def->GetState(root2)->IsDone(ctx), "ParallelState(Any) is not done when no region is done");
        }

        void TestStaleAndRestart()
        {
            fmt::print("== 4) conflicts: stale requests, child beats parent on equal priority, restart ==\n");
            Hfsm h = BuildHfsm();
            // Add a parent-level guard transition with the same priority as Idle -> Walk.
            Transition t;
            t.source = h.grounded;
            t.target = h.jump;
            t.guard = FlagGuard(&TestBoard::flagA);
            const TransitionId groundedGuard = h.def->AddTransition(t);
            h.def->Finalize();

            SMManager m(h.def);
            m.SetUserData(TestBoard{});
            TestBoard* b = m.UserData<TestBoard>();
            m.Start();
            b->flagA = true;
            m.Update(0.016f);
            Check(m.IsActive(h.walk) && !m.IsActive(h.jump),
                  "equal priority: the inner (Idle -> Walk) transition wins over Grounded -> Jump");

            // Grounded -> Jump stays enabled; next Update it fires (Walk is a child of Grounded).
            b->trace.clear();
            m.Update(0.016f);
            Check(m.IsActive(h.jump) && Join(b->trace) == "uWalk -Walk +Jump",
                  "parent-level guard transition fires once no child transition competes");

            // A request whose source is no longer active is dropped.
            m.RequestTransition(groundedGuard);
            b->trace.clear();
            m.Update(0.016f);
            Check(m.TransitionsLastUpdate() == 0 && m.IsActive(h.jump), "request from an inactive source is dropped");

            // Direct request to an active state is a no-op; to the Walk leaf it swaps the branch.
            Check(m.RequestTransitionTo(h.jump), "direct request queued");
            m.Update(0.016f);
            Check(m.TransitionsLastUpdate() == 0, "direct request to an active state does nothing");
            b->flagA = false;
            b->trace.clear();
            m.RequestTransitionTo(h.walk);
            m.Update(0.016f);
            Check(Join(b->trace) == "uJump -Jump +Walk" && m.IsActive(h.grounded),
                  "direct jump into another branch enters the path (Grounded, Walk)");

            // Self transition (target == source): exit and re-enter.
            Transition self;
            Hfsm h2 = BuildHfsm();
            self.source = h2.walk;
            self.target = h2.walk;
            self.guard = FlagGuard(&TestBoard::flagB);
            self.priority = 5; // beats Walk -> Jump, which reads the same flag
            h2.def->AddTransition(self);
            h2.def->Finalize();
            SMManager m2(h2.def);
            m2.SetUserData(TestBoard{});
            TestBoard* b2 = m2.UserData<TestBoard>();
            m2.Start();
            b2->flagA = true;
            m2.Update(0.016f);
            b2->flagA = false;
            b2->flagB = true;
            b2->trace.clear();
            m2.Update(0.016f);
            Check(Join(b2->trace) == "uWalk -Walk +Walk", "a self transition exits and re-enters the state");
        }

        void TestValidation()
        {
            fmt::print("== 5) validation ==\n");
            StateMachineDefinition def;
            Check(!def.Validate().empty(), "empty definition is invalid (no root)");

            const StateId root = def.AddState(std::make_unique<SingleState>("Root"));
            def.SetRoot(root);
            Check(!def.Validate().empty(), "SingleState without children / initial child is invalid");

            const StateId a = def.AddState(std::make_unique<SMState>("A"), root);
            static_cast<SingleState*>(def.GetState(root))->SetInitialChild(a);
            Check(def.Validate().empty(), "Root{A} with an initial child is valid");
            Check(def.AddState(std::make_unique<SMState>("A"), root) == kInvalidStateId,
                  "duplicate state names are rejected");
            Check(def.AddState(std::make_unique<SMState>("B"), a) == kInvalidStateId, "a leaf cannot have children");
            def.AddState(std::make_unique<SMState>("bad name"), root);
            const std::vector<std::string> errors = def.Validate();
            Check(!errors.empty() && errors.front().find("identifier") != std::string::npos,
                  "non-identifier names fail validation");
        }

        void TestUsdRoundTrip()
        {
            fmt::print("== 6) USD round trip: definition + runtime snapshot ==\n");
            StateTypeRegistry registry;
            registry.RegisterStateType("Trace",
                                       [](const std::string& name, const ParamBag&)
                                       { return std::make_unique<TraceState>(name); });
            registry.RegisterStateType("Counting",
                                       [](const std::string& name, const ParamBag&)
                                       { return std::make_unique<CountingState>(name); });
            registry.RegisterGuard("FlagA", FlagGuard(&TestBoard::flagA));
            registry.RegisterAction("Mark",
                                    [](StateContext& ctx) { ctx.UserData<TestBoard>()->trace.push_back("action"); });

            auto def = std::make_shared<StateMachineDefinition>();
            def->Name = "roundtrip";
            ParamBag params;
            params.Set("speed", 1.5f);
            params.Set("clip", 7);
            params.Set("label", std::string("hello"));
            params.Set("flag", true);
            const StateId root = def->AddState(registry.CreateState("Parallel", "Root", ParamBag{}));
            const StateId loco = def->AddState(registry.CreateState("Single", "Loco", ParamBag{}), root);
            const StateId idle = def->AddState(registry.CreateState("Trace", "Idle", params), loco);
            const StateId walk = def->AddState(registry.CreateState("Counting", "Walk", ParamBag{}), loco);
            const StateId other = def->AddState(registry.CreateState("Single", "Other", ParamBag{}), root);
            const StateId x = def->AddState(registry.CreateState("Trace", "X", ParamBag{}), other);
            def->SetRoot(root);
            static_cast<SingleState*>(def->GetState(loco))->SetInitialChild(idle);
            static_cast<SingleState*>(def->GetState(other))->SetInitialChild(x);
            static_cast<ParallelState*>(def->GetState(root))->SetCompletionPolicy(CompletionPolicy::Any);
            const EventId ev = def->RegisterEvent("Go");
            Transition t;
            t.name = "go";
            t.source = idle;
            t.target = walk;
            t.trigger = ev;
            t.priority = 3;
            t.guardName = "FlagA";
            t.guard = registry.CreateGuard("FlagA");
            t.actionName = "Mark";
            t.action = registry.CreateAction("Mark");
            def->AddTransition(t);
            Check(def->Finalize(), "round-trip definition finalizes");

            const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ranim_statemachine_selftest";
            std::filesystem::create_directories(dir);
            const std::string defPath = (dir / "roundtrip.statemachine.usda").string();
            const std::string runPath = (dir / "roundtrip.smstate.usda").string();
            std::string error;
            Check(StateMachineSerializer::SerializeDefinition(*def, "doc text", defPath, error),
                  "SerializeDefinition: " + error);

            std::string doc;
            std::shared_ptr<StateMachineDefinition> loaded =
                    StateMachineSerializer::DeserializeDefinition(defPath, registry, doc, error);
            Check(loaded != nullptr, "DeserializeDefinition: " + error);
            if (loaded)
            {
                Check(doc == "doc text" && loaded->Name == "roundtrip", "doc and name survive");
                Check(loaded->StateCount() == def->StateCount() && loaded->TransitionCount() == 1 &&
                              loaded->EventCount() == 1,
                      "state / transition / event counts match");
                const StateId lIdle = loaded->FindState("Idle");
                const StateId lLoco = loaded->FindState("Loco");
                Check(lIdle != kInvalidStateId && loaded->GetState(lIdle)->GetParent() == lLoco,
                      "hierarchy restored (Idle under Loco)");
                Check(loaded->GetState(lIdle)->GetParams() == params,
                      "typed parameters restored (float/int/string/bool)");
                Check(loaded->GetState(lIdle)->GetTypeName() == "Trace", "state type name restored");
                Check(static_cast<SingleState*>(loaded->GetState(lLoco))->GetInitialChild() == lIdle,
                      "initial child restored");
                Check(static_cast<ParallelState*>(loaded->GetState(loaded->GetRoot()))->GetCompletionPolicy() ==
                              CompletionPolicy::Any,
                      "completion policy restored");
                Check(loaded->GetState(loaded->GetRoot())->GetChildren().size() == 2 &&
                              loaded->GetState(loaded->GetState(loaded->GetRoot())->GetChildren()[0])->GetName() ==
                                      "Loco",
                      "region order restored");
                const Transition* lt = loaded->GetTransition(0);
                Check(lt != nullptr && lt->name == "go" && lt->priority == 3 &&
                              lt->trigger == loaded->FindEvent("Go") && lt->guardName == "FlagA" && lt->guard &&
                              lt->actionName == "Mark" && lt->action && lt->source == lIdle &&
                              lt->target == loaded->FindState("Walk"),
                      "transition fields and callables restored through the registry");

                // The loaded definition behaves like the original.
                SMManager m(loaded);
                m.SetUserData(TestBoard{});
                TestBoard* b = m.UserData<TestBoard>();
                m.Start();
                b->flagA = true;
                m.DispatchEvent("Go");
                m.Update(0.016f);
                m.Update(0.016f);
                m.Update(0.016f);
                Check(m.IsActive(loaded->FindState("Walk")) &&
                              std::find(b->trace.begin(), b->trace.end(), "action") != b->trace.end(),
                      "loaded machine runs (event + guard + action)");
                Check(m.GetStateData<CounterData>(loaded->FindState("Walk"))->count == 2,
                      "per-state instance data accumulates");

                // Runtime snapshot: active leaves + instance data; restoring runs no callbacks.
                Check(StateMachineSerializer::SerializeRuntime(m, runPath, error), "SerializeRuntime: " + error);
                SMManager m2(loaded);
                m2.SetUserData(TestBoard{});
                Check(StateMachineSerializer::DeserializeRuntime(runPath, m2, error), "DeserializeRuntime: " + error);
                Check(m2.IsRunning() && m2.IsActive(loaded->FindState("Walk")) && m2.IsActive(loaded->FindState("X")) &&
                              m2.UserData<TestBoard>()->trace.empty(),
                      "snapshot restores the active configuration without callbacks");
                Check(m2.GetStateData<CounterData>(loaded->FindState("Walk"))->count == 2,
                      "snapshot restores instance data");
                m2.Update(0.016f);
                Check(m2.GetStateData<CounterData>(loaded->FindState("Walk"))->count == 3,
                      "restored machine keeps running");

                // Two managers on one definition evolve independently.
                Check(m.GetStateData<CounterData>(loaded->FindState("Walk"))->count == 2,
                      "the original manager is unaffected");

                SMManager wrong(def);
                Check(!StateMachineSerializer::DeserializeRuntime(defPath, wrong, error),
                      "a definition file is rejected as a runtime snapshot");
            }

            StateTypeRegistry empty;
            const bool rejected = StateMachineSerializer::DeserializeDefinition(defPath, empty, doc, error) == nullptr;
            Check(rejected && error.find("unregistered") != std::string::npos,
                  "unregistered state type is reported: " + error);
            std::filesystem::remove_all(dir);
        }

        void TestCharacterDefinition()
        {
            fmt::print("\n[character]\n");
            StateTypeRegistry registry;
            CharacterController::RegisterCharacterTypes(registry);
            std::string error;
            std::shared_ptr<StateMachineDefinition> def = CharacterController::BuildDefaultDefinition(registry, error);
            Check(def != nullptr, "built-in character definition finalizes: " + error);
            if (!def)
            {
                return;
            }

            // usda round trip
            const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ranim_selftest_character";
            std::filesystem::create_directories(dir);
            const std::string path = (dir / "character.statemachine.usda").generic_string();
            Check(StateMachineSerializer::SerializeDefinition(*def, "test", path, error), "character definition saves: " + error);
            std::string doc;
            std::shared_ptr<StateMachineDefinition> loaded = StateMachineSerializer::DeserializeDefinition(path, registry, doc, error);
            Check(loaded != nullptr && loaded->StateCount() == def->StateCount() &&
                          loaded->TransitionCount() == def->TransitionCount(),
                  "character definition round-trips: " + error);
            std::filesystem::remove_all(dir);

            // Drive the machine without a scene: the guards read the input / motion / feedback structs, the
            // states only write AnimParams. The profile is the only model data they need.
            CharacterProfile profile;
            profile.walkSpeed = 1.2f;
            profile.jogSpeed = 3.0f;
            ProfileMontage punch;
            punch.slot = CharacterController::kUpperBodySlot;
            punch.desc.name = "punch";
            punch.desc.clipName = "Punch";
            punch.desc.priority = 10;
            profile.montages["punch"] = punch;
            ProfileMontage hit;
            hit.slot = CharacterController::kFullBodySlot;
            hit.desc.name = "hit";
            hit.desc.clipName = "Hit_Chest";
            hit.desc.priority = 20;
            hit.desc.lockMovement = true;
            profile.montages["hit"] = hit;

            SMManager m(def);
            CharacterInput input;
            MotionState motion;
            GraphFeedback feedback;
            AnimParams anim;
            CharacterStateData data;
            data.input = &input;
            data.motion = &motion;
            data.graph = &feedback;
            data.profile = &profile;
            data.anim = &anim;
            CharacterController::BindStateData(*def, data);
            Check(data.airborneState == def->FindState("Airborne") && data.deadState == def->FindState("Dead"),
                  "BindStateData resolves the Airborne / Dead state ids");
            m.SetUserData(data);
            m.Start();
            Check(m.IsActive(def->FindState("Stand")) && m.IsActive(def->FindState("Free")) && m.IsActive(def->FindState("Unarmed")),
                  "starts in Stand || Free || Unarmed");
            Check(anim.locoSelect == "stand", "Stand selects the 'stand' input");

            input.moveY = 1.0f;
            m.Update(0.016f);
            Check(std::fabs(anim.moveSpeed - 1.2f) < 1e-5f, "walking input: Stand asks for the walk speed");
            input.run = true;
            m.Update(0.016f);
            Check(std::fabs(anim.moveSpeed - 3.0f) < 1e-5f, "run input: Stand asks for the jog speed (no state change)");

            // Jump is a motion fact: the controller sets airborne, the machine follows.
            motion.airborne = true;
            motion.grounded = false;
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("JumpStart")) && anim.locoSelect == "jump_start" && anim.locoRestart,
                  "airborne -> Airborne.Start, selector 'jump_start' with restart");
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("JumpStart")), "stays in Start while in the air");
            motion.airborne = false;
            motion.grounded = true;
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("JumpLand")) && anim.locoSelect == "jump_land", "grounded before the start clip ends -> Land");
            feedback.locoDone = true; // the graph reports the land clip ended
            m.Update(0.016f);
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("Stand")) && anim.locoSelect == "stand", "Land completes (locoDone) -> Stand");
            feedback.locoDone = false;

            // Action region: montage requests by name, slot from the profile, priorities decide.
            Event action;
            action.id = def->FindEvent("Action");
            action.payload = std::string("punch");
            m.DispatchEvent(action);
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("UpperBodyBusy")) && anim.slotRequests.size() == 1 &&
                          anim.slotRequests[0].slot == "UpperBody" && anim.slotRequests[0].montage == "punch" &&
                          !anim.movementLock,
                  "'punch' -> UpperBodyBusy with one slot request, movement free");
            anim.slotRequests.clear();
            feedback.upperBodyIdle = true; // idle on the frame of the request must not count as done
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("UpperBodyBusy")), "not done on the first update after entering");
            action.payload = std::string("hit");
            m.DispatchEvent(action);
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("FullBodyBusy")) && anim.currentMontage[1] == "hit" && anim.movementLock &&
                          anim.currentMontage[0].empty(),
                  "'hit' (priority 20, full body) takes over from punch and locks movement");
            action.payload = std::string("punch");
            m.DispatchEvent(action);
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("FullBodyBusy")), "a lower-priority upper body request does not interrupt the hit");
            feedback.fullBodyIdle = true;
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("Free")) && !anim.movementLock, "slot idle -> Free, movement unlocked");

            // Weapon region and death.
            m.DispatchEvent("ToggleWeapon");
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("Pistol")) && anim.upperBodyAlphaTarget == 1.0f, "ToggleWeapon -> Pistol, upper body layer on");
            input.crouch = true;
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("Crouch")) && anim.locoSelect == "crouch", "crouch input -> Crouch ring");
            m.DispatchEvent("Die");
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("Dead")) && anim.dead && anim.locoSelect == "death", "Die -> Dead (terminal)");
            action.payload = std::string("punch");
            m.DispatchEvent(action);
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("Free")), "actions are refused once dead");
            input.crouch = false;
            m.Update(0.016f);
            Check(m.IsActive(def->FindState("Dead")), "Dead has no way out");
        }
    } // namespace

    namespace
    {
        // Step 2 of the UAL integration: clips and graph nodes referenced by name.
        void TestNames()
        {
            fmt::print("\n[names]\n");
            std::vector<std::shared_ptr<AnimClip>> clips;
            for (const char* name : {"Armature|Idle_Loop", "Armature|Jump", "Jump_Start", "Armature|Jump"})
            {
                auto clip = std::make_shared<AnimClip>();
                clip->SetClipName(name);
                clips.push_back(clip);
            }
            Check(Model::FindClipIndexIn(clips, "Armature|Idle_Loop") == 0, "full clip name resolves");
            Check(Model::FindClipIndexIn(clips, "Idle_Loop") == 0, "short clip name resolves");
            Check(Model::FindClipIndexIn(clips, "Armature|Jump") == 1, "first full match wins over later duplicates");
            Check(Model::FindClipIndexIn(clips, "Jump_Start") == 2, "clip without '|' matches its own name");
            Check(Model::FindClipIndexIn(clips, "Jump") == 1, "short name does not match a prefix (Jump_Start)");
            Check(Model::FindClipIndexIn(clips, "Nope") == -1 && Model::FindClipIndexIn(clips, "") == -1,
                  "unknown / empty names give -1");

            GraphDesc desc;
            NodeDesc player;
            player.Type = "ClipPlayer";
            player.Name = "Walk";
            player.ClipName = "Walk_Fwd_Loop";
            player.ClipIndex = 7;
            const int p = desc.AddNode(player);
            NodeDesc root;
            root.Type = "Root";
            root.Name = "Out";
            root.Inputs.emplace_back("Result", p);
            desc.OutputNode = desc.AddNode(root);
            Check(desc.FindNodeByName("Walk") == p && desc.FindNodeByName("Out") == desc.OutputNode &&
                          desc.FindNodeByName("") == -1 && desc.FindNodeByName("x") == -1,
                  "GraphDesc::FindNodeByName");

            const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ranim_selftest_names";
            std::filesystem::create_directories(dir);
            const std::string path = (dir / "names.animgraph.usda").generic_string();
            std::string error;
            GraphLayout layout;
            layout.NodePos.assign(2, glm::vec2(0.0f));
            Check(SaveAnimGraphUsd(desc, layout, "names test", path, error), "animgraph with names saves: " + error);
            GraphDesc loaded;
            GraphLayout loadedLayout;
            std::string doc;
            Check(LoadAnimGraphUsd(path, loaded, loadedLayout, doc, error), "animgraph with names loads: " + error);
            Check(loaded.Nodes.size() == 2 && loaded.Nodes[static_cast<size_t>(p)].Name == "Walk" &&
                          loaded.Nodes[static_cast<size_t>(p)].ClipName == "Walk_Fwd_Loop" &&
                          loaded.Nodes[static_cast<size_t>(p)].ClipIndex == 7 &&
                          loaded.Nodes[static_cast<size_t>(desc.OutputNode)].Name == "Out",
                  "ranim:name / ranim:clipName round-trip (clipIndex kept as fallback)");
            std::filesystem::remove_all(dir);
        }

        // Step 3: bone masks for LayeredBlend + the usda attributes that carry them.
        void TestBoneMask()
        {
            fmt::print("\n[bone mask]\n");
            //   0 root -> 1 pelvis -> 2 spine1 -> 3 spine2 -> 4 head
            //                      -> 5 thigh  -> 6 calf
            const std::vector<int32_t> parents = {-1, 0, 1, 2, 3, 1, 5};
            const std::vector<float> hard = BuildBoneMask(parents, 2, 0);
            Check(hard == std::vector<float>({0, 0, 1, 1, 1, 0, 0}), "subtree mask: spine1 and below, legs untouched");
            const std::vector<float> soft = BuildBoneMask(parents, 2, 2);
            Check(std::fabs(soft[2] - 1.0f / 3.0f) < 1e-6f && std::fabs(soft[3] - 2.0f / 3.0f) < 1e-6f && soft[4] == 1.0f &&
                          soft[5] == 0.0f,
                  "blend depth 2 ramps 1/3, 2/3, 1 down the spine");
            Check(BuildBoneMask(parents, -1, 0) == std::vector<float>(7, 0.0f), "unknown root gives an all-zero mask");
            Check(BuildBoneMask(parents, 0, 0) == std::vector<float>(7, 1.0f), "root mask covers everything");

            GraphDesc desc;
            NodeDesc layered;
            layered.Type = "LayeredBlend";
            layered.MaskRoot = "Spine1";
            layered.BlendDepth = 2;
            layered.Param = 0.75f;
            const int id = desc.AddNode(layered);
            NodeDesc root;
            root.Type = "Root";
            root.Inputs.emplace_back("Result", id);
            desc.OutputNode = desc.AddNode(root);
            const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ranim_selftest_mask";
            std::filesystem::create_directories(dir);
            const std::string path = (dir / "mask.animgraph.usda").generic_string();
            std::string error;
            GraphLayout layout;
            layout.NodePos.assign(2, glm::vec2(0.0f));
            Check(SaveAnimGraphUsd(desc, layout, "mask test", path, error), "animgraph with a LayeredBlend saves: " + error);
            GraphDesc loaded;
            GraphLayout loadedLayout;
            std::string doc;
            Check(LoadAnimGraphUsd(path, loaded, loadedLayout, doc, error), "animgraph with a LayeredBlend loads: " + error);
            Check(loaded.Nodes.size() == 2 && loaded.Nodes[static_cast<size_t>(id)].MaskRoot == "Spine1" &&
                          loaded.Nodes[static_cast<size_t>(id)].BlendDepth == 2 &&
                          std::fabs(loaded.Nodes[static_cast<size_t>(id)].Param - 0.75f) < 1e-6f,
                  "ranim:maskRoot / ranim:blendDepth / ranim:param round-trip");
            std::filesystem::remove_all(dir);
        }

        // Step 4: the crossfade bookkeeping behind CrossfadeSelect.
        void TestCrossfade()
        {
            fmt::print("\n[crossfade]\n");
            CrossfadeTracker t;
            t.SetActive(0, true);
            Check(t.Step(0.016f, 0.2f) && t.WeightOf(0) == 1.0f && t.Layers().size() == 1, "seeded input is settled at weight 1");
            t.SetActive(1, false);
            bool monotonic = true;
            float prev1 = 0.0f;
            float sum = 0.0f;
            bool settled = false;
            int steps = 0;
            while (!settled && steps < 100)
            {
                settled = t.Step(0.05f, 0.2f);
                ++steps;
                monotonic = monotonic && t.WeightOf(1) >= prev1;
                prev1 = t.WeightOf(1);
                sum = t.TotalWeight();
                if (!settled)
                {
                    monotonic = monotonic && std::fabs(sum - 1.0f) < 1e-5f; // symmetric ramps sum to 1
                }
            }
            Check(settled && steps == 4 && monotonic, "0.2 s fade at 0.05 s steps settles in 4 steps, rising monotonically, sum 1 on the way");
            Check(t.Layers().size() == 1 && t.Layers().front().index == 1, "the faded-out input was dropped");

            // change of mind mid-fade: resumes from the current weight, never pops
            t.SetActive(0, false);
            t.Step(0.05f, 0.2f);
            t.Step(0.05f, 0.2f);
            const float w0 = t.WeightOf(0);
            t.SetActive(1, false);
            t.Step(0.05f, 0.2f);
            Check(std::fabs(w0 - 0.5f) < 1e-5f && std::fabs(t.WeightOf(0) - 0.25f) < 1e-5f && std::fabs(t.WeightOf(1) - 0.75f) < 1e-5f,
                  "switching back mid-fade continues from the current weights");

            // blend time 0 switches at once
            CrossfadeTracker fast;
            fast.SetActive(2, true);
            fast.SetActive(3, false);
            Check(fast.Step(0.016f, 0.0f) && fast.WeightOf(3) == 1.0f && fast.Layers().size() == 1, "blend time 0 is an instant switch");
        }

        // Step 6: the polar grid behind BlendSpace2D.
        void TestPolarGrid()
        {
            fmt::print("\n[polar grid]\n");
            //  row y=0: idle (x 0)      row y=1: 0 fwd, 90 right, 180 bwd, 270 left
            const std::vector<GridSample> g = {{0, 0}, {0, 1}, {90, 1}, {180, 1}, {270, 1}};
            GridWeight w[4];
            auto weightOf = [&](int count, int index)
            {
                for (int i = 0; i < count; ++i)
                {
                    if (w[i].index == index)
                    {
                        return w[i].weight;
                    }
                }
                return 0.0f;
            };
            int n = ResolvePolarGrid(g, 45.0f, 1.0f, true, 360.0f, w);
            Check(n == 2 && std::fabs(weightOf(n, 1) - 0.5f) < 1e-5f && std::fabs(weightOf(n, 2) - 0.5f) < 1e-5f,
                  "45 deg on the top row: fwd / right half each");
            n = ResolvePolarGrid(g, 359.0f, 1.0f, true, 360.0f, w);
            Check(n == 2 && std::fabs(weightOf(n, 4) - 1.0f / 90.0f) < 1e-4f && std::fabs(weightOf(n, 1) - 89.0f / 90.0f) < 1e-4f,
                  "359 deg wraps between left (270) and fwd (0)");
            n = ResolvePolarGrid(g, -90.0f, 1.0f, true, 360.0f, w);
            Check(n == 1 && weightOf(n, 4) == 1.0f, "-90 deg is exactly left");
            n = ResolvePolarGrid(g, 90.0f, 0.25f, true, 360.0f, w);
            Check(n == 2 && std::fabs(weightOf(n, 0) - 0.75f) < 1e-5f && std::fabs(weightOf(n, 2) - 0.25f) < 1e-5f,
                  "speed 0.25: idle row (single sample) 0.75 + right 0.25");
            n = ResolvePolarGrid(g, 45.0f, 0.5f, true, 360.0f, w);
            float sum = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                sum += w[i].weight;
            }
            Check(n == 3 && std::fabs(sum - 1.0f) < 1e-5f && std::fabs(weightOf(n, 0) - 0.5f) < 1e-5f,
                  "between rows: 3 terms (idle + two directions), weights sum to 1");
            n = ResolvePolarGrid(g, 45.0f, 5.0f, false, 360.0f, w);
            Check(n == 2 && std::fabs(weightOf(n, 1) - 0.5f) < 1e-5f, "speed above the top row clamps to it");
            n = ResolvePolarGrid(g, 300.0f, 1.0f, false, 360.0f, w);
            Check(n == 1 && weightOf(n, 4) == 1.0f, "no wrap: beyond the last column clamps to it");
        }

        // Step 7: the montage envelope and notify crossing behind AnimSlot.
        void TestMontage()
        {
            fmt::print("\n[montage]\n");
            MontageDesc d;
            d.name = "punch";
            d.durationSec = 1.0f;
            d.blendInSec = 0.2f;
            d.blendOutSec = 0.2f;
            d.notifies = {{0.0f, "start"}, {0.5f, "hit"}, {1.0f, "end"}};
            MontagePlayback p(d);
            std::vector<std::string> crossed;
            p.Advance(0.1f, &crossed);
            Check(std::fabs(p.Weight() - 0.5f) < 1e-5f && p.CurrentPhase() == MontagePlayback::Phase::In && crossed.empty(),
                  "0.1 s into a 0.2 s blend-in: weight 0.5, 'start' at t=0 is not crossed (prev < t)");
            p.Advance(0.1f, &crossed);
            Check(p.Weight() == 1.0f && p.CurrentPhase() == MontagePlayback::Phase::Hold, "blend-in done -> hold at 1");
            p.Advance(0.35f, &crossed); // t = 0.55
            Check(crossed.size() == 1 && crossed[0] == "hit", "the notify at 0.5 fires once when crossed");
            crossed.clear();
            p.Advance(0.3f, &crossed); // t = 0.85 >= 1.0 - 0.2 -> blend-out starts; 0.05 of it already elapsed
            Check(p.CurrentPhase() == MontagePlayback::Phase::Out && crossed.empty() && std::fabs(p.Weight() - 0.75f) < 1e-5f,
                  "natural blend-out starts blendOut before the end (weight 0.75 at t = 0.85)");
            p.Advance(0.05f, &crossed); // t = 0.9, weight 0.5
            Check(std::fabs(p.Weight() - 0.5f) < 1e-5f, "halfway through the blend-out at t = 0.9");
            p.Advance(0.2f, &crossed); // t clamps to 1.0, weight 0
            Check(p.Finished() && p.Time() == 1.0f && crossed.size() == 1 && crossed[0] == "end", "finished at the clip end, 'end' notify crossed");

            MontageDesc loopDesc = d;
            loopDesc.loop = true;
            loopDesc.blendInSec = 0.0f;
            MontagePlayback loop(loopDesc);
            crossed.clear();
            loop.Advance(0.9f, &crossed);
            loop.Advance(0.2f, &crossed); // wraps: t = 0.1
            Check(std::fabs(loop.Time() - 0.1f) < 1e-5f && loop.Weight() == 1.0f && crossed.size() == 3 &&
                          crossed[0] == "hit" && crossed[1] == "end" && crossed[2] == "start",
                  "looping montage wraps and fires end + start across the seam");
            loop.Stop(0.1f);
            loop.Advance(0.05f, nullptr);
            Check(loop.BlendingOut() && std::fabs(loop.Weight() - 0.5f) < 1e-5f, "Stop starts a blend-out from the current weight");
            loop.Advance(0.05f, nullptr);
            Check(loop.Finished(), "and finishes when it reaches 0");
        }
    } // namespace

    int RunStateMachineSelfTests()
    {
        gChecks = 0;
        gFails = 0;
        fmt::print("StateMachine self test\n");
        TestHierarchy();
        TestEventsAndPriority();
        TestParallel();
        TestStaleAndRestart();
        TestValidation();
        TestUsdRoundTrip();
        TestCharacterDefinition();
        TestNames();
        TestBoneMask();
        TestCrossfade();
        TestPolarGrid();
        TestMontage();
        fmt::print("\nResult: {}/{} passed\n", gChecks - gFails, gChecks);
        return gFails;
    }
} // namespace RAnimation
