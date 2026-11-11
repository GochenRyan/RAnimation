#include <Behavior/BehaviorTestAssets.h>

#include <vector>

#include <fmt/format.h>

#include <Agent/PerceptionKeys.h>
#include <Behavior/BehaviorSystem.h>

namespace RAnimation
{
    namespace
    {
        struct TreeBuilder
        {
            TreeDesc d;
            TreeBuilder(const char* name, float hz, const char* doc)
            {
                d.name = name;
                d.tickHz = hz;
                d.doc = doc;
            }
            uint32_t Add(const char* type, std::vector<uint32_t> children = {}, std::vector<std::pair<std::string, std::string>> params = {},
                         const char* label = "")
            {
                BtNodeDesc n;
                n.id = d.nextId++;
                n.type = type;
                n.label = label;
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
            uint32_t Compare(const char* key, const char* op, const char* value = "")
            {
                std::vector<std::pair<std::string, std::string>> p = {{"key", std::string("@") + key}, {"op", op}};
                if (*value != '\0')
                {
                    p.emplace_back("value", value);
                }
                return Add("BlackboardCompare", {}, p);
            }
            uint32_t Set(const char* key, const char* value) { return Add("SetBlackboard", {}, {{"key", std::string("@") + key}, {"value", value}}); }
        };

        TreeDesc Corridor()
        {
            TreeBuilder b("corridor", 10.0f, "Walk forward until touching another character; then (Cooldown) walk back home and stay.");
            b.Key("home", BbType::Vec3, BbScope::Instance, glm::vec3(0.0f));
            b.Key("goal", BbType::Vec3, BbScope::Instance, glm::vec3(0.0f));
            b.Key("ready", BbType::Bool, BbScope::Instance, false);
            b.Key("bumped", BbType::Bool, BbScope::Instance, false);
            const uint32_t setup = b.Add("MemSequence",
                                         {b.Compare("ready", "==", "false"),
                                          b.Add("ProjectPoint", {}, {{"facing", "true"}, {"distance", "0"}, {"out", "@home"}}),
                                          b.Add("ProjectPoint", {}, {{"facing", "true"}, {"distance", "8"}, {"out", "@goal"}}),
                                          b.Set("ready", "true")},
                                         {}, "setup");
            const uint32_t back = b.Add("ReactiveSequence",
                                        {b.Compare("bumped", "==", "true"),
                                         b.Add("MemSequence",
                                               {b.Add("MoveTo", {}, {{"target", "@home"}, {"gait", "Walk"}, {"arriveRadius", "0.3"}, {"failOnBlocked", "false"}}),
                                                b.Add("Wait", {}, {{"seconds", "1000"}})})},
                                        {}, "after the bump: back home");
            const uint32_t walk = b.Add("ReactiveSequence",
                                        {b.Compare(PerceptionKeys::kContactCount, "==", "0"),
                                         b.Add("MoveTo", {}, {{"target", "@goal"}, {"gait", "Walk"}, {"arriveRadius", "0.3"}, {"failOnBlocked", "false"}})},
                                        {}, "walk until touching");
            const uint32_t bump = b.Add("Cooldown", {b.Add("MemSequence", {b.Add("Log", {}, {{"text", "bump"}}), b.Set("bumped", "true")})}, {{"seconds", "5"}});
            b.d.rootId = b.Add("MemSelector", {setup, back, walk, bump}, {}, "corridor");
            return b.d;
        }

        TreeDesc Bouncer()
        {
            TreeBuilder b("bouncer", 10.0f, "Jog forward; a boundary wall turns it away along the wall normal.");
            b.Key("goal", BbType::Vec3, BbScope::Instance, glm::vec3(0.0f));
            b.Key("ready", BbType::Bool, BbScope::Instance, false);
            b.Key("turned", BbType::Bool, BbScope::Instance, false);
            const uint32_t setup = b.Add("MemSequence",
                                         {b.Compare("ready", "==", "false"),
                                          b.Add("ProjectPoint", {}, {{"facing", "true"}, {"distance", "30"}, {"out", "@goal"}}), b.Set("ready", "true")},
                                         {}, "setup");
            // the jog is guarded (a running MemSelector child is resumed, so the guard must sit in that child)
            const uint32_t go = b.Add("ReactiveSequence",
                                      {b.Compare(PerceptionKeys::kBoundaryTouching, "==", "false"),
                                       b.Add("MoveTo", {}, {{"target", "@goal"}, {"gait", "Jog"}, {"arriveRadius", "0.5"}, {"failOnBlocked", "false"}})},
                                      {}, "jog until a wall");
            const uint32_t wall = b.Add("MemSequence",
                                        {b.Add("Log", {}, {{"text", "wall"}}),
                                         b.Add("ProjectPoint", {}, {{"direction", std::string("@") + PerceptionKeys::kBoundaryNormal}, {"distance", "8"}, {"out", "@goal"}}),
                                         b.Set("turned", "true"), b.Add("FaceTo", {}, {{"target", "@goal"}, {"toleranceDeg", "20"}})},
                                        {}, "wall: turn away");
            b.d.rootId = b.Add("MemSelector", {setup, go, wall}, {}, "bouncer");
            return b.d;
        }

        TreeDesc Patrol()
        {
            TreeBuilder b("patrol", 10.0f, "Route 'patrol': FindMarker -> MoveTo -> next index, forever.");
            b.Key("wp", BbType::Int, BbScope::Instance, 0);
            b.Key("target", BbType::Vec3, BbScope::Instance, glm::vec3(0.0f));
            b.Key("patrol.laps", BbType::Float, BbScope::TreeShared, 0.0f);
            b.d.rootId = b.Add("MemSequence",
                               {b.Add("FindMarker", {}, {{"kind", "Waypoint"}, {"name", "patrol"}, {"index", "@wp"}, {"out", "@target"}}),
                                b.Add("MoveTo", {}, {{"target", "@target"}, {"gait", "Jog"}, {"arriveRadius", "0.4"}, {"failOnBlocked", "false"}}),
                                b.Add("AddBlackboard", {}, {{"key", "@wp"}, {"delta", "1"}}),
                                b.Add("AddBlackboard", {}, {{"key", "@patrol.laps"}, {"delta", "0.1666667"}})},
                               {}, "patrol");
            return b.d;
        }

        TreeDesc Watcher()
        {
            TreeBuilder b("watcher", 2.0f, "Sees the player -> raises the global alarm and faces the last seen position.");
            b.Key("world.alarm", BbType::Bool, BbScope::Global, false);
            b.Key(PerceptionKeys::kPlayerVisible, BbType::Bool, BbScope::Instance, false, true); // explicit: wake on sight
            const uint32_t seen = b.Add("ReactiveSequence",
                                        {b.Compare(PerceptionKeys::kPlayerVisible, "==", "true"),
                                         b.Add("MemSequence",
                                               {b.Add("Log", {}, {{"text", "I see you"}}), b.Set("world.alarm", "true"),
                                                b.Add("FaceTo", {}, {{"target", std::string("@") + PerceptionKeys::kPlayerLastSeenPos}, {"toleranceDeg", "10"}}),
                                                b.Add("Wait", {}, {{"seconds", "0.5"}})})},
                                        {}, "seen");
            // idle first and guarded: when the player shows up, the running idle branch fails and the selector
            // moves on to "seen" in the same tick (a remembered child is resumed, not re-chosen)
            const uint32_t idle = b.Add("ReactiveSequence", {b.Compare(PerceptionKeys::kPlayerVisible, "==", "false"), b.Add("Wait", {}, {{"seconds", "1"}})},
                                        {}, "idle while unseen");
            b.d.rootId = b.Add("MemSelector", {idle, seen}, {}, "watcher");
            return b.d;
        }

        TreeDesc Lookout()
        {
            TreeBuilder b("lookout", 2.0f, "Stands still (the FOV array only perceives).");
            b.d.rootId = b.Add("Wait", {}, {{"seconds", "5"}});
            return b.d;
        }

        TreeDesc Greeter()
        {
            TreeBuilder b("greeter", 5.0f, "An interaction request (U) -> face the requester and wave.");
            const uint32_t greet = b.Add("ReactiveSequence",
                                         {b.Compare(PerceptionKeys::kInteractionFrom, "IsSet"),
                                          b.Add("MemSequence",
                                                {b.Add("Log", {}, {{"text", "greeting"}}),
                                                 b.Add("FaceTo", {}, {{"target", std::string("@") + PerceptionKeys::kInteractionFrom}, {"toleranceDeg", "15"}}),
                                                 b.Add("PlayAction", {}, {{"montage", "wave"}, {"waitForCompletion", "true"}, {"cancelOnHalt", "true"}}),
                                                 b.Add("ClearBlackboard", {}, {{"key", std::string("@") + PerceptionKeys::kInteractionFrom}})})},
                                         {}, "greet");
            b.d.rootId = b.Add("MemSelector", {greet, b.Add("Wait", {}, {{"seconds", "2"}})}, {}, "greeter");
            return b.d;
        }

        TreeDesc Talker()
        {
            TreeBuilder b("talker", 10.0f, "Talks (PlayAction waits for it); a talk that ends Cancelled or Rejected leads to the other branch.");
            b.Key("talked", BbType::Bool, BbScope::Instance, false);
            b.Key("otherBranch", BbType::Bool, BbScope::Instance, false);
            const uint32_t talk = b.Add("ReactiveSequence",
                                        {b.Compare("talked", "==", "false"),
                                         b.Add("MemSequence",
                                               {b.Add("PlayAction", {}, {{"montage", "talk"}, {"waitForCompletion", "true"}, {"cancelOnHalt", "true"}}),
                                                b.Set("talked", "true")})},
                                        {}, "talk");
            const uint32_t other = b.Add("MemSequence", {b.Add("Log", {}, {{"text", "other branch"}}), b.Set("otherBranch", "true"), b.Add("Wait", {}, {{"seconds", "1000"}})},
                                         {}, "other branch");
            b.d.rootId = b.Add("MemSelector", {talk, other}, {}, "talker");
            return b.d;
        }
    } // namespace

    bool WriteBehaviorTestAssets(const BehaviorSystem& system, std::string& outError)
    {
        BlackboardSchema global;
        BlackboardKeyDecl alarm;
        alarm.name = "world.alarm";
        alarm.type = BbType::Bool;
        alarm.scope = BbScope::Global;
        alarm.defaultValue = false;
        alarm.wakeOnChange = true;
        global.keys.push_back(alarm);
        if (!WriteBlackboardSchema(global, system.AbsolutePath(BehaviorSystem::kGlobalSchemaRelPath), outError))
        {
            return false;
        }
        const std::pair<const char*, TreeDesc> trees[] = {{"corridor", Corridor()}, {"bouncer", Bouncer()}, {"patrol", Patrol()},
                                                          {"watcher", Watcher()},   {"lookout", Lookout()}, {"greeter", Greeter()},
                                                          {"talker", Talker()}};
        for (const auto& [name, tree] : trees)
        {
            TreeDesc desc = tree;
            AutoLayoutTree(desc);
            std::vector<ValidationError> errors;
            if (!CompileTree(std::make_shared<TreeDesc>(desc), name, system.Types(), global, errors))
            {
                outError = fmt::format("{}: {}", name, errors.empty() ? "?" : errors.front().message);
                return false;
            }
            const std::string path = system.AbsolutePath(fmt::format("behavior/{}.behaviortree.usda", name));
            if (!WriteTreeDesc(desc, system.Types(), path, outError))
            {
                return false;
            }
            fmt::print("wrote {}\n", path);
        }
        return true;
    }
} // namespace RAnimation
