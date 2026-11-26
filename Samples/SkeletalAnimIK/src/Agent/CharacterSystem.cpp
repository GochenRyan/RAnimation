#include <Agent/CharacterSystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include <fmt/format.h>

#include <Agent/PerceptionKeys.h>
#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>

namespace RAnimation
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        double MicrosSince(Clock::time_point t)
        {
            return std::chrono::duration<double, std::micro>(Clock::now() - t).count();
        }

        glm::vec3 Flat(glm::vec3 v)
        {
            v.y = 0.0f;
            return v;
        }
    } // namespace

    bool CharacterSystem::Init(const std::string& assetsDir, std::string& outError)
    {
        mShared = CharacterController::CreateShared(assetsDir, outError);
        return mShared != nullptr;
    }

    void CharacterSystem::Log(std::string line)
    {
        fmt::print("agents: {}\n", line);
        mEventLog.push_back(std::move(line));
        while (mEventLog.size() > 64)
        {
            mEventLog.pop_front();
        }
    }

    void CharacterSystem::SortAgents()
    {
        std::sort(mAgents.begin(), mAgents.end(), [](const auto& a, const auto& b) { return a->entity < b->entity; });
    }

    Agent* CharacterSystem::Find(EntityId id)
    {
        for (const auto& a : mAgents)
        {
            if (a->entity == id)
            {
                return a.get();
            }
        }
        return nullptr;
    }

    const Agent* CharacterSystem::Find(EntityId id) const
    {
        return const_cast<CharacterSystem*>(this)->Find(id);
    }

    Agent* CharacterSystem::FindByInstance(const ModelInstance* instance)
    {
        for (const auto& a : mAgents)
        {
            if (a->instance.get() == instance)
            {
                return a.get();
            }
        }
        return nullptr;
    }

    void CharacterSystem::InitBlackboard(Agent& agent)
    {
        const IDecisionMaker* own = agent.OwnBrain();
        const BlackboardSchema* schema = own != nullptr ? own->InstanceSchema() : nullptr;
        agent.instanceBlackboard.Init(schema != nullptr ? *schema : PerceptionSchema());
        agent.perception = PerceptionMemory{};
        if (agent.brain)
        {
            agent.brain->BindInstanceBlackboard(&agent.instanceBlackboard);
        }
        if (agent.parkedBrain)
        {
            agent.parkedBrain->BindInstanceBlackboard(&agent.instanceBlackboard);
        }
        Agent* a = &agent;
        // D10: perception writes wake the agent in this frame; anything later (another agent's tick, an
        // interaction request) wakes it next frame; its own writes never wake it.
        agent.instanceBlackboard.onChange = [this, a](uint32_t, EntityId writer)
        {
            if (writer == a->entity)
            {
                return;
            }
            // perception and writes between frames: due in the coming scheduling pass; writes made while
            // agents are being ticked / executed: next frame (D10, order independent)
            if (mPhase == Phase::Scheduling || mPhase == Phase::Execution)
                a->schedule.wakeFlagNext = true;
            else
                a->schedule.wakeFlag = true;
        };
    }

    Agent* CharacterSystem::AddHeadlessAgent(EntityId id, std::unique_ptr<IDecisionMaker> brain)
    {
        if (id == kInvalidEntityId || Find(id) != nullptr)
        {
            return nullptr;
        }
        auto agent = std::make_unique<Agent>();
        agent->entity = id;
        agent->brain = brain ? std::move(brain) : std::make_unique<IdleBrain>();
        InitBlackboard(*agent);
        ResetSchedule(*agent, true);
        Agent* raw = agent.get();
        mAgents.push_back(std::move(agent));
        SortAgents();
        return raw;
    }

    void CharacterSystem::ResetSchedule(Agent& agent, bool wake)
    {
        const float hz = agent.brain ? agent.brain->TickHz() : 0.0f;
        AgentSchedule& s = agent.schedule;
        s.intervalSec = hz > 0.0f ? 1.0f / hz : 0.0f;
        // Phase stagger (load balancing only): offset from the stable id, reproducible.
        const double phase = staggerPhases ? std::fmod(static_cast<double>(agent.entity) * 0.6180339887498949, 1.0) : 0.0;
        s.accumulator = static_cast<float>(phase) * s.intervalSec;
        s.nextWakeTime = std::numeric_limits<float>::infinity();
        s.wakeFlag = wake;
        s.wakeFlagNext = false;
    }

    Agent* CharacterSystem::AddAgent(const std::shared_ptr<ModelInstance>& instance, std::unique_ptr<IDecisionMaker> brain, std::string& outError)
    {
        if (!mShared)
        {
            outError = "character system is not initialised";
            return nullptr;
        }
        if (!instance || instance->GetEntityId() == kInvalidEntityId)
        {
            outError = "instance has no entity id";
            return nullptr;
        }
        if (Find(instance->GetEntityId()) != nullptr)
        {
            outError = fmt::format("entity {} already is an agent", instance->GetEntityId());
            return nullptr;
        }
        auto agent = std::make_unique<Agent>();
        agent->entity = instance->GetEntityId();
        agent->instance = instance;
        agent->controller = std::make_unique<CharacterController>();
        agent->controller->InitShared(mShared);
        if (!agent->controller->Attach(instance, outError))
        {
            return nullptr;
        }
        agent->brain = brain ? std::move(brain) : std::make_unique<IdleBrain>();
        agent->config = instance->GetAgentConfig();
        InitBlackboard(*agent);
        ResetSchedule(*agent, true);
        agent->controller->BuildFeedback(agent->feedback);
        Agent* raw = agent.get();
        mAgents.push_back(std::move(agent));
        SortAgents();
        Log(fmt::format("agent {} added ({})", raw->entity, raw->brain->Describe()));
        return raw;
    }

    void CharacterSystem::RemoveAgent(EntityId id)
    {
        auto it = std::find_if(mAgents.begin(), mAgents.end(), [id](const auto& a) { return a->entity == id; });
        if (it == mAgents.end())
        {
            return;
        }
        if ((*it)->controller)
        {
            (*it)->controller->Detach();
        }
        if (mPossessed == id)
        {
            mPossessed = kInvalidEntityId;
        }
        Log(fmt::format("agent {} removed", id));
        mAgents.erase(it);
    }

    void CharacterSystem::Clear()
    {
        for (const auto& a : mAgents)
        {
            if (a->controller)
            {
                a->controller->Detach();
            }
        }
        mAgents.clear();
        mPossessed = kInvalidEntityId;
        mRejected.clear();
    }

    void CharacterSystem::SetBrain(Agent& agent, std::unique_ptr<IDecisionMaker> brain)
    {
        if (!brain)
        {
            brain = std::make_unique<IdleBrain>();
        }
        if (agent.parkedBrain)
        {
            agent.parkedBrain = std::move(brain);
            agent.parkedBrain->Reset();
            InitBlackboard(agent);
            return;
        }
        if (agent.brain)
        {
            agent.brain->Halt(agent.intent);
        }
        agent.brain = std::move(brain);
        agent.brain->Reset();
        agent.intent = AgentIntent{};
        InitBlackboard(agent);
        ResetSchedule(agent, true);
    }

    void CharacterSystem::RebuildBrain(Agent& agent)
    {
        std::unique_ptr<IDecisionMaker> brain;
        if (agent.config && mBrainFactory)
        {
            std::string error;
            brain = mBrainFactory(*agent.config, agent.entity, error);
            if (!error.empty())
            {
                Log(fmt::format("agent {}: {}", agent.entity, error));
            }
        }
        SetBrain(agent, std::move(brain));
    }

    bool CharacterSystem::Possess(EntityId id, std::unique_ptr<IDecisionMaker> playerBrain)
    {
        if (id != kInvalidEntityId && Find(id) == nullptr)
        {
            return false;
        }
        // the current player agent gets its own brain back and starts over (D3)
        if (mPossessed != kInvalidEntityId)
        {
            if (Agent* a = Find(mPossessed))
            {
                if (a->parkedBrain)
                {
                    a->brain->Halt(a->intent);
                    a->brain = std::move(a->parkedBrain);
                    a->brain->Reset();
                    a->intent = AgentIntent{};
                    ResetSchedule(*a, true);
                }
            }
            Log(fmt::format("released agent {}", mPossessed));
            mPossessed = kInvalidEntityId;
        }
        if (id == kInvalidEntityId)
        {
            return true;
        }
        Agent* b = Find(id);
        // halt: every running node stops, channels are handed back, actions cancel by their own rules; the
        // instance blackboard is kept (13.1 #1)
        b->brain->Halt(b->intent);
        // the Halt may have queued CancelAction commands: execute them before the player takes over
        for (const IntentCommand& c : b->intent.commands)
        {
            if (c.type == CommandType::CancelAction && b->controller)
            {
                b->controller->CancelAction(c.cancelTarget);
            }
        }
        b->parkedBrain = std::move(b->brain);
        b->brain = playerBrain ? std::move(playerBrain) : std::make_unique<PlayerBrain>(&mPlayerInput);
        b->intent = AgentIntent{};
        ResetSchedule(*b, true);
        mPossessed = id;
        Log(fmt::format("possessed agent {} ({})", id, b->brain->Kind()));
        return true;
    }

    void CharacterSystem::WakeNow(EntityId id)
    {
        if (Agent* a = Find(id))
        {
            a->schedule.wakeFlag = true;
        }
    }

    void CharacterSystem::WakeNextFrame(EntityId id)
    {
        if (Agent* a = Find(id))
        {
            if (mPhase == Phase::Scheduling || mPhase == Phase::Execution)
                a->schedule.wakeFlagNext = true;
            else
                a->schedule.wakeFlag = true;
        }
    }

    void CharacterSystem::SyncFromScene(ModelAndInstanceData& scene)
    {
        mScene = &scene;
        // agents whose instance left the scene (deleted, undo of an import) or lost its config
        std::vector<EntityId> remove;
        for (const auto& a : mAgents)
        {
            if (!a->instance)
            {
                continue; // headless
            }
            const bool inScene = std::find(scene.miModelInstances.begin(), scene.miModelInstances.end(), a->instance) !=
                                 scene.miModelInstances.end();
            if (!inScene || (a->fromConfig && !a->instance->GetAgentConfig()))
            {
                remove.push_back(a->entity);
            }
        }
        for (EntityId id : remove)
        {
            RemoveAgent(id);
        }

        for (const std::shared_ptr<ModelInstance>& instance : scene.miModelInstances)
        {
            const std::optional<AgentConfig>& config = instance->GetAgentConfig();
            if (!config || scene.IsStaticLayerModel(instance->GetModel().get()))
            {
                continue;
            }
            Agent* agent = FindByInstance(instance.get());
            if (agent == nullptr)
            {
                if (mRejected.count(instance->GetEntityId()) != 0)
                {
                    continue;
                }
                std::unique_ptr<IDecisionMaker> brain;
                std::string error;
                if (mBrainFactory)
                {
                    brain = mBrainFactory(*config, instance->GetEntityId(), error);
                }
                std::string attachError;
                agent = AddAgent(instance, std::move(brain), attachError);
                if (agent == nullptr)
                {
                    mRejected.insert(instance->GetEntityId());
                    Log(fmt::format("entity {} cannot be an agent: {}", instance->GetEntityId(), attachError));
                    continue;
                }
                if (!error.empty())
                {
                    Log(fmt::format("agent {}: {}", agent->entity, error));
                }
                agent->fromConfig = true;
                if (config->possessed && mPossessed == kInvalidEntityId)
                {
                    Possess(agent->entity);
                }
                continue;
            }
            // tree / tick rate edited: rebuild the agent's own brain (perception parameters are read live)
            if (agent->config && (agent->config->tree != config->tree || agent->config->tickHz != config->tickHz))
            {
                agent->config = config;
                RebuildBrain(*agent);
            }
            else
            {
                agent->config = config;
            }
        }
    }

    void CharacterSystem::ApplyPush(Agent& agent, const glm::vec3& delta)
    {
        agent.controller->ApplyPush(delta);
    }

    void CharacterSystem::Update(float deltaTime, ModelAndInstanceData& scene, bool paused, const PerceptionInputs& perception)
    {
        mScene = &scene;
        mStats = Stats{};
        mStats.agents = static_cast<int>(mAgents.size());
        if (!paused)
        {
            mWorldTime += deltaTime;
        }

        // (1) perception
        auto t0 = Clock::now();
        if (!paused)
        {
            mPhase = Phase::Perception;
            UpdatePerception(perception);
        }
        mStats.perceptionUs = MicrosSince(t0);

        // (2) scheduling, ascending entity id; at most one tick per agent per frame
        t0 = Clock::now();
        mPhase = Phase::Scheduling;
        if (!paused)
        {
            for (const auto& ptr : mAgents)
            {
                Agent& agent = *ptr;
                AgentSchedule& s = agent.schedule;
                s.accumulator += deltaTime;
                const bool regular = s.intervalSec <= 0.0f || s.accumulator >= s.intervalSec;
                const bool timer = mWorldTime >= s.nextWakeTime;
                const bool woken = s.wakeFlag || timer || !agent.feedback.statusChanges.empty();
                if (!regular && !woken)
                {
                    continue;
                }
                DecisionContext ctx;
                ctx.worldTime = mWorldTime;
                ctx.deltaSinceLastTick = mWorldTime - s.lastTickTime;
                ctx.self = agent.entity;
                ctx.feedback = &agent.feedback;
                ctx.instanceBlackboard = &agent.instanceBlackboard;
                ctx.globalBlackboard = globalBlackboard;
                ctx.requests = &agent.requests;
                ctx.world = this;
                agent.brain->Tick(ctx, agent.intent);
                ++s.ticks;
                s.lastTickWasWake = !regular;
                if (!regular)
                {
                    ++s.wakeTicks;
                    ++mStats.woken;
                }
                ++mStats.ticked;
                s.accumulator = 0.0f;
                s.wakeFlag = false;
                s.nextWakeTime = agent.brain->NextWakeTime();
                s.lastTickTime = mWorldTime;
                // the status changes were seen; do not let them re-trigger a tick next frame
                agent.feedback.statusChanges.clear();
            }
        }
        mStats.decisionUs = MicrosSince(t0);

        // (3) + (4) steering and the controllers
        t0 = Clock::now();
        mPhase = Phase::Execution;
        for (const auto& ptr : mAgents)
        {
            ExecuteAgent(*ptr, deltaTime, scene, paused);
        }
        mStats.executionUs = MicrosSince(t0);

        // frame end: deferred wakes become due (D10)
        for (const auto& ptr : mAgents)
        {
            ptr->schedule.wakeFlag = ptr->schedule.wakeFlag || ptr->schedule.wakeFlagNext;
            ptr->schedule.wakeFlagNext = false;
        }
        mStats.rays = perceptionSettings.raysThisFrame;
        mPhase = Phase::Idle;
    }

    void CharacterSystem::ExecuteAgent(Agent& agent, float deltaTime, ModelAndInstanceData& scene, bool paused)
    {
        if (!agent.controller)
        {
            agent.intent.commands.clear(); // headless: decisions only
            return;
        }
        CharacterController& controller = *agent.controller;
        SteeringResult steer;
        if (!paused)
        {
            std::optional<glm::vec3> target = agent.intent.locomotion.target;
            if (agent.intent.locomotion.targetEntity != kInvalidEntityId)
            {
                target = Position(agent.intent.locomotion.targetEntity);
            }
            Steer(agent.intent, controller.Motion(), agent.feedback, target, steering, agent.steering, steer);
        }
        agent.intent.commands.clear();

        controller.SetInput(steer.input);
        for (const IntentCommand& c : steer.commands)
        {
            switch (c.type)
            {
                case CommandType::Jump: controller.Jump(); break;
                case CommandType::Action: controller.RequestAction(c.montage, c.id); break;
                case CommandType::CancelAction: controller.CancelAction(c.cancelTarget); break;
                case CommandType::Die: controller.Die(); break;
                case CommandType::ToggleWeapon: controller.ToggleWeapon(); break;
                case CommandType::Interact: HandleInteraction(agent); break;
            }
        }
        controller.Update(deltaTime, scene);

        // ExecutionFeedback: controller part + what only the intent layer knows (arrival, facing, blocked)
        std::vector<std::pair<RequestId, RequestStatus>> pending = std::move(agent.feedback.statusChanges);
        controller.BuildFeedback(agent.feedback);
        // changes not consumed yet (agent not ticked since) stay visible
        agent.feedback.statusChanges.insert(agent.feedback.statusChanges.begin(), pending.begin(), pending.end());
        agent.feedback.arrived = steer.arrived;
        agent.feedback.distanceToTarget = steer.distance;
        agent.feedback.facingErrorDeg = steer.facingErrorDeg;

        const LocomotionIntent& loco = agent.intent.locomotion;
        const glm::vec3 position = controller.Motion().position;
        if (paused || loco.mode != LocomotionMode::MoveTo || steer.arrived || agent.intent.direct)
        {
            agent.feedback.blocked = false;
            agent.blockedAnchor = position;
            agent.blockedAnchorTime = mWorldTime;
            agent.lastMode = loco.mode;
            return;
        }
        const glm::vec3 target = loco.targetEntity != kInvalidEntityId ? Position(loco.targetEntity).value_or(loco.target) : loco.target;
        if (agent.lastMode != LocomotionMode::MoveTo || glm::length(Flat(target - agent.lastTarget)) > 0.05f)
        {
            agent.feedback.blocked = false;
            agent.blockedAnchor = position;
            agent.blockedAnchorTime = mWorldTime;
        }
        else if (mWorldTime - agent.blockedAnchorTime >= blockedSeconds)
        {
            agent.feedback.blocked = glm::length(Flat(position - agent.blockedAnchor)) < blockedEpsilon;
            agent.blockedAnchor = position;
            agent.blockedAnchorTime = mWorldTime;
        }
        agent.lastTarget = target;
        agent.lastMode = loco.mode;
    }

    // ---- perception (§7) ---------------------------------------------------------------------------------

    void CharacterSystem::UpdatePerception(const PerceptionInputs& inputs)
    {
        using namespace PerceptionKeys;
        perceptionSettings.raysThisFrame = 0;
        const float now = mWorldTime;

        // contact and boundary events (one frame late, merged per entity pair by CollisionSystem)
        for (const ContactEvent& e : inputs.contacts)
        {
            Agent* a = Find(e.a);
            if (a == nullptr || !a->controller)
            {
                continue;
            }
            Blackboard& bb = a->instanceBlackboard;
            if (e.staticBody)
            {
                if (e.tag != StaticColliderTag::Boundary)
                {
                    continue;
                }
                a->perception.boundaryContacts = std::max(0, a->perception.boundaryContacts + (e.begin ? 1 : -1));
                if (e.begin)
                {
                    bb.SetByName(kBoundaryTime, now, kInvalidEntityId);
                    bb.SetByName(kBoundaryNormal, e.normalOnA, kInvalidEntityId);
                }
                bb.SetByName(kBoundaryTouching, a->perception.boundaryContacts > 0, kInvalidEntityId);
                continue;
            }
            if (e.begin)
            {
                a->perception.touching.insert(e.b);
                bb.SetByName(kContactEntity, e.b, kInvalidEntityId);
                bb.SetByName(kContactTime, now, kInvalidEntityId);
            }
            else
            {
                a->perception.touching.erase(e.b);
            }
            bb.SetByName(kContactCount, static_cast<int>(a->perception.touching.size()), kInvalidEntityId);
        }

        const Agent* player = Find(mPossessed);
        if (player != nullptr && !player->controller)
        {
            player = nullptr;
        }
        const glm::vec3 playerPos = player ? player->controller->Motion().position : glm::vec3(0.0f);

        for (const auto& ptr : mAgents)
        {
            Agent& agent = *ptr;
            if (!agent.controller)
            {
                continue;
            }
            Blackboard& bb = agent.instanceBlackboard;
            const MotionState& motion = agent.controller->Motion();
            const float fovHalf = agent.config ? agent.config->fovHalfDeg : 60.0f;
            const float sight = agent.config ? agent.config->sightRange : 20.0f;
            const float proximity = agent.config ? agent.config->proximityRadius : 8.0f;

            // nearest other agent (distance only)
            EntityId nearest = kInvalidEntityId;
            float nearestDistance = std::numeric_limits<float>::max();
            for (const auto& other : mAgents)
            {
                if (other.get() == &agent || !other->controller)
                {
                    continue;
                }
                const float d = glm::length(Flat(other->controller->Motion().position - motion.position));
                if (d < nearestDistance)
                {
                    nearestDistance = d;
                    nearest = other->entity;
                }
            }
            if (nearest != kInvalidEntityId)
                bb.SetByName(kNearestEntity, nearest, kInvalidEntityId);
            else
                bb.Clear(static_cast<uint32_t>(std::max(0, bb.Find(kNearestEntity))), kInvalidEntityId);

            // player: distance, range, field of view, line of sight
            if (player != nullptr && player != &agent)
            {
                const glm::vec3 toPlayer = Flat(playerPos - motion.position);
                const float distance = glm::length(toPlayer);
                bb.SetByName(kPlayerEntity, player->entity, kInvalidEntityId);
                bb.SetByName(kPlayerDistance, distance, kInvalidEntityId);
                bb.SetByName(kPlayerInRange, distance <= proximity, kInvalidEntityId);
                bool inFov = false;
                if (distance <= sight)
                {
                    const glm::vec3 forward = ForwardFromYaw(motion.yawDeg);
                    const float cosAngle = distance > 1e-4f ? glm::dot(forward, toPlayer / distance) : 1.0f;
                    inFov = cosAngle >= std::cos(glm::radians(fovHalf));
                }
                bb.SetByName(kPlayerInFov, inFov, kInvalidEntityId);

                bool visible = false;
                if (inFov)
                {
                    // eye: the head joint when the model has one, else a fixed height
                    glm::vec3 eye = motion.position + glm::vec3(0.0f, perceptionSettings.eyeHeight, 0.0f);
                    const std::shared_ptr<Model> model = agent.instance->GetModel();
                    int32_t head = model->FindNodeIndex("head");
                    if (head < 0)
                    {
                        head = model->FindNodeIndex("Head");
                    }
                    if (head >= 0)
                    {
                        std::vector<glm::mat4> globals;
                        model->ComputeNodeGlobals(agent.instance->GetCpuPose(), agent.instance->GetLocalTransformMatrix(), globals);
                        if (static_cast<size_t>(head) < globals.size())
                        {
                            eye = glm::vec3(globals[static_cast<size_t>(head)][3]);
                        }
                    }
                    const glm::vec3 chest = playerPos + glm::vec3(0.0f, perceptionSettings.chestHeight, 0.0f);
                    ++perceptionSettings.raysThisFrame;
                    visible = !inputs.lineBlocked || !inputs.lineBlocked(eye, chest);
                }
                bb.SetByName(kPlayerVisible, visible, kInvalidEntityId);
                if (visible)
                {
                    bb.SetByName(kPlayerLastSeenPos, playerPos, kInvalidEntityId);
                    bb.SetByName(kPlayerLastSeenTime, now, kInvalidEntityId);
                }
                agent.perception.visible = visible;
            }
            else
            {
                bb.SetByName(kPlayerInRange, false, kInvalidEntityId);
                bb.SetByName(kPlayerInFov, false, kInvalidEntityId);
                bb.SetByName(kPlayerVisible, false, kInvalidEntityId);
                if (player == nullptr)
                {
                    const int slot = bb.Find(kPlayerEntity);
                    if (slot >= 0)
                        bb.Clear(static_cast<uint32_t>(slot), kInvalidEntityId);
                }
                agent.perception.visible = false;
            }

            // zone: the smallest zone containing the agent (nested zones report the inner one)
            std::string zone;
            float zoneVolume = std::numeric_limits<float>::max();
            if (inputs.staticLayers != nullptr)
            {
                const glm::vec3 probe = motion.position + glm::vec3(0.0f, 0.1f, 0.0f);
                for (const StaticLayer& layer : *inputs.staticLayers)
                {
                    if (const StaticZone* z = layer.statics.ZoneAt(probe); z != nullptr && z->box.Volume() < zoneVolume)
                    {
                        zone = z->name;
                        zoneVolume = z->box.Volume();
                    }
                }
            }
            if (!agent.perception.zoneValid || zone != agent.perception.zone)
            {
                agent.perception.zone = zone;
                agent.perception.zoneValid = true;
                bb.SetByName(kZone, zone, kInvalidEntityId);
                bb.SetByName(kZoneEnterTime, now, kInvalidEntityId);
            }
        }
    }

    // P7 / book §10: the player asks to interact. Four filters: in range -> not too close -> both see each
    // other (the player faces the NPC and the NPC faces the player) -> the nearest one wins.
    void CharacterSystem::HandleInteraction(Agent& player)
    {
        using namespace PerceptionKeys;
        const MotionState& p = player.controller->Motion();
        const glm::vec3 playerForward = ForwardFromYaw(p.yawDeg);
        const float cosFov = std::cos(glm::radians(interactFovHalfDeg));
        Agent* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        for (const auto& ptr : mAgents)
        {
            Agent& other = *ptr;
            if (&other == &player || !other.controller)
            {
                continue;
            }
            const MotionState& o = other.controller->Motion();
            const glm::vec3 delta = Flat(o.position - p.position);
            const float distance = glm::length(delta);
            if (distance > interactMaxDistance || distance < interactMinDistance)
            {
                continue;
            }
            const glm::vec3 dir = delta / distance;
            if (glm::dot(playerForward, dir) < cosFov || glm::dot(ForwardFromYaw(o.yawDeg), -dir) < cosFov)
            {
                continue;
            }
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &other;
            }
        }
        mLastInteractionTarget = best ? best->entity : kInvalidEntityId;
        if (best == nullptr)
        {
            Log(fmt::format("interaction from {}: nobody in range and facing", player.entity));
            return;
        }
        best->instanceBlackboard.SetByName(kInteractionFrom, player.entity, kInvalidEntityId);
        best->instanceBlackboard.SetByName(kInteractionTime, mWorldTime, kInvalidEntityId);
        Log(fmt::format("interaction from {} -> agent {} ({:.2f} m)", player.entity, best->entity, bestDistance));
    }

    // ---- WorldQuery --------------------------------------------------------------------------------------

    std::optional<glm::vec3> CharacterSystem::Position(EntityId id) const
    {
        if (const Agent* a = Find(id); a != nullptr && a->controller)
        {
            return a->controller->Motion().position;
        }
        if (mScene != nullptr && id != kInvalidEntityId)
        {
            for (const auto& instance : mScene->miModelInstances)
            {
                if (instance->GetEntityId() == id)
                {
                    return instance->GetWorldPosition();
                }
            }
        }
        return std::nullopt;
    }

    std::optional<float> CharacterSystem::YawDeg(EntityId id) const
    {
        if (const Agent* a = Find(id); a != nullptr && a->controller)
        {
            return a->controller->Motion().yawDeg;
        }
        if (mScene != nullptr && id != kInvalidEntityId)
        {
            for (const auto& instance : mScene->miModelInstances)
            {
                if (instance->GetEntityId() == id)
                {
                    return instance->GetInstanceSettings().mWorldRotation.y;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<glm::vec3> CharacterSystem::FindMarker(MarkerKind kind, const std::string& name, int index) const
    {
        if (mScene == nullptr)
        {
            return std::nullopt;
        }
        for (const StaticLayer& layer : mScene->miStaticLayers)
        {
            const LevelStatics& s = layer.statics;
            switch (kind)
            {
                case MarkerKind::Waypoint:
                {
                    const std::vector<const StaticMarker*> route = s.Route(name);
                    if (!route.empty())
                    {
                        const int n = static_cast<int>(route.size());
                        return route[static_cast<size_t>(((index % n) + n) % n)]->position;
                    }
                    break;
                }
                case MarkerKind::Zone:
                    for (const StaticZone& z : s.zones)
                    {
                        if (z.name == name)
                        {
                            return glm::vec3(z.box.center.x, z.box.center.y - z.box.halfExtents.y, z.box.center.z);
                        }
                    }
                    break;
                case MarkerKind::Spawn:
                    for (const StaticMarker& m : s.markers)
                    {
                        if (m.kind == StaticMarkerKind::Spawn && (m.name == name || m.name == "SPAWN_" + name))
                        {
                            return m.position;
                        }
                    }
                    break;
            }
        }
        return std::nullopt;
    }

    int CharacterSystem::RouteLength(const std::string& route) const
    {
        if (mScene == nullptr)
        {
            return 0;
        }
        int n = 0;
        for (const StaticLayer& layer : mScene->miStaticLayers)
        {
            n += static_cast<int>(layer.statics.Route(route).size());
        }
        return n;
    }
} // namespace RAnimation
