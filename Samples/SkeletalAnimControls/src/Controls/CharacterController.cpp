#include <Controls/CharacterController.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <filesystem>
#include <limits>

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <AnimGraph/AnimGraphAsset.h>
#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/AnimNode.h>
#include <AnimGraph/AnimationSystem.h>
#include <AnimGraph/GraphBaker.h>
#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>
#include <StateMachine/StateMachineUsdIo.h>

namespace RAnimation
{
    namespace
    {
        constexpr int kSlotUpper = 0;
        constexpr int kSlotFull = 1;

        CharacterStateData* DataOf(StateContext& ctx) { return ctx.UserData<CharacterStateData>(); }

        const CharacterInput* InputOf(StateContext& ctx)
        {
            const CharacterStateData* data = DataOf(ctx);
            return data != nullptr ? data->input : nullptr;
        }

        const MotionState* MotionOf(StateContext& ctx)
        {
            const CharacterStateData* data = DataOf(ctx);
            return data != nullptr ? data->motion : nullptr;
        }

        const GraphFeedback* FeedbackOf(StateContext& ctx)
        {
            const CharacterStateData* data = DataOf(ctx);
            return data != nullptr ? data->graph : nullptr;
        }

        AnimParams* AnimOf(StateContext& ctx)
        {
            CharacterStateData* data = DataOf(ctx);
            return data != nullptr ? data->anim : nullptr;
        }

        const CharacterProfile* ProfileOf(StateContext& ctx)
        {
            const CharacterStateData* data = DataOf(ctx);
            return data != nullptr ? data->profile : nullptr;
        }

        // The montage name an "Action" event carries.
        std::string EventMontage(const Event* event)
        {
            if (event == nullptr)
            {
                return {};
            }
            if (const auto* s = std::any_cast<std::string>(&event->payload))
            {
                return *s;
            }
            if (const auto* c = std::any_cast<const char*>(&event->payload))
            {
                return *c;
            }
            return {};
        }

        int SlotIndex(const std::string& slot) { return slot == CharacterController::kFullBodySlot ? kSlotFull : kSlotUpper; }

        float SpeedForRow(const CharacterProfile* profile, const std::string& row, bool run)
        {
            if (profile == nullptr)
            {
                return 0.0f;
            }
            if (row == "crouch")
            {
                return profile->crouchSpeed;
            }
            if (row == "sprint")
            {
                return profile->sprintSpeed;
            }
            return run ? profile->jogSpeed : profile->walkSpeed;
        }

        // Counts updates since OnEnter: the graph applies a request one frame later, so "done" and "idle"
        // are only meaningful from the second update on.
        struct FrameCountData final : public IStateInstanceData
        {
            int frames = 0;
            void Serialize(ParamBag& out) const override { out.Set("frames", frames); }
            void Deserialize(const ParamBag& in) override { frames = in.Get<int>("frames", 0); }
        };

        // Stand / Crouch / Sprint.Loop: selects a locomotion ring and moves at the row's speed.
        class LocomotionState final : public SMState
        {
        public:
            using SMState::SMState;

            void OnEnter(StateContext& ctx) override
            {
                if (AnimParams* anim = AnimOf(ctx))
                {
                    anim->locoSelect = GetParams().Get<std::string>("locoSelect", "stand");
                    anim->locoRestart = false;
                }
            }

            void OnUpdate(StateContext& ctx, float) override
            {
                const CharacterInput* in = InputOf(ctx);
                AnimParams* anim = AnimOf(ctx);
                if (in == nullptr || anim == nullptr)
                {
                    return;
                }
                const std::string row = GetParams().Get<std::string>("row", "stand");
                anim->moveSpeed = in->MoveMagnitude() * SpeedForRow(ProfileOf(ctx), row, in->run);
            }

            void OnExit(StateContext& ctx) override
            {
                if (AnimParams* anim = AnimOf(ctx))
                {
                    anim->moveSpeed = 0.0f;
                }
            }
        };

        // One-shot branch of the selector (Sprint.Enter / Exit, Turn, Airborne.Start / Land, Dead): switches
        // the selector with a restart and completes when the graph reports the branch's clips ended.
        class SelectState final : public SMState
        {
        public:
            using SMState::SMState;

            std::unique_ptr<IStateInstanceData> CreateRuntimeData() const override { return std::make_unique<FrameCountData>(); }

            void OnEnter(StateContext& ctx) override
            {
                if (auto* data = ctx.GetStateData<FrameCountData>())
                {
                    data->frames = 0;
                }
                if (AnimParams* anim = AnimOf(ctx))
                {
                    anim->locoSelect = GetParams().Get<std::string>("locoSelect", "stand");
                    anim->locoRestart = GetParams().Get<bool>("restart", true);
                    if (GetParams().Get<bool>("keepMoving", false))
                    {
                        // Airborne keeps the horizontal speed it took off with.
                    }
                    else
                    {
                        anim->moveSpeed = 0.0f;
                    }
                    if (GetParams().Get<bool>("dead", false))
                    {
                        anim->dead = true;
                    }
                }
            }

            void OnUpdate(StateContext& ctx, float) override
            {
                if (auto* data = ctx.GetStateData<FrameCountData>())
                {
                    ++data->frames;
                }
            }

            bool IsDone(StateContext& ctx) const override
            {
                const auto* data = ctx.GetStateData<FrameCountData>();
                const GraphFeedback* fb = FeedbackOf(ctx);
                return data != nullptr && fb != nullptr && data->frames >= 2 && fb->locoDone;
            }
        };

        // Action region: a slot is busy with the montage named by the "Action" event that entered it.
        class SlotBusyState final : public SMState
        {
        public:
            using SMState::SMState;

            std::unique_ptr<IStateInstanceData> CreateRuntimeData() const override { return std::make_unique<FrameCountData>(); }

            void OnEnter(StateContext& ctx) override
            {
                if (auto* data = ctx.GetStateData<FrameCountData>())
                {
                    data->frames = 0;
                }
                AnimParams* anim = AnimOf(ctx);
                if (anim == nullptr)
                {
                    return;
                }
                const std::string slot = GetParams().Get<std::string>("slot", CharacterController::kUpperBodySlot);
                // OnEnter runs without the event; the transition's RememberMontage action parked the name.
                const std::string montage = anim->pendingMontage;
                anim->pendingMontage.clear();
                anim->slotRequests.push_back({slot, montage});
                anim->currentMontage[SlotIndex(slot)] = montage;
                const CharacterProfile* profile = ProfileOf(ctx);
                const ProfileMontage* pm = profile != nullptr ? profile->FindMontage(montage) : nullptr;
                anim->movementLock = pm != nullptr && pm->desc.lockMovement;
            }

            void OnUpdate(StateContext& ctx, float) override
            {
                if (auto* data = ctx.GetStateData<FrameCountData>())
                {
                    ++data->frames;
                }
            }

            void OnExit(StateContext& ctx) override
            {
                if (AnimParams* anim = AnimOf(ctx))
                {
                    const std::string slot = GetParams().Get<std::string>("slot", CharacterController::kUpperBodySlot);
                    anim->currentMontage[SlotIndex(slot)].clear();
                    anim->movementLock = false;
                }
            }

            bool IsDone(StateContext& ctx) const override
            {
                const auto* data = ctx.GetStateData<FrameCountData>();
                const GraphFeedback* fb = FeedbackOf(ctx);
                if (data == nullptr || fb == nullptr || data->frames < 2)
                {
                    return false;
                }
                const std::string slot = GetParams().Get<std::string>("slot", CharacterController::kUpperBodySlot);
                return SlotIndex(slot) == kSlotFull ? fb->fullBodyIdle : fb->upperBodyIdle;
            }
        };

        // Weapon region: sets the upper body layer target.
        class WeaponState final : public SMState
        {
        public:
            using SMState::SMState;

            void OnEnter(StateContext& ctx) override
            {
                if (AnimParams* anim = AnimOf(ctx))
                {
                    anim->upperBodyAlphaTarget = GetParams().Get<float>("alpha", 0.0f);
                }
            }
        };

        // The event's montage exists in the profile, targets `slot`, and the character is alive.
        bool ActionTargetsSlot(StateContext& ctx, const Event* event, const char* slot)
        {
            const CharacterProfile* profile = ProfileOf(ctx);
            const AnimParams* anim = AnimOf(ctx);
            const ProfileMontage* pm = profile != nullptr ? profile->FindMontage(EventMontage(event)) : nullptr;
            return pm != nullptr && pm->slot == slot && (anim == nullptr || !anim->dead);
        }

        // Strictly higher priority than whatever the busy states currently hold (either slot): a full-body
        // hit is not interrupted by an upper-body punch just because the upper slot happens to be free.
        bool ActionOutranks(StateContext& ctx, const Event* event)
        {
            const CharacterProfile* profile = ProfileOf(ctx);
            const AnimParams* anim = AnimOf(ctx);
            if (profile == nullptr || anim == nullptr)
            {
                return false;
            }
            const ProfileMontage* incoming = profile->FindMontage(EventMontage(event));
            if (incoming == nullptr)
            {
                return false;
            }
            int current = std::numeric_limits<int>::min();
            bool any = false;
            for (const std::string& name : anim->currentMontage)
            {
                if (const ProfileMontage* pm = profile->FindMontage(name))
                {
                    current = std::max(current, pm->desc.priority);
                    any = true;
                }
            }
            return !any || incoming->desc.priority > current;
        }
    } // namespace

    void CharacterController::RegisterCharacterTypes(StateTypeRegistry& registry)
    {
        registry.RegisterStateType("LocomotionState",
                                   [](const std::string& name, const ParamBag&)
                                   { return std::make_unique<LocomotionState>(name); });
        registry.RegisterStateType("SelectState",
                                   [](const std::string& name, const ParamBag&) { return std::make_unique<SelectState>(name); });
        registry.RegisterStateType("SlotBusyState",
                                   [](const std::string& name, const ParamBag&)
                                   { return std::make_unique<SlotBusyState>(name); });
        registry.RegisterStateType("WeaponState",
                                   [](const std::string& name, const ParamBag&) { return std::make_unique<WeaponState>(name); });

        // ---- motion / input guards (never animation state) ----------------------------------------------
        registry.RegisterGuard("Airborne",
                               [](StateContext& ctx, const Event*)
                               {
                                   const MotionState* m = MotionOf(ctx);
                                   const CharacterStateData* data = DataOf(ctx);
                                   const bool inAirborne = data != nullptr && data->airborneState != kInvalidStateId &&
                                                           ctx.Manager().IsActive(data->airborneState);
                                   return m != nullptr && m->airborne && !inAirborne;
                               });
        registry.RegisterGuard("Grounded",
                               [](StateContext& ctx, const Event*)
                               {
                                   const MotionState* m = MotionOf(ctx);
                                   return m != nullptr && m->grounded;
                               });
        registry.RegisterGuard("WantCrouch",
                               [](StateContext& ctx, const Event*)
                               {
                                   const CharacterInput* in = InputOf(ctx);
                                   return in != nullptr && in->crouch;
                               });
        registry.RegisterGuard("WantStand",
                               [](StateContext& ctx, const Event*)
                               {
                                   const CharacterInput* in = InputOf(ctx);
                                   return in == nullptr || !in->crouch;
                               });
        registry.RegisterGuard("WantSprint",
                               [](StateContext& ctx, const Event*)
                               {
                                   const CharacterInput* in = InputOf(ctx);
                                   return in != nullptr && in->sprint && in->MoveMagnitude() > 0.0f && !in->crouch;
                               });
        registry.RegisterGuard("StopSprint",
                               [](StateContext& ctx, const Event*)
                               {
                                   const CharacterInput* in = InputOf(ctx);
                                   return in == nullptr || !in->sprint || in->MoveMagnitude() <= 0.0f;
                               });
        registry.RegisterGuard("WantTurnLeft",
                               [](StateContext& ctx, const Event*)
                               {
                                   const CharacterInput* in = InputOf(ctx);
                                   return in != nullptr && in->turn < -0.5f && in->MoveMagnitude() <= 0.0f;
                               });
        registry.RegisterGuard("WantTurnRight",
                               [](StateContext& ctx, const Event*)
                               {
                                   const CharacterInput* in = InputOf(ctx);
                                   return in != nullptr && in->turn > 0.5f && in->MoveMagnitude() <= 0.0f;
                               });
        registry.RegisterGuard("Alive",
                               [](StateContext& ctx, const Event*)
                               {
                                   const AnimParams* anim = AnimOf(ctx);
                                   return anim == nullptr || !anim->dead;
                               });
        // ---- action guards: the event carries the montage name, the profile says which slot ------------
        registry.RegisterAction("RememberMontage",
                                [](StateContext& ctx)
                                {
                                    if (AnimParams* anim = AnimOf(ctx))
                                    {
                                        anim->pendingMontage = EventMontage(ctx.CurrentEvent());
                                    }
                                });
        registry.RegisterGuard("ActionUpperBody",
                               [](StateContext& ctx, const Event* e) { return ActionTargetsSlot(ctx, e, kUpperBodySlot); });
        registry.RegisterGuard("ActionFullBody",
                               [](StateContext& ctx, const Event* e) { return ActionTargetsSlot(ctx, e, kFullBodySlot); });
        registry.RegisterGuard("ActionOutranksUpper",
                               [](StateContext& ctx, const Event* e)
                               { return ActionTargetsSlot(ctx, e, kUpperBodySlot) && ActionOutranks(ctx, e); });
        registry.RegisterGuard("ActionOutranksFull",
                               [](StateContext& ctx, const Event* e)
                               { return ActionTargetsSlot(ctx, e, kFullBodySlot) && ActionOutranks(ctx, e); });
    }

    std::shared_ptr<StateMachineDefinition> CharacterController::BuildDefaultDefinition(
            const StateTypeRegistry& registry, std::string& outError)
    {
        auto def = std::make_shared<StateMachineDefinition>();
        def->Name = "character";

        auto addState = [&](const char* type, const char* name, StateId parent, const ParamBag& params = ParamBag{})
        {
            std::unique_ptr<SMState> state = registry.CreateState(type, name, params);
            return state ? def->AddState(std::move(state), parent) : kInvalidStateId;
        };
        auto loco = [](const char* select, const char* row)
        {
            ParamBag p;
            p.Set("locoSelect", std::string(select));
            p.Set("row", std::string(row));
            return p;
        };
        auto select = [](const char* select, bool keepMoving = false, bool dead = false)
        {
            ParamBag p;
            p.Set("locoSelect", std::string(select));
            p.Set("restart", true);
            p.Set("keepMoving", keepMoving);
            p.Set("dead", dead);
            return p;
        };
        auto slot = [](const char* name)
        {
            ParamBag p;
            p.Set("slot", std::string(name));
            return p;
        };
        auto weapon = [](float alpha)
        {
            ParamBag p;
            p.Set("alpha", alpha);
            return p;
        };

        const StateId root = addState("Parallel", "Root", kInvalidStateId);
        const StateId locomotion = addState("Single", "Locomotion", root);
        const StateId stand = addState("LocomotionState", "Stand", locomotion, loco("stand", "stand"));
        const StateId crouch = addState("LocomotionState", "Crouch", locomotion, loco("crouch", "crouch"));
        const StateId sprint = addState("Single", "Sprint", locomotion);
        const StateId sprintEnter = addState("SelectState", "SprintEnter", sprint, select("sprint_enter", true));
        const StateId sprintLoop = addState("LocomotionState", "SprintLoop", sprint, loco("sprint_loop", "sprint"));
        const StateId sprintExit = addState("SelectState", "SprintExit", sprint, select("sprint_exit"));
        const StateId turn = addState("Single", "Turn", locomotion);
        const StateId turnLeft = addState("SelectState", "TurnLeft", turn, select("turn90_l"));
        const StateId turnRight = addState("SelectState", "TurnRight", turn, select("turn90_r"));
        const StateId airborne = addState("Single", "Airborne", locomotion);
        const StateId airStart = addState("SelectState", "JumpStart", airborne, select("jump_start", true));
        const StateId airLoop = addState("SelectState", "JumpLoop", airborne, select("jump_loop", true));
        const StateId airLand = addState("SelectState", "JumpLand", airborne, select("jump_land"));
        const StateId dead = addState("SelectState", "Dead", locomotion, select("death", false, true));
        const StateId action = addState("Single", "Action", root);
        const StateId free = addState("Leaf", "Free", action);
        const StateId upperBusy = addState("SlotBusyState", "UpperBodyBusy", action, slot(kUpperBodySlot));
        const StateId fullBusy = addState("SlotBusyState", "FullBodyBusy", action, slot(kFullBodySlot));
        const StateId weaponRegion = addState("Single", "Weapon", root);
        const StateId unarmed = addState("WeaponState", "Unarmed", weaponRegion, weapon(0.0f));
        const StateId pistol = addState("WeaponState", "Pistol", weaponRegion, weapon(1.0f));
        for (StateId id : {root, locomotion, stand, crouch, sprint, sprintEnter, sprintLoop, sprintExit, turn, turnLeft,
                           turnRight, airborne, airStart, airLoop, airLand, dead, action, free, upperBusy, fullBusy,
                           weaponRegion, unarmed, pistol})
        {
            if (id == kInvalidStateId)
            {
                outError = "character state types are not registered";
                return nullptr;
            }
        }
        def->SetRoot(root);
        static_cast<SingleState*>(def->GetState(locomotion))->SetInitialChild(stand);
        static_cast<SingleState*>(def->GetState(sprint))->SetInitialChild(sprintEnter);
        static_cast<SingleState*>(def->GetState(turn))->SetInitialChild(turnLeft);
        static_cast<SingleState*>(def->GetState(airborne))->SetInitialChild(airStart);
        static_cast<SingleState*>(def->GetState(action))->SetInitialChild(free);
        static_cast<SingleState*>(def->GetState(weaponRegion))->SetInitialChild(unarmed);

        const EventId evAction = def->RegisterEvent("Action");
        const EventId evToggleWeapon = def->RegisterEvent("ToggleWeapon");
        const EventId evDie = def->RegisterEvent("Die");
        def->RegisterEvent("Notify"); // montage notifies, for guards of future transitions

        auto add = [&](StateId from, StateId to, const char* guard, EventId trigger = kInvalidEventId, int priority = 0,
                       const char* action = nullptr)
        {
            Transition t;
            t.source = from;
            t.target = to;
            t.trigger = trigger;
            t.priority = priority;
            if (guard != nullptr)
            {
                t.guardName = guard;
                t.guard = registry.CreateGuard(guard);
            }
            if (action != nullptr)
            {
                t.actionName = action;
                t.action = registry.CreateAction(action);
            }
            def->AddTransition(std::move(t));
        };

        // Locomotion: rings and one-shots
        add(stand, crouch, "WantCrouch");
        add(crouch, stand, "WantStand");
        add(stand, sprintEnter, "WantSprint");
        add(sprintEnter, sprintLoop, nullptr); // completion
        add(sprintLoop, sprintExit, "StopSprint");
        add(sprintExit, stand, nullptr);
        add(stand, turnLeft, "WantTurnLeft");
        add(stand, turnRight, "WantTurnRight");
        add(turnLeft, stand, nullptr);
        add(turnRight, stand, nullptr);
        // Airborne is a motion fact, not an event: any locomotion state leaves the ground the same way.
        add(locomotion, airStart, "Airborne", kInvalidEventId, 10);
        add(airStart, airLoop, nullptr);
        add(airStart, airLand, "Grounded", kInvalidEventId, 5); // touched down before the start clip ended
        add(airLoop, airLand, "Grounded");
        add(airLand, stand, nullptr);
        // Death is terminal (no outgoing transitions).
        add(locomotion, dead, "Alive", evDie, 20);

        // Action: three states no matter how many montages; priorities decide interruptions.
        add(free, upperBusy, "ActionUpperBody", evAction, 10, "RememberMontage");
        add(free, fullBusy, "ActionFullBody", evAction, 10, "RememberMontage");
        add(upperBusy, free, nullptr);
        add(fullBusy, free, nullptr);
        add(upperBusy, upperBusy, "ActionOutranksUpper", evAction, 10, "RememberMontage");
        add(fullBusy, fullBusy, "ActionOutranksFull", evAction, 10, "RememberMontage");
        add(upperBusy, fullBusy, "ActionOutranksFull", evAction, 10, "RememberMontage");
        add(fullBusy, upperBusy, "ActionOutranksUpper", evAction, 10, "RememberMontage");

        // Weapon
        add(unarmed, pistol, nullptr, evToggleWeapon, 5);
        add(pistol, unarmed, nullptr, evToggleWeapon, 5);

        std::vector<std::string> errors;
        if (!def->Finalize(&errors))
        {
            outError = errors.front();
            return nullptr;
        }
        return def;
    }

    void CharacterController::BindStateData(const StateMachineDefinition& definition, CharacterStateData& data)
    {
        data.airborneState = definition.FindState("Airborne");
        data.deadState = definition.FindState("Dead");
    }

    bool CharacterController::Init(const std::string& assetsDir, std::string& outError)
    {
        mAssetsDir = assetsDir;
        RegisterCharacterTypes(mRegistry);

        const std::string path = (std::filesystem::path(assetsDir) / kDefinitionRelPath).generic_string();
        std::string error;
        if (std::filesystem::exists(path))
        {
            mDefinition = StateMachineSerializer::DeserializeDefinition(path, mRegistry, mDoc, error);
            if (mDefinition)
            {
                mSource = path;
            }
            else
            {
                fmt::print(stderr,
                           "CharacterController: '{}' not loaded ({}), using the built-in definition\n",
                           path,
                           error);
            }
        }
        if (!mDefinition)
        {
            mDefinition = BuildDefaultDefinition(mRegistry, error);
            if (!mDefinition)
            {
                outError = error;
                return false;
            }
            mSource = "(built-in)";
            mDoc = "Controllable character: Locomotion {Stand, Crouch, Sprint, Turn, Airborne, Dead} || Action {Free, "
                   "UpperBodyBusy, FullBodyBusy} || Weapon {Unarmed, Pistol}. Montages and graph inputs by name; "
                   "model data in <Stem>.profile.json.";
        }
        return true;
    }

    bool CharacterController::SaveDefinition(const std::string& path, std::string& outError) const
    {
        if (!mDefinition)
        {
            outError = "no definition";
            return false;
        }
        return StateMachineSerializer::SerializeDefinition(*mDefinition, mDoc, path, outError);
    }

    bool CharacterController::Attach(std::shared_ptr<ModelInstance> instance, std::string& outError)
    {
        if (!mDefinition)
        {
            outError = "controller is not initialised";
            return false;
        }
        if (!instance || !instance->GetModel())
        {
            outError = "instance has no model";
            return false;
        }
        const Model& model = *instance->GetModel();

        // profile (cached per path) ----------------------------------------------------------------------
        const std::string profilePath = CharacterProfile::PathForModel(model);
        std::shared_ptr<CharacterProfile> profile;
        if (auto it = mProfileCache.find(profilePath); it != mProfileCache.end())
        {
            profile = it->second;
        }
        else
        {
            profile = std::make_shared<CharacterProfile>();
            std::string error;
            if (!CharacterProfile::Load(profilePath, *profile, error))
            {
                outError = fmt::format("no character profile for '{}': {}", model.GetModelFileName(), error);
                return false;
            }
            mProfileCache[profilePath] = profile;
        }
        // graph (cached per path) ------------------------------------------------------------------------
        std::shared_ptr<AnimGraphAsset> graph;
        if (auto it = mGraphCache.find(profile->graphPath); it != mGraphCache.end())
        {
            graph = it->second;
        }
        else
        {
            std::string error;
            graph = AnimGraphAsset::LoadFromFile(profile->graphPath, error);
            if (!graph)
            {
                outError = fmt::format("could not load '{}': {}", profile->graphPath, error);
                return false;
            }
            mGraphCache[profile->graphPath] = graph;
        }

        Detach();
        mInstance = std::move(instance);
        mProfile = profile;
        mGraphAsset = graph;
        mPreviousAsset = mInstance->GetGraphAsset();
        mInstance->SetGraphAsset(mGraphAsset);
        mNodes = GraphNodes{};
        mInput = CharacterInput{};
        mMotion = MotionState{};
        mFeedback = GraphFeedback{};
        mAnim = AnimParams{};
        mEasedSpeed = 0.0f;
        mEasedDirectionDeg = 0.0f;
        mEasedAlpha = 0.0f;
        mAppliedLocoSelect.clear();
        mLog.clear();
        mNotifies.clear();

        const InstanceSettings settings = mInstance->GetInstanceSettings();
        mMotion.position = settings.mWorldPosition;
        mMotion.yawDeg = settings.mWorldRotation.y;

        mManager = std::make_unique<SMManager>(mDefinition);
        CharacterStateData data;
        data.input = &mInput;
        data.motion = &mMotion;
        data.graph = &mFeedback;
        data.profile = mProfile.get();
        data.anim = &mAnim;
        BindStateData(*mDefinition, data);
        mManager->SetUserData(data);
        mManager->OnTransition = [this](const TransitionRecord& r)
        {
            const StateMachineDefinition& def = mManager->Definition();
            const SMState* from = def.GetState(r.from);
            const SMState* to = def.GetState(r.to);
            PushLog(fmt::format("{} -> {}", from != nullptr ? from->GetName() : "(start)", to != nullptr ? to->GetName() : "?"));
        };
        if (!mManager->Start())
        {
            outError = "definition is not finalized";
            mManager.reset();
            mInstance->SetGraphAsset(mPreviousAsset);
            mInstance.reset();
            return false;
        }
        return true;
    }

    void CharacterController::Detach()
    {
        if (mManager)
        {
            mManager->Stop();
            mManager.reset();
        }
        if (mNodes.graph != nullptr)
        {
            mNodes.graph->OnNotify = nullptr;
        }
        mNodes = GraphNodes{};
        if (mInstance)
        {
            mInstance->SetGraphAsset(mPreviousAsset);
            mInstance.reset();
        }
        mPreviousAsset = nullptr;
        mGraphAsset = nullptr;
        mProfile = nullptr;
    }

    void CharacterController::SetInput(const CharacterInput& input)
    {
        mInput = input;
    }

    void CharacterController::Jump()
    {
        if (!mManager || !mProfile || mAnim.dead || !mMotion.grounded || mMotion.movementLocked)
        {
            return;
        }
        mMotion.verticalVelocity = mProfile->jumpSpeed;
        mMotion.grounded = false;
        mMotion.airborne = true;
    }

    bool CharacterController::RequestAction(const std::string& montage)
    {
        if (!mManager || !mProfile)
        {
            return false;
        }
        if (mProfile->FindMontage(montage) == nullptr)
        {
            PushLog(fmt::format("action '{}' is not in the profile", montage));
            return false;
        }
        Event e;
        e.id = mDefinition->FindEvent("Action");
        e.payload = montage;
        mManager->DispatchEvent(e);
        return true;
    }

    void CharacterController::ToggleWeapon()
    {
        if (mManager)
        {
            mManager->DispatchEvent("ToggleWeapon");
        }
    }

    void CharacterController::Die()
    {
        if (mManager)
        {
            mManager->DispatchEvent("Die");
        }
    }

    // Transitions and warnings go to the console; the deque only backs the --controller verification.
    void CharacterController::PushLog(std::string line)
    {
        fmt::print("character: {}\n", line);
        mLog.push_back(std::move(line));
        while (mLog.size() > 24)
        {
            mLog.pop_front();
        }
    }

    // Gameplay movement, owned here: the state machine only tells us how fast it wants to go.
    void CharacterController::Move(float deltaTime)
    {
        if (!mProfile || !mInstance)
        {
            return;
        }
        InstanceSettings s = mInstance->GetInstanceSettings();
        mMotion.position = s.mWorldPosition;
        mMotion.yawDeg = s.mWorldRotation.y;
        mMotion.movementLocked = mAnim.movementLock;

        const bool canMove = !mAnim.dead && !mMotion.movementLocked;
        if (canMove && mInput.turn != 0.0f)
        {
            mMotion.yawDeg -= mInput.turn * mProfile->turnDegPerSec * deltaTime;
        }
        float speed = 0.0f;
        glm::vec3 velocity(0.0f);
        const float magnitude = mInput.MoveMagnitude();
        if (canMove && magnitude > 0.0f && mAnim.moveSpeed > 0.0f)
        {
            speed = mAnim.moveSpeed;
            // local move direction (x right, z forward) rotated by the facing yaw; the model faces +Z at yaw 0
            const glm::vec3 local(mInput.moveX / magnitude, 0.0f, mInput.moveY / magnitude);
            const float yaw = glm::radians(mMotion.yawDeg);
            const glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
            const glm::vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));
            velocity = (forward * local.z + right * local.x) * speed;
        }
        mMotion.speed = speed;

        // vertical: a simple floor at y = 0
        if (mMotion.airborne)
        {
            mMotion.verticalVelocity -= mProfile->gravity * deltaTime;
            velocity.y = mMotion.verticalVelocity;
        }
        mMotion.position += velocity * deltaTime;
        if (mMotion.airborne && mMotion.position.y <= 0.0f && mMotion.verticalVelocity <= 0.0f)
        {
            mMotion.position.y = 0.0f;
            mMotion.verticalVelocity = 0.0f;
            mMotion.airborne = false;
            mMotion.grounded = true;
        }

        s.mWorldPosition = mMotion.position;
        s.mWorldRotation.y = mMotion.yawDeg;
        mInstance->SetInstanceSettings(s);
    }

    CharacterController::GraphNodes* CharacterController::ResolveNodes(ModelAndInstanceData& scene)
    {
        if (!mInstance || !mGraphAsset)
        {
            return nullptr;
        }
        const EvalMode mode = AnimationSystem::EffectiveMode(*mInstance->GetModel(), scene.miEvalMode);
        AnimGraphInstance* graph = mInstance->EnsureGraph(mode, scene.miTrackGlobalTimeSec);
        if (graph == nullptr)
        {
            return nullptr;
        }
        if (graph != mNodes.graph || mGraphAsset->Revision != mNodes.revision || graph->FindNode(kLocoSelectNodeName) != mNodes.locoSelect)
        {
            mNodes.graph = graph;
            mNodes.revision = mGraphAsset->Revision;
            mNodes.locoSelect = graph->FindNode<CrossfadeSelect>(kLocoSelectNodeName);
            mNodes.stand = graph->FindNode<BlendSpace2D>(kStandNodeName);
            mNodes.crouch = graph->FindNode<BlendSpace2D>(kCrouchNodeName);
            mNodes.aim = graph->FindNode<BlendSpace1D>(kAimNodeName);
            mNodes.upperBodyLayer = graph->FindNode<LayeredBlend>(kUpperBodyLayerNodeName);
            mNodes.upperBodySlot = graph->FindSlot(kUpperBodySlot);
            mNodes.fullBodySlot = graph->FindSlot(kFullBodySlot);
            mAppliedLocoSelect.clear();
            if (mNodes.locoSelect != nullptr)
            {
                mNodes.locoSelect->BlendTimeSec = mProfile ? mProfile->locoBlendSec : 0.2f;
            }
            graph->OnNotify = [this](std::string_view slot, std::string_view notify)
            {
                fmt::print("character: notify {}:{}\n", slot, notify);
                mNotifies.push_back(fmt::format("{}:{}", slot, notify));
                while (mNotifies.size() > 16)
                {
                    mNotifies.pop_front();
                }
                if (mManager && mDefinition)
                {
                    Event e;
                    e.id = mDefinition->FindEvent("Notify");
                    e.payload = std::string(notify);
                    if (e.id != kInvalidEventId)
                    {
                        mManager->DispatchEvent(e);
                    }
                }
            };
        }
        return &mNodes;
    }

    void CharacterController::ReadFeedback(ModelAndInstanceData& scene)
    {
        GraphNodes* nodes = ResolveNodes(scene);
        mFeedback = GraphFeedback{};
        if (nodes == nullptr)
        {
            return;
        }
        if (nodes->locoSelect != nullptr)
        {
            mFeedback.locoDone = nodes->locoSelect->IsActiveDone();
            mFeedback.locoFading = nodes->locoSelect->IsFading();
        }
        mFeedback.upperBodyIdle = nodes->upperBodySlot == nullptr || nodes->upperBodySlot->IsIdle();
        mFeedback.fullBodyIdle = nodes->fullBodySlot == nullptr || nodes->fullBodySlot->IsIdle();
    }

    void CharacterController::Update(float deltaTime, ModelAndInstanceData& scene)
    {
        if (!mManager || !mInstance)
        {
            return;
        }
        // The instance may have been deleted from the scene behind our back.
        if (std::find(scene.miModelInstances.begin(), scene.miModelInstances.end(), mInstance) ==
            scene.miModelInstances.end())
        {
            Detach();
            return;
        }
        mLastDeltaTime = deltaTime;
        Move(deltaTime);
        ReadFeedback(scene);
        mManager->Update(deltaTime);
        ApplyVisuals(scene);
    }

    void CharacterController::ApplyVisuals(ModelAndInstanceData& scene)
    {
        GraphNodes* nodes = ResolveNodes(scene);
        if (nodes == nullptr || !mProfile)
        {
            return;
        }
        const float dt = std::max(0.0f, mLastDeltaTime);

        // locomotion rings: eased speed and direction
        const float speedStep = mProfile->speedBlendRatePerSec * dt;
        mEasedSpeed += std::clamp(mMotion.speed - mEasedSpeed, -speedStep, speedStep);
        const float magnitude = mInput.MoveMagnitude();
        if (magnitude > 0.0f)
        {
            const float target = glm::degrees(std::atan2(mInput.moveX, mInput.moveY)); // 0 = forward, 90 = right
            float delta = std::fmod(target - mEasedDirectionDeg + 540.0f, 360.0f) - 180.0f;
            const float dirStep = mProfile->directionBlendRateDegPerSec * dt;
            delta = std::clamp(delta, -dirStep, dirStep);
            mEasedDirectionDeg = std::fmod(mEasedDirectionDeg + delta + 360.0f, 360.0f);
        }
        if (nodes->stand != nullptr)
        {
            nodes->stand->InputX = mEasedDirectionDeg;
            nodes->stand->InputY = mEasedSpeed;
        }
        if (nodes->crouch != nullptr)
        {
            nodes->crouch->InputX = mEasedDirectionDeg;
            nodes->crouch->InputY = mEasedSpeed;
        }

        // selector
        if (nodes->locoSelect != nullptr && mAnim.locoSelect != mAppliedLocoSelect)
        {
            if (nodes->locoSelect->SetActiveByName(mAnim.locoSelect, mAnim.locoRestart))
            {
                mAppliedLocoSelect = mAnim.locoSelect;
            }
            else
            {
                PushLog(fmt::format("graph has no LocoSelect input '{}'", mAnim.locoSelect));
                mAppliedLocoSelect = mAnim.locoSelect; // report once
            }
        }

        // upper body layer + aim offset
        const float alphaTarget = mInput.aim || mAnim.upperBodyAlphaTarget > 0.0f ? mAnim.upperBodyAlphaTarget : 0.0f;
        const float alphaStep = mProfile->alphaBlendRatePerSec * dt;
        mEasedAlpha += std::clamp(alphaTarget - mEasedAlpha, -alphaStep, alphaStep);
        if (nodes->upperBodyLayer != nullptr)
        {
            // the slot's montages always show through; the aim pose only when armed
            const float slotWeight = nodes->upperBodySlot != nullptr ? nodes->upperBodySlot->MontageWeight() : 0.0f;
            nodes->upperBodyLayer->Alpha = std::max(mEasedAlpha, slotWeight);
        }
        if (nodes->aim != nullptr)
        {
            nodes->aim->Input = std::clamp(0.5f + 0.5f * mInput.aimPitch, 0.0f, 1.0f);
        }

        // montage requests
        for (const SlotRequest& r : mAnim.slotRequests)
        {
            const ProfileMontage* pm = mProfile->FindMontage(r.montage);
            if (pm == nullptr || !nodes->graph->PlayMontage(r.slot, pm->desc))
            {
                PushLog(fmt::format("montage '{}' could not be played into slot '{}'", r.montage, r.slot));
            }
        }
        mAnim.slotRequests.clear();
    }

    std::string CharacterController::ActiveStatesText() const
    {
        if (!mManager || !mManager->IsRunning())
        {
            return "(not running)";
        }
        std::string text;
        for (StateId leaf : mManager->ActiveLeaves())
        {
            if (!text.empty())
            {
                text += "  |  ";
            }
            text += mManager->Definition().StatePath(leaf);
        }
        return text;
    }
} // namespace RAnimation
