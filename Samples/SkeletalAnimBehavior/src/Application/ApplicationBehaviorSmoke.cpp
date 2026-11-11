// --behavior: the scripted behaviour scene test (design doc §12.2) on behavior_test.level.usda.
//
// Entity ids of the level: 1 player (possessed, scripted route WP_player_0..8), 2 / 3 corridor pair,
// 4 bouncer, 5 patrol, 6 watcher (behind the screen), 7-10 FOV array (N, E, S, W), 11 greeter,
// 12 talker, 13 requester (request lifecycle, scripted).
// Timeline: frame 30 checkpoint save (talker mid-PlayAction), frame 45 load -> the talker's request reads
// Cancelled -> its tree takes the other branch. Everything else runs on from the loaded state.

#include <Application/Application.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <Agent/PerceptionKeys.h>
#include <Agent/ScriptBrain.h>
#include <Behavior/BehaviorTreeBrain.h>

using namespace RAnimation;

struct Application::BehaviorSmoke
{
    static constexpr EntityId kPlayer = 1, kCorridorA = 2, kCorridorB = 3, kBouncer = 4, kPatrol = 5, kWatcher = 6;
    static constexpr EntityId kLookoutN = 7, kLookoutE = 8, kLookoutS = 9, kLookoutW = 10, kGreeter = 11, kTalker = 12, kRequester = 13;
    static constexpr int kSaveFrame = 30;
    static constexpr int kLoadFrame = 45;
    static constexpr int kRouteLength = 9;

    std::string slot;

    // scripted player
    int routeIndex = 0;
    int holdFrames = 0;
    bool interactSent = false;
    bool fovChecked = false;
    std::string fovResult;
    bool fovOk = false;

    // observations
    int corridorBegin = 0;
    int corridorEnd = 0;
    float bouncerStartYaw = 0.0f;
    bool bouncerStartKnown = false;
    bool bouncerTouched = false;
    float bouncerMaxTurn = 0.0f;
    bool watcherWasVisible = false;
    int watcherSeenFrame = -1;
    bool watcherTickedThatFrame = false;
    bool watcherTickWasWake = false;
    std::vector<std::string> zones;
    int patrolWp = 0;
    bool greeterWaved = false;
    EntityId interactionTarget = 0;
    bool talkerRunningAtSave = false;
    bool savedOk = false;
    bool loadedOk = false;
    int talkerOtherBranchFrame = -1;
    bool talkerSawCancelled = false;
    bool alarm = false;

    // request lifecycle (requester, scripted after the load)
    std::map<std::string, RequestId> requestIds;
    std::map<std::string, int> requestIssued;
    std::map<std::string, RequestStatus> requestFinal;
};

namespace
{
    const BbValue* Key(const Agent* agent, const char* name)
    {
        if (agent == nullptr)
        {
            return nullptr;
        }
        const int slot = agent->instanceBlackboard.Find(name);
        return slot >= 0 ? &agent->instanceBlackboard.Get(static_cast<uint32_t>(slot)) : nullptr;
    }

    template <typename T> T KeyOr(const Agent* agent, const char* name, T fallback)
    {
        const BbValue* v = Key(agent, name);
        if (v != nullptr)
        {
            if (const T* t = std::get_if<T>(v))
            {
                return *t;
            }
        }
        return fallback;
    }
} // namespace

bool Application::setupBehaviorSmoke()
{
    auto smoke = std::make_shared<BehaviorSmoke>();
    smoke->slot = (std::filesystem::temp_directory_path() / "ranim_behavior_smoke" / "checkpoint").generic_string();
    mBehaviorSmoke = smoke;
    mSceneEditor.SetMode(Editor::Mode::View); // Edit mode pauses every agent (D17)
    mCharacterSystem.SyncFromScene(mSceneEditor.ModelData());
    for (EntityId id : {BehaviorSmoke::kPlayer, BehaviorSmoke::kCorridorA, BehaviorSmoke::kBouncer, BehaviorSmoke::kPatrol, BehaviorSmoke::kWatcher,
                        BehaviorSmoke::kGreeter, BehaviorSmoke::kTalker, BehaviorSmoke::kRequester})
    {
        if (mCharacterSystem.Find(id) == nullptr)
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke[behavior] agent {} is missing: --scene must be behavior_test.level.usda\n", id);
            return false;
        }
    }
    installBehaviorSmokeBrains();
    mAfterCheckpointLoad = [this]() { installBehaviorSmokeBrains(); };
    return true;
}

// The scripted player (possessed) and the scripted requester; called again after the checkpoint load, which
// rebuilds every agent from the level.
void Application::installBehaviorSmokeBrains()
{
    std::shared_ptr<BehaviorSmoke> s = mBehaviorSmoke;
    auto player = std::make_unique<ScriptBrain>(
            [s](int, DecisionContext& ctx, AgentIntent& intent)
            {
                intent.direct.reset();
                if (s->routeIndex >= BehaviorSmoke::kRouteLength || ctx.feedback == nullptr || ctx.world == nullptr)
                {
                    intent.locomotion = LocomotionIntent{};
                    return;
                }
                if (s->holdFrames > 0)
                {
                    --s->holdFrames;
                    intent.locomotion = LocomotionIntent{};
                    if (s->holdFrames == 0)
                    {
                        ++s->routeIndex;
                    }
                    return;
                }
                const std::optional<glm::vec3> target = ctx.world->FindMarker(MarkerKind::Waypoint, "player", s->routeIndex);
                if (!target)
                {
                    s->routeIndex = BehaviorSmoke::kRouteLength;
                    return;
                }
                const glm::vec3 p = ctx.feedback->motion.position;
                if (glm::length(glm::vec2(target->x - p.x, target->z - p.z)) <= 0.35f)
                {
                    intent.locomotion = LocomotionIntent{};
                    if (s->routeIndex == 7)
                    {
                        s->holdFrames = 45; // stand north of the FOV array (checked while holding)
                    }
                    else if (s->routeIndex == 8)
                    {
                        IntentCommand c;
                        c.type = CommandType::Interact;
                        intent.commands.push_back(c);
                        s->interactSent = true;
                        s->holdFrames = 240;
                    }
                    else
                    {
                        ++s->routeIndex;
                    }
                    return;
                }
                intent.locomotion.mode = LocomotionMode::MoveTo;
                intent.locomotion.target = *target;
                intent.locomotion.gait = Gait::Jog;
                intent.locomotion.arriveRadius = 0.3f;
            });
    mCharacterSystem.Possess(BehaviorSmoke::kPlayer, std::move(player));

    // request lifecycle: talk + cancel -> Cancelled, nod (not cancelable) + cancel -> Completed, unknown -> Rejected,
    // wave -> Completed
    auto requester = std::make_unique<ScriptBrain>(
            [s](int frame, DecisionContext& ctx, AgentIntent& intent)
            {
                auto request = [&](const char* name, const char* montage)
                {
                    IntentCommand c;
                    c.type = CommandType::Action;
                    c.montage = montage;
                    c.id = ctx.requests->Allocate();
                    intent.commands.push_back(c);
                    s->requestIds[name] = c.id;
                    s->requestIssued[name] = frame;
                };
                auto cancel = [&](const char* name)
                {
                    IntentCommand c;
                    c.type = CommandType::CancelAction;
                    c.cancelTarget = s->requestIds[name];
                    intent.commands.push_back(c);
                };
                if (frame == 0)
                    request("talk", "talk");
                if (frame == 20)
                    cancel("talk");
                if (frame == 40)
                    request("nod", "nod");
                if (frame == 50)
                    cancel("nod");
                if (frame == 200)
                    request("unknown", "no_such_montage");
                if (frame == 210)
                    request("wave", "wave");
                for (const auto& [name, id] : s->requestIds)
                {
                    // the feedback knows a request from the frame after it was issued (it reads Cancelled before)
                    if (frame <= s->requestIssued[name])
                    {
                        continue;
                    }
                    const RequestStatus st = ctx.feedback->Query(id);
                    if (IsFinished(st) && s->requestFinal.count(name) == 0)
                    {
                        s->requestFinal[name] = st;
                        fmt::print("smoke[behavior] requester: '{}' #{} -> {}\n", name, id, RequestStatusName(st));
                    }
                }
            });
    if (Agent* agent = mCharacterSystem.Find(BehaviorSmoke::kRequester))
    {
        // only after the load: the checkpoint replaces every agent anyway
        if (s->loadedOk)
        {
            mCharacterSystem.SetBrain(*agent, std::move(requester));
        }
    }
}

void Application::observeBehaviorFrame(int frame)
{
    BehaviorSmoke& s = *mBehaviorSmoke;
    const float now = mCharacterSystem.WorldTime();

    // contact events consumed by this frame's perception (produced by last frame's collision step)
    if (mCollisionSystem.IsInitialized())
    {
        for (const ContactEvent& e : mCollisionSystem.ContactEvents())
        {
            if (e.a == BehaviorSmoke::kCorridorA && e.b == BehaviorSmoke::kCorridorB)
            {
                (e.begin ? s.corridorBegin : s.corridorEnd)++;
                fmt::print("smoke[behavior] frame {}: corridor contact {}\n", frame, e.begin ? "Begin" : "End");
            }
        }
    }

    // bouncer: heading change after the wall
    if (const Agent* bouncer = mCharacterSystem.Find(BehaviorSmoke::kBouncer))
    {
        const float yaw = bouncer->controller->Motion().yawDeg;
        if (!s.bouncerStartKnown)
        {
            s.bouncerStartYaw = yaw;
            s.bouncerStartKnown = true;
        }
        if (!s.bouncerTouched && KeyOr<bool>(bouncer, PerceptionKeys::kBoundaryTouching, false))
        {
            s.bouncerTouched = true;
            fmt::print("smoke[behavior] frame {}: bouncer touches the boundary\n", frame);
        }
        if (s.bouncerTouched)
        {
            s.bouncerMaxTurn = std::max(s.bouncerMaxTurn, std::abs(WrapDeltaDeg(yaw, s.bouncerStartYaw)));
        }
    }

    // watcher: visible false -> true, ticked in that frame
    if (const Agent* watcher = mCharacterSystem.Find(BehaviorSmoke::kWatcher))
    {
        const bool visible = KeyOr<bool>(watcher, PerceptionKeys::kPlayerVisible, false);
        if (visible && !s.watcherWasVisible && s.watcherSeenFrame < 0)
        {
            s.watcherSeenFrame = frame;
            s.watcherTickedThatFrame = watcher->schedule.lastTickTime == now;
            s.watcherTickWasWake = watcher->schedule.lastTickWasWake;
            fmt::print("smoke[behavior] frame {}: watcher sees the player (ticked this frame: {}, by wake-up: {})\n", frame,
                       s.watcherTickedThatFrame, s.watcherTickWasWake);
        }
        s.watcherWasVisible = visible;
    }

    // player zones
    if (const Agent* player = mCharacterSystem.Find(BehaviorSmoke::kPlayer))
    {
        const std::string zone = KeyOr<std::string>(player, PerceptionKeys::kZone, std::string());
        if (s.zones.empty() || s.zones.back() != zone)
        {
            s.zones.push_back(zone);
            fmt::print("smoke[behavior] frame {}: player zone '{}'\n", frame, zone);
        }
    }

    // FOV array: while the player holds north of it
    if (s.routeIndex == 7 && s.holdFrames == 1 && !s.fovChecked)
    {
        s.fovChecked = true;
        bool ok = true;
        for (const auto& [id, expected] : std::vector<std::pair<EntityId, bool>>{
                     {BehaviorSmoke::kLookoutN, true}, {BehaviorSmoke::kLookoutE, false}, {BehaviorSmoke::kLookoutS, false}, {BehaviorSmoke::kLookoutW, false}})
        {
            const bool inFov = KeyOr<bool>(mCharacterSystem.Find(id), PerceptionKeys::kPlayerInFov, false);
            s.fovResult += fmt::format("{}:{} ", id, inFov ? "in" : "out");
            ok = ok && inFov == expected;
        }
        s.fovOk = ok;
        fmt::print("smoke[behavior] frame {}: FOV array {}\n", frame, s.fovResult);
    }

    // patrol progress, alarm, greeter
    s.patrolWp = std::max(s.patrolWp, KeyOr<int>(mCharacterSystem.Find(BehaviorSmoke::kPatrol), "wp", 0));
    const int alarmSlot = mBehaviorSystem.Global().Find("world.alarm");
    s.alarm = alarmSlot >= 0 && std::get<bool>(mBehaviorSystem.Global().Get(static_cast<uint32_t>(alarmSlot)));
    if (s.interactSent && s.interactionTarget == 0)
    {
        s.interactionTarget = mCharacterSystem.LastInteractionTarget();
    }
    if (const Agent* greeter = mCharacterSystem.Find(BehaviorSmoke::kGreeter))
    {
        for (const auto& [id, status] : greeter->feedback.requests)
        {
            if (status == RequestStatus::Completed && !s.greeterWaved)
            {
                s.greeterWaved = true;
                fmt::print("smoke[behavior] frame {}: greeter wave request #{} completed\n", frame, id);
            }
        }
    }

    // talker + checkpoint
    if (const Agent* talker = mCharacterSystem.Find(BehaviorSmoke::kTalker))
    {
        if (frame == BehaviorSmoke::kSaveFrame)
        {
            for (const auto& [id, status] : talker->feedback.requests)
            {
                s.talkerRunningAtSave = s.talkerRunningAtSave || status == RequestStatus::Accepted;
            }
            fmt::print("smoke[behavior] frame {}: checkpoint save (talker mid-PlayAction: {})\n", frame, s.talkerRunningAtSave);
            RequestCheckpointSave(s.slot);
            s.savedOk = true;
        }
        if (frame == BehaviorSmoke::kLoadFrame)
        {
            fmt::print("smoke[behavior] frame {}: checkpoint load\n", frame);
            RequestCheckpointLoad(s.slot);
            s.loadedOk = true;
        }
        if (frame > BehaviorSmoke::kLoadFrame && s.talkerOtherBranchFrame < 0 && KeyOr<bool>(talker, "otherBranch", false))
        {
            s.talkerOtherBranchFrame = frame;
            if (const auto* bt = dynamic_cast<const BehaviorTreeBrain*>(talker->brain.get()))
            {
                for (const TickLogEntry& e : bt->Log())
                {
                    s.talkerSawCancelled = s.talkerSawCancelled || e.text.find("Cancelled") != std::string::npos;
                }
            }
            fmt::print("smoke[behavior] frame {}: talker took the other branch (its request read Cancelled: {})\n", frame, s.talkerSawCancelled);
        }
    }
}

void Application::verifyBehaviorSmoke()
{
    BehaviorSmoke& s = *mBehaviorSmoke;
    bool ok = true;
    auto check = [&ok](bool condition, const std::string& what)
    {
        fmt::print(condition ? fmt::text_style{} : fg(fmt::color::red), "smoke[behavior] {} {}\n", condition ? "ok  " : "FAIL", what);
        ok = ok && condition;
    };

    // tick logs (tail) of the tree agents
    for (const auto& agent : mCharacterSystem.Agents())
    {
        const auto* bt = dynamic_cast<const BehaviorTreeBrain*>(agent->OwnBrain());
        if (bt == nullptr)
        {
            continue;
        }
        fmt::print("smoke[behavior] agent {} ({}): {} ticks ({} by wake-up), root {}\n", agent->entity, bt->Tree()->desc->name, agent->schedule.ticks,
                   agent->schedule.wakeTicks, NodeStatusName(bt->RootStatus()));
        const std::deque<TickLogEntry>& log = bt->Log();
        for (size_t i = log.size() > 4 ? log.size() - 4 : 0; i < log.size(); ++i)
        {
            const TickLogEntry& e = log[i];
            const int index = bt->Tree()->IndexOf(e.nodeId);
            fmt::print("smoke[behavior]     {:7.2f} s  {}  {} -> {} {}\n", e.time, index >= 0 ? bt->Tree()->nodes[static_cast<size_t>(index)].path : "?",
                       NodeStatusName(e.from), NodeStatusName(e.to), e.text);
        }
    }

    check(s.corridorBegin == 1 && s.corridorEnd == 1,
          fmt::format("corridor: contact Begin x{} / End x{} (exactly once each, Cooldown debounced the reaction)", s.corridorBegin, s.corridorEnd));
    check(s.bouncerTouched && s.bouncerMaxTurn > 90.0f, fmt::format("bouncer: touched the boundary and turned {:.0f} deg (> 90)", s.bouncerMaxTurn));
    check(s.watcherSeenFrame >= 0 && s.watcherTickedThatFrame,
          fmt::format("watcher: visible false -> true at frame {} and ticked in that frame (wake-up: {})", s.watcherSeenFrame, s.watcherTickWasWake));
    const auto seq = [&s]()
    {
        std::string text;
        for (const std::string& z : s.zones)
            text += "'" + z + "' ";
        return text;
    }();
    const auto contains = [&s](std::vector<std::string> wanted)
    {
        size_t next = 0;
        for (const std::string& z : s.zones)
        {
            if (next < wanted.size() && z == wanted[next])
                ++next;
        }
        return next == wanted.size();
    };
    check(contains({"", "Plaza", "Fountain", "Plaza", ""}), "zones: '' -> Plaza -> Fountain (nested: the smaller wins) -> Plaza -> '': " + seq);
    check(s.fovChecked && s.fovOk, "FOV array: only the north-facing lookout sees the player: " + s.fovResult);
    check(s.patrolWp >= 6, fmt::format("patrol: reached {} waypoints (route of 6)", s.patrolWp));
    check(s.alarm, "watcher raised the global world.alarm");
    check(s.interactionTarget == BehaviorSmoke::kGreeter && s.greeterWaved, fmt::format("interaction (U): target {} and the greeter's wave completed", s.interactionTarget));
    check(s.savedOk && s.loadedOk && s.talkerRunningAtSave, "checkpoint saved with the talker mid-PlayAction, then loaded");
    check(s.talkerOtherBranchFrame > 0 && s.talkerSawCancelled,
          fmt::format("after the load the talker's request read Cancelled -> other branch (frame {})", s.talkerOtherBranchFrame));
    auto final = [&s](const char* name) { return s.requestFinal.count(name) ? RequestStatusName(s.requestFinal[name]) : "unfinished"; };
    check(s.requestFinal.count("talk") && s.requestFinal["talk"] == RequestStatus::Cancelled, fmt::format("requests: talk + CancelAction -> {}", final("talk")));
    check(s.requestFinal.count("nod") && s.requestFinal["nod"] == RequestStatus::Completed,
          fmt::format("requests: nod (cancelable = false) + CancelAction -> {}", final("nod")));
    check(s.requestFinal.count("unknown") && s.requestFinal["unknown"] == RequestStatus::Rejected, fmt::format("requests: unknown montage -> {}", final("unknown")));
    check(s.requestFinal.count("wave") && s.requestFinal["wave"] == RequestStatus::Completed, fmt::format("requests: wave -> {}", final("wave")));
    fmt::print(ok ? fg(fmt::color::green) : fg(fmt::color::red), "smoke[behavior] {}\n", ok ? "OK" : "FAILED");
    mVerifyFailed = mVerifyFailed || !ok;
}
