// Application: the agent / behaviour stage of a frame (perception -> decisions -> steering -> controllers),
// behaviour system setup and the frame-end requests (checkpoint save / load, §8.4).

#include <Application/Application.h>

#include <cmath>
#include <filesystem>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <Behavior/BehaviorTreeBrain.h>
#include <Model/UsdSceneExporter.h>

using namespace RAnimation;

namespace
{
    constexpr int kSaveVersion = 1;

    // "<dir>/<name>" (any of the two file names is accepted too)
    std::string SlotBase(const std::string& slotPath)
    {
        std::string base = slotPath;
        for (const char* suffix : {".behaviorsave.usda", ".level.usda"})
        {
            const std::string s(suffix);
            if (base.size() > s.size() && base.compare(base.size() - s.size(), s.size(), s) == 0)
            {
                base.resize(base.size() - s.size());
            }
        }
        return base;
    }
} // namespace

void Application::initBehavior()
{
    std::string error;
    if (!mBehaviorSystem.Init(ASSETS_SRC_DIR "/SkeletalAnimBehavior", error))
    {
        fmt::print(stderr, fg(fmt::color::yellow), "BehaviorSystem: {}\n", error);
    }
    mCharacterSystem.globalBlackboard = &mBehaviorSystem.Global();
    mCharacterSystem.SetBrainFactory([this](const AgentConfig& config, EntityId self, std::string& outError)
                                     { return mBehaviorSystem.CreateBrain(config, self, outError); });
    mBehaviorSystem.wake = [this](EntityId id) { mCharacterSystem.WakeNextFrame(id); };
    mUserInterface.SetBehaviorSystem(&mBehaviorSystem);
    mUserInterface.SetCheckpointHooks([this](const std::string& slot) { RequestCheckpointSave(slot); },
                                      [this](const std::string& slot) { RequestCheckpointLoad(slot); });
}

void Application::updateAgents(float deltaTime)
{
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    updateCharacterInput();
    mCharacterSystem.SyncFromScene(scene);

    // Perception inputs: last frame's merged contact events (one frame late, §2.2) and the static-layer
    // line-of-sight query; both empty when collision is off.
    PerceptionInputs perception;
    perception.staticLayers = &scene.miStaticLayers;
    if (mCollisionSystem.IsInitialized() && mCollisionSystem.GetSettings().enabled)
    {
        // a freshly imported level must block sight in its very first frame too
        mCollisionSystem.SyncStatics(scene);
        perception.contacts = mCollisionSystem.ContactEvents();
        perception.lineBlocked = [this](const glm::vec3& from, const glm::vec3& to) { return mCollisionSystem.LineBlocked(from, to); };
    }
    mCharacterSystem.Update(deltaTime, scene, mSceneEditor.IsEditMode(), perception);

    // §4.2: the follow cameras track the selected instance; a possession change selects the new player
    static EntityId lastPossessed = ~EntityId{0}; // first frame: remember, do not override a chosen selection
    if (lastPossessed == ~EntityId{0})
    {
        lastPossessed = mCharacterSystem.Possessed();
    }
    if (mCharacterSystem.Possessed() != lastPossessed)
    {
        lastPossessed = mCharacterSystem.Possessed();
        for (size_t i = 0; i < scene.miModelInstances.size(); ++i)
        {
            if (lastPossessed != kInvalidEntityId && scene.miModelInstances[i]->GetEntityId() == lastPossessed)
            {
                scene.miSelectedInstance = static_cast<int>(i);
            }
        }
    }
}

// §12.3 performance: N more agents on the test trees (half patrol, half watcher), 10 Hz, in an 8 m grid around
// the origin. MainLoop accumulates CharacterSystem::LastStats; printFrameStats reports the averages.
void Application::spawnBehaviorBench(int count)
{
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    std::shared_ptr<Model> model;
    for (const auto& instance : scene.miModelInstances)
    {
        if (instance->GetAgentConfig() && !scene.IsStaticLayerModel(instance->GetModel().get()))
        {
            model = instance->GetModel();
            break;
        }
    }
    if (!model)
    {
        model = scene.miModelList.front();
    }
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))));
    std::vector<InstanceSettings> settings;
    for (int i = 0; i < count; ++i)
    {
        InstanceSettings s;
        s.mWorldPosition = glm::vec3(-8.0f + 16.0f * static_cast<float>(i % cols) / std::max(1, cols - 1), 0.0f,
                                     -8.0f + 16.0f * static_cast<float>(i / cols) / std::max(1, cols - 1));
        s.mWorldRotation = glm::vec3(0.0f, static_cast<float>((i * 37) % 360 - 180), 0.0f);
        s.mAnimPhase = static_cast<float>(i % 10) / 10.0f;
        settings.push_back(s);
    }
    const size_t first = scene.miModelInstances.size();
    mSceneEditor.AddInstances(model, settings, "Behaviour bench");
    for (size_t i = first; i < scene.miModelInstances.size(); ++i)
    {
        AgentConfig config;
        config.tree = (i % 2 == 0) ? "behavior/patrol.behaviortree.usda" : "behavior/watcher.behaviortree.usda";
        config.tickHz = 10.0f;
        scene.miModelInstances[i]->SetAgentConfig(config);
    }
    mBehaviorBench = count;
    fmt::print("smoke: behaviour bench, {} extra agent(s) (patrol / watcher at 10 Hz)\n", count);
}

void Application::updateBehaviorFrameEnd()
{
    if (mBehaviorBench > 0)
    {
        const CharacterSystem::Stats& st = mCharacterSystem.LastStats();
        mBenchSums[0] += st.perceptionUs;
        mBenchSums[1] += st.decisionUs;
        mBenchSums[2] += st.executionUs;
        mBenchSums[3] += st.ticked;
        mBenchSums[4] += 1.0;
        if (mFrameLimit >= 0 && mFramesRendered + 1 >= mFrameLimit)
        {
            const double n = std::max(1.0, mBenchSums[4]);
            fmt::print("smoke[bench] {} agents ({} extra), {} frames: perception {:.1f} us, decisions {:.1f} us ({:.1f} ticks/frame), "
                       "steering + controllers {:.1f} us per frame; rays last frame {}\n",
                       mCharacterSystem.Agents().size(), mBehaviorBench, static_cast<int>(n), mBenchSums[0] / n, mBenchSums[1] / n,
                       mBenchSums[3] / n, mBenchSums[2] / n, st.rays);
        }
    }
    if (!mSaveRequest.empty())
    {
        std::string error;
        const std::string slot = mSaveRequest;
        mSaveRequest.clear();
        if (!saveCheckpoint(slot, error))
        {
            fmt::print(stderr, fg(fmt::color::red), "checkpoint save failed: {}\n", error);
        }
    }
    if (!mLoadRequest.empty())
    {
        std::string error;
        const std::string slot = mLoadRequest;
        mLoadRequest.clear();
        if (!loadCheckpoint(slot, error))
        {
            fmt::print(stderr, fg(fmt::color::red), "checkpoint load failed: {}\n", error);
        }
        else if (mAfterCheckpointLoad)
        {
            mAfterCheckpointLoad();
        }
    }
}

// §8.4: <slot>.level.usda (every instance: transforms, ids, agent configs, camera) + <slot>.behaviorsave.usda
// (world time, possession, Global / TreeShared blackboards, and the full runtime of the agents near the player).
bool Application::saveCheckpoint(const std::string& slotPath, std::string& outError)
{
    const std::string base = SlotBase(slotPath);
    const std::string levelPath = base + ".level.usda";
    const std::string savePath = base + ".behaviorsave.usda";
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(base).parent_path(), ec);
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    if (!ExportSceneToUsd(scene, mRenderer->GetRenderData().rdCameraRig, levelPath))
    {
        outError = "could not export " + levelPath;
        return false;
    }

    SaveNode root;
    root.name = "BehaviorSave";
    root.Set("saveVersion", kSaveVersion);
    root.Set("worldTime", mCharacterSystem.WorldTime());
    root.Set("possessed", static_cast<uint64_t>(mCharacterSystem.Possessed()));
    root.Set("saveRadius", saveRadius);
    root.Set("level", std::filesystem::path(levelPath).filename().generic_string());

    SaveBlackboardValues(mBehaviorSystem.Global(), root.Child("Global"), 0);
    SaveNode& shared = root.Child("TreeShared");
    for (const auto& [path, store] : mBehaviorSystem.TreeSharedStores())
    {
        const std::shared_ptr<const CompiledTree> tree = mBehaviorSystem.Cached(path);
        if (!tree || path.empty())
        {
            continue;
        }
        SaveNode& t = shared.Child(path);
        t.Set("tree", path);
        t.Set("contentHash", static_cast<uint64_t>(tree->contentHash));
        SaveBlackboardValues(*store, t, 0);
    }

    // D15: agents within saveRadius of the possessed one, plus the possessed one (everybody without a player)
    const Agent* player = mCharacterSystem.Find(mCharacterSystem.Possessed());
    const glm::vec3 center = player != nullptr && player->controller ? player->controller->Motion().position : glm::vec3(0.0f);
    SaveNode& agents = root.Child("Agents");
    int saved = 0;
    for (const auto& ptr : mCharacterSystem.Agents())
    {
        const Agent& agent = *ptr;
        if (!agent.controller)
        {
            continue;
        }
        const glm::vec3 p = agent.controller->Motion().position;
        const float distance = glm::length(glm::vec2(p.x - center.x, p.z - center.z));
        if (player != nullptr && &agent != player && distance > saveRadius)
        {
            continue;
        }
        SaveNode& a = agents.Child("E_" + std::to_string(agent.entity));
        a.Set("entity", static_cast<uint64_t>(agent.entity));
        agent.controller->SaveRuntime(a);
        SaveNode& brain = a.Child("Brain");
        const IDecisionMaker* own = agent.OwnBrain();
        brain.Set("kind", std::string(own->Kind()));
        brain.Set("accumulator", agent.schedule.accumulator);
        if (std::isfinite(agent.schedule.nextWakeTime))
        {
            brain.Set("nextWakeTime", agent.schedule.nextWakeTime);
        }
        brain.Set("nextRequestId", static_cast<uint64_t>(agent.requests.Peek()));
        if (&agent != player)
        {
            own->SaveRuntime(brain);
        }
        ++saved;
    }
    if (!WriteSaveTree(root, savePath, outError))
    {
        return false;
    }
    fmt::print("checkpoint: saved {} agent(s) of {} (radius {:.0f} m) at world time {:.3f} -> {}\n", saved, mCharacterSystem.Agents().size(),
               saveRadius, mCharacterSystem.WorldTime(), savePath);
    return true;
}

// §8.4 load procedure: level -> agents from their configs (every brain starts from the root) -> blackboards
// -> saved agents (motion, AnimParams, state machine without OnEnter, brain) -> wake them. The controllers'
// request tables start empty, so every request a node still waits for reads Cancelled (D15).
bool Application::loadCheckpoint(const std::string& slotPath, std::string& outError)
{
    const std::string base = SlotBase(slotPath);
    SaveNode root;
    if (!ReadSaveTree(base + ".behaviorsave.usda", root, outError))
    {
        return false;
    }
    const std::string level = (std::filesystem::path(base).parent_path() / root.GetOr<std::string>("level", "")).generic_string();
    ImportedScene imported;
    if (!ImportSceneFromUsd(level, imported))
    {
        outError = "could not import " + level;
        return false;
    }

    // 1-2: scene and agents (fresh brains, D15 for everybody not in the save)
    mCharacterSystem.Clear();
    mSceneEditor.ClearForLoad();
    mSceneEditor.ImportScene(imported);
    mRenderer->GetRenderData().rdCameraRig = imported.camera;
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    mCharacterSystem.SyncFromScene(scene);
    mCharacterSystem.SetWorldTime(root.GetOr<float>("worldTime", 0.0f));

    // 3: blackboards (a TreeShared store only when its tree did not change)
    if (const SaveNode* global = root.FindChild("Global"))
    {
        LoadBlackboardValues(mBehaviorSystem.Global(), *global, 0, kInvalidEntityId);
    }
    if (const SaveNode* shared = root.FindChild("TreeShared"))
    {
        for (const SaveNode& t : shared->children)
        {
            const std::string path = t.GetOr<std::string>("tree", t.name);
            const std::shared_ptr<const CompiledTree> tree = mBehaviorSystem.Cached(path);
            if (!tree || tree->contentHash != t.GetOr<uint64_t>("contentHash", 0))
            {
                fmt::print(stderr, fg(fmt::color::yellow), "checkpoint: TreeShared of '{}' skipped (tree changed)\n", path);
                continue;
            }
            LoadBlackboardValues(*mBehaviorSystem.TreeShared(path), t, 0, kInvalidEntityId);
        }
    }

    // possession as saved
    const EntityId possessed = static_cast<EntityId>(root.GetOr<uint64_t>("possessed", 0));
    if (mCharacterSystem.Possessed() != possessed)
    {
        mCharacterSystem.Possess(possessed);
    }

    // 4: saved agents
    int restored = 0;
    if (const SaveNode* agents = root.FindChild("Agents"))
    {
        for (const SaveNode& a : agents->children)
        {
            Agent* agent = mCharacterSystem.Find(static_cast<EntityId>(a.GetOr<uint64_t>("entity", 0)));
            if (agent == nullptr || !agent->controller)
            {
                continue;
            }
            std::string error;
            if (!agent->controller->LoadRuntime(a, error))
            {
                fmt::print(stderr, fg(fmt::color::yellow), "checkpoint: agent {} controller not restored: {}\n", agent->entity, error);
                continue;
            }
            if (const SaveNode* brain = a.FindChild("Brain"))
            {
                agent->requests.Restore(static_cast<RequestId>(brain->GetOr<uint64_t>("nextRequestId", 1)));
                IDecisionMaker* own = agent->OwnBrain();
                const std::string kind = brain->GetOr<std::string>("kind", "");
                if (agent->entity != possessed && kind == own->Kind())
                {
                    if (own->LoadRuntime(*brain, error))
                    {
                        agent->schedule.accumulator = brain->GetOr<float>("accumulator", 0.0f);
                    }
                    else
                    {
                        // keeps the fresh brain from step 2 (starts from the root)
                        fmt::print(stderr, fg(fmt::color::yellow), "checkpoint: agent {} brain starts over: {}\n", agent->entity, error);
                        own->Reset();
                    }
                }
            }
            ++restored;
        }
    }
    // 6: everybody ticks on the first frame after the load
    for (const auto& agent : mCharacterSystem.Agents())
    {
        mCharacterSystem.WakeNow(agent->entity);
    }
    fmt::print("checkpoint: loaded {} (world time {:.3f}), {} agent(s) restored, {} started over\n", base, mCharacterSystem.WorldTime(), restored,
               static_cast<int>(mCharacterSystem.Agents().size()) - restored);
    return true;
}
