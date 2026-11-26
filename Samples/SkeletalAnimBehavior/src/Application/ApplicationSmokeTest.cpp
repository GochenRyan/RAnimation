// Application: command-line smoke test (scene import, tier selection, graph assignment, crowd spawn,
// frame-limited run with statistics). Split out of Application.cpp.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <AnimGraph/AnimGraphAsset.h>
#include <Agent/ScriptBrain.h>
#include <Application/Application.h>
#include <Model/UsdSceneExporter.h>

#include <deque>
#include <iterator>

using namespace RAnimation;

bool Application::ApplySmokeTestOptions(const SmokeTestOptions& options)
{
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    RRenderData& renderData = mRenderer->GetRenderData();

    if (!options.scenePath.empty())
    {
        ImportedScene imported;
        if (!ImportSceneFromUsd(options.scenePath, imported))
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: could not import scene '{}'\n", options.scenePath);
            return false;
        }
        renderData.rdCameraRig = imported.camera;
        const SceneEditor::ImportResult result = mSceneEditor.ImportScene(imported);
        fmt::print("smoke: imported {} instance(s), {} static layer(s) from '{}' (entity ids: {} new, {} reassigned; {} asset(s) failed)\n",
                   result.instances,
                   result.staticLayers,
                   options.scenePath,
                   result.newIds,
                   result.reassignedIds,
                   result.failedAssets);
    }

    if (!options.levelRoundTripPath.empty())
    {
        if (options.scenePath.empty())
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: --level-roundtrip needs --scene\n");
            return false;
        }
        mVerifyFailed = !runLevelRoundTrip(options.levelRoundTripPath) || mVerifyFailed;
    }

    if (!options.evalMode.empty())
    {
        const std::string m = options.evalMode;
        if (m == "cpu")
            scene.miEvalMode = EvalMode::Cpu;
        else if (m == "a")
            scene.miEvalMode = EvalMode::PerCharacter;
        else if (m == "b")
            scene.miEvalMode = EvalMode::PerNpc;
        else if (m == "c")
            scene.miEvalMode = EvalMode::Tracks;
        else
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: unknown eval mode '{}' (cpu|a|b|c)\n", m);
            return false;
        }
        fmt::print("smoke: eval mode = {}\n", EvalModeName(scene.miEvalMode));
    }

    if (!options.graphPath.empty())
    {
        std::string error;
        std::shared_ptr<AnimGraphAsset> asset = AnimGraphAsset::LoadFromFile(options.graphPath, error);
        if (!asset)
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: could not load graph '{}': {}\n", options.graphPath, error);
            return false;
        }
        for (const std::shared_ptr<ModelInstance>& instance : scene.miModelInstances)
        {
            instance->SetGraphAsset(asset);
        }
        fmt::print("smoke: assigned graph '{}' ({} nodes) to {} instance(s)\n", asset->Name, asset->RuntimeDesc.Nodes.size(), scene.miModelInstances.size());
    }

    if (options.crowd > 0 && !scene.miModelList.empty())
    {
        std::shared_ptr<Model> model = scene.miModelList.front();
        const int clipCount = static_cast<int>(model->GetAnimClips().size());
        const int maxInstances = static_cast<int>(renderData.rdResourceBudget.maxWorldMatrices);
        const int available = std::max(0, maxInstances - static_cast<int>(scene.miModelInstances.size()));
        const int wanted = std::min(options.crowd, available);
        const int cols = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(wanted))));
        std::vector<InstanceSettings> settings;
        settings.reserve(static_cast<size_t>(wanted));
        for (int i = 0; i < wanted; ++i)
        {
            InstanceSettings s;
            s.mWorldPosition = glm::vec3(static_cast<float>(i % cols - cols / 2) * 1.5f, 0.0f, static_cast<float>(i / cols) * 1.5f + 5.0f);
            s.mWorldRotation = glm::vec3(0.0f, static_cast<float>(std::rand() % 360 - 180), 0.0f);
            s.mAnimClipNr = clipCount > 0 ? static_cast<unsigned int>(std::rand() % clipCount) : 0u;
            s.mAnimPhase = static_cast<float>(std::rand() % 1000) / 1000.0f;
            settings.push_back(s);
        }
        mSceneEditor.AddInstances(model, settings, "Spawn Crowd");
        fmt::print("smoke: spawned {} crowd instance(s) (budget {})\n", wanted, maxInstances);
    }

    if (!options.saveStateMachinePath.empty())
    {
        std::string error;
        CharacterController writer;
        writer.InitShared(mCharacterSystem.Shared());
        if (!writer.SaveDefinition(options.saveStateMachinePath, error))
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: could not save the state machine: {}\n", error);
            return false;
        }
        fmt::print("smoke: wrote '{}'\n", options.saveStateMachinePath);
    }

    // Collision: initialised here (not in init) so --no-collision and the thread switches apply before any
    // body exists. Models without a collision profile simply do not collide.
    if (!options.noCollision)
    {
        CollisionThreadingPolicy threading;
        if (options.collisionThreads == "single")
            threading.mode = CollisionThreadingPolicy::Mode::Single;
        else if (options.collisionThreads == "pool")
            threading.mode = CollisionThreadingPolicy::Mode::Pool;
        else if (options.collisionThreads.empty() || options.collisionThreads == "auto")
            threading.mode = CollisionThreadingPolicy::Mode::Auto;
        else
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: unknown --collision-threads '{}' (auto|single|pool)\n", options.collisionThreads);
            return false;
        }
        if (options.collisionThreshold >= 0)
        {
            threading.joltThreshold = options.collisionThreshold;
        }
        std::string error;
        const uint32_t maxBodies = static_cast<uint32_t>(std::clamp<uint64_t>(renderData.rdResourceBudget.maxWorldMatrices, 64, 4096));
        if (!mCollisionSystem.Init(maxBodies, threading, error))
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: collision init failed: {}\n", error);
            return false;
        }
        mCollisionSystem.GetSettings().showAABB = options.collisionDebugDraw;
        mCollisionSystem.GetSettings().showShapes = options.collisionDebugDraw;
        fmt::print("smoke: collision on (max {} bodies, jolt threads {}, mode {}, threshold {})\n",
                   maxBodies,
                   mCollisionSystem.World().ThreadPoolSize(),
                   options.collisionThreads.empty() ? "auto" : options.collisionThreads,
                   threading.joltThreshold);
    }

    mCollisionBench = options.collisionBench;
    mCollisionBenchFrames = std::max(30, options.collisionBenchFrames);
    mCollisionBenchSizes.clear();
    {
        size_t start = 0;
        const std::string& list = options.collisionBenchSizes;
        while (!list.empty() && start <= list.size())
        {
            const size_t comma = list.find(',', start);
            const std::string tok = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!tok.empty())
            {
                mCollisionBenchSizes.push_back(std::max(1, std::atoi(tok.c_str())));
            }
            if (comma == std::string::npos)
            {
                break;
            }
            start = comma + 1;
        }
    }
    if (mCollisionBenchSizes.empty())
    {
        mCollisionBenchSizes = {1, 2, 4, 8, 16, 32, 64, 128, 256};
    }
    if (mCollisionBench && (options.noCollision || scene.miModelList.empty()))
    {
        fmt::print(stderr, fg(fmt::color::red), "smoke: --collision-bench needs collision enabled and a --scene with at least one model\n");
        return false;
    }

    mFrameLimit = options.frames;
    mVerifyAtEnd = options.verify;
    mFixedDeltaTime = options.fixedDeltaTime;

    if (options.showBehaviorEditor)
    {
        mUserInterface.ShowBehaviorTreeEditor(true);
    }
    if (options.viewMode)
    {
        mSceneEditor.SetMode(Editor::Mode::View);
    }
    if (options.selectEntity != 0)
    {
        for (size_t i = 0; i < scene.miModelInstances.size(); ++i)
        {
            if (scene.miModelInstances[i]->GetEntityId() == options.selectEntity)
            {
                scene.miSelectedInstance = static_cast<int>(i);
            }
        }
    }
    if (options.editorSmoke && !setupEditorSmoke())
    {
        fmt::print(stderr, fg(fmt::color::red), "smoke: --editor-smoke needs --scene behavior_test.level.usda\n");
        return false;
    }
    if (options.behaviorBench > 0)
    {
        if (scene.miModelList.empty())
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: --behavior-bench needs a --scene with a character model\n");
            return false;
        }
        spawnBehaviorBench(options.behaviorBench);
        mSceneEditor.SetMode(Editor::Mode::View);
        if (mFixedDeltaTime <= 0.0f)
            mFixedDeltaTime = 1.0f / 60.0f;
        if (mFrameLimit < 0)
            mFrameLimit = 600;
    }
    if (options.behavior)
    {
        if (options.scenePath.empty() || !setupBehaviorSmoke())
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: --behavior needs --scene behavior_test.level.usda\n");
            return false;
        }
        if (mFixedDeltaTime <= 0.0f)
        {
            mFixedDeltaTime = 1.0f / 60.0f;
        }
        if (mFrameLimit < 0)
        {
            mFrameLimit = 1800;
        }
        fmt::print("smoke: behaviour scene, dt {:.4f} s, {} frames\n", mFixedDeltaTime, mFrameLimit);
    }

    if (options.controller)
    {
        if (scene.miModelInstances.empty())
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: --controller needs an instance (use --scene)\n");
            return false;
        }
        // The first instance runs the scripted sequence through a ScriptBrain (the same interface a behaviour
        // tree uses); a level-configured agent on that instance gets its brain replaced.
        mCharacterSystem.SyncFromScene(scene);
        const std::shared_ptr<ModelInstance>& first = scene.miModelInstances.front();
        Agent* agent = mCharacterSystem.FindByInstance(first.get());
        if (agent != nullptr)
        {
            if (mCharacterSystem.Possessed() == agent->entity)
            {
                mCharacterSystem.Possess(kInvalidEntityId);
            }
            mCharacterSystem.SetBrain(*agent, std::make_unique<ScriptBrain>(ScriptBrain::ControllerScript()));
        }
        else
        {
            std::string error;
            agent = mCharacterSystem.AddAgent(first, std::make_unique<ScriptBrain>(ScriptBrain::ControllerScript()), error);
            if (agent == nullptr)
            {
                fmt::print(stderr, fg(fmt::color::red), "smoke: could not create the scripted agent: {}\n", error);
                return false;
            }
        }
        mScriptEntity = agent->entity;
        mControllerScript = true;
        // Edit mode pauses every agent (D17); the editor starts in Edit.
        mSceneEditor.SetMode(Editor::Mode::View);
        if (mFixedDeltaTime <= 0.0f)
        {
            mFixedDeltaTime = 1.0f / 60.0f;
        }
        if (mFrameLimit < 0)
        {
            mFrameLimit = 600;
        }
        fmt::print("smoke: character controller attached (definition: {}), scripted input, dt {:.4f} s, {} frames\n",
                   agent->controller->DefinitionSource(), mFixedDeltaTime, mFrameLimit);
    }
    return true;
}

Agent* Application::scriptAgent()
{
    return mCharacterSystem.Find(mScriptEntity);
}

// The scripted sequence itself is ScriptBrain::ControllerScript (ticked by CharacterSystem, frame N of the
// brain = frame N here). This only checks the gameplay side the state machine never owns.
void Application::checkControllerFrame(int frame)
{
    Agent* agent = scriptAgent();
    if (agent == nullptr)
    {
        fmt::print(fg(fmt::color::red), "smoke[controller] frame {}: the scripted agent is gone\n", frame);
        mVerifyFailed = true;
        return;
    }
    CharacterController& controller = *agent->controller;
    if (frame == 0)
    {
        mControllerStart = controller.Motion().position;
    }

    const MotionState& motion = controller.Motion();
    auto expectMotion = [&](bool ok, const char* what)
    {
        if (!ok)
        {
            fmt::print(fg(fmt::color::red), "smoke[controller] frame {}: motion check failed: {}\n", frame, what);
            mVerifyFailed = true;
        }
    };
    if (frame == 149)
    {
        const float travelled = glm::length(glm::vec2(motion.position.x, motion.position.z) - glm::vec2(mControllerStart.x, mControllerStart.z));
        expectMotion(motion.grounded && travelled > 0.3f, "walked forward on the ground before the jump");
    }
    if (frame == 30 && mCollisionSystem.IsInitialized() && mCollisionSystem.GetSettings().enabled)
    {
        // static-layer display instances never get a character body
        const ModelAndInstanceData& data = mSceneEditor.ModelData();
        const size_t instances = static_cast<size_t>(std::count_if(data.miModelInstances.begin(),
                                                                   data.miModelInstances.end(),
                                                                   [&data](const std::shared_ptr<ModelInstance>& i)
                                                                   { return !data.IsStaticLayerModel(i->GetModel().get()); }));
        expectMotion(mCollisionSystem.BodyCount() == instances,
                     "one collision body per instance (CollisionSystem::Sync picked up the whole scene)");
        // Guards the joint-matrix scale trap: the UAL bind pose carries a 100x scale in its 3x3, so a shape
        // offset pushed through the raw matrix lands metres away and the body's AABB turns into a tall column.
        for (const std::shared_ptr<ModelInstance>& instance : data.miModelInstances)
        {
            glm::vec3 mn, mx;
            if (!mCollisionSystem.GetInstanceBounds(instance.get(), mn, mx))
            {
                continue;
            }
            const glm::vec3 size = mx - mn;
            const glm::vec3 centre = (mn + mx) * 0.5f;
            const glm::vec3 position = instance->GetWorldPosition();
            const float horizontal = glm::length(glm::vec2(centre.x - position.x, centre.z - position.z));
            expectMotion(size.y > 1.2f && size.y < 2.6f && size.x < 2.5f && size.z < 2.5f && horizontal < 1.0f,
                         "collision body AABB is character sized and centred on its instance");
        }
        fmt::print("smoke[controller] frame {}: collision bodies {} / instances {}, {} debug lines\n",
                   frame,
                   mCollisionSystem.BodyCount(),
                   instances,
                   mSceneEditor.ModelData().miDebugLines.LineCount());
    }
    if (frame == 160)
    {
        expectMotion(motion.airborne && motion.position.y > 0.0f, "airborne shortly after Jump()");
    }
    if (frame == 300)
    {
        expectMotion(motion.grounded && motion.position.y == 0.0f, "back on the ground 2.5 s after the jump");
    }
    if (frame == 456)
    {
        expectMotion(motion.movementLocked, "the hit montage locks movement");
    }
    if (frame == 30 || frame == 90 || frame == 150 || frame == 300 || frame == 330 || frame == 390 || frame == 420 ||
        frame == 450 || frame == 540)
    {
        fmt::print("smoke[controller] frame {}: {}\n", frame, controller.ActiveStatesText());
    }
}

bool Application::runLevelRoundTrip(const std::string& outPath)
{
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    bool ok = true;
    const auto check = [&ok](bool condition, const std::string& what)
    {
        fmt::print(condition ? fmt::text_style{} : fg(fmt::color::red), "smoke[level] {} {}\n", condition ? "ok  " : "FAIL", what);
        ok = ok && condition;
    };
    const auto vec = [](const glm::vec3& v) { return fmt::format("({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z); };

    // what the static layers contributed (the P0 acceptance reads these numbers)
    for (const StaticLayer& layer : scene.miStaticLayers)
    {
        fmt::print("smoke[level] static layer '{}': display model {}, {} collider(s), {} zone(s), {} marker(s), {} warning(s)\n",
                   layer.path,
                   layer.model ? fmt::format("{} triangle(s)", layer.model->GetTriangleCount()) : std::string("none"),
                   layer.statics.colliders.size(),
                   layer.statics.zones.size(),
                   layer.statics.markers.size(),
                   layer.statics.warnings.size());
        if (layer.model)
        {
            // baked display geometry, one line per mesh: these are the numbers the viewport shows
            for (const RMesh& mesh : layer.model->GetMeshes())
            {
                glm::vec3 mn(std::numeric_limits<float>::max());
                glm::vec3 mx(std::numeric_limits<float>::lowest());
                for (const RVertex& v : mesh.vertices)
                {
                    mn = glm::min(mn, v.position);
                    mx = glm::max(mx, v.position);
                }
                fmt::print("smoke[level]   display mesh {} tris, bounds {} .. {}\n", mesh.indices.size() / 3, vec(mn), vec(mx));
            }
        }
        for (const StaticCollider& c : layer.statics.colliders)
        {
            fmt::print("smoke[level]   collider {} ({}) centre {} half {}\n",
                       c.name,
                       c.tag == StaticColliderTag::Boundary ? "boundary" : "obstacle",
                       vec(c.box.center),
                       vec(c.box.halfExtents));
        }
        for (const StaticZone& z : layer.statics.zones)
        {
            fmt::print("smoke[level]   zone {} centre {} half {}\n", z.name, vec(z.box.center), vec(z.box.halfExtents));
        }
        for (const StaticMarker& m : layer.statics.markers)
        {
            fmt::print("smoke[level]   {} {} route '{}' #{} at {} radius {:.2f}\n",
                       m.kind == StaticMarkerKind::Spawn ? "spawn" : "waypoint",
                       m.name,
                       m.route,
                       m.index,
                       vec(m.position),
                       m.radius);
        }
    }

    std::vector<std::shared_ptr<ModelInstance>> expected;
    std::set<EntityId> ids;
    bool unique = true;
    for (const std::shared_ptr<ModelInstance>& instance : scene.miModelInstances)
    {
        unique = unique && instance->GetEntityId() != kInvalidEntityId && ids.insert(instance->GetEntityId()).second;
        if (!scene.IsStaticLayerModel(instance->GetModel().get()))
        {
            expected.push_back(instance);
        }
    }
    check(unique, fmt::format("every instance has a unique entity id ({} instances)", scene.miModelInstances.size()));

    if (!ExportSceneToUsd(scene, mRenderer->GetRenderData().rdCameraRig, outPath))
    {
        check(false, "export to '" + outPath + "'");
        return false;
    }
    ImportedScene back;
    check(ImportSceneFromUsd(outPath, back), "re-import of the exported level");
    check(back.instances.size() == expected.size(),
          fmt::format("instance count survives ({} exported, static display instances excluded: {})", back.instances.size(), expected.size()));
    int idMismatch = 0;
    int agentMismatch = 0;
    for (size_t i = 0; i < std::min(back.instances.size(), expected.size()); ++i)
    {
        idMismatch += back.instances[i].entityId != expected[i]->GetEntityId() ? 1 : 0;
        agentMismatch += back.instances[i].agent != expected[i]->GetAgentConfig() ? 1 : 0;
    }
    check(idMismatch == 0, fmt::format("entity ids unchanged after export -> import ({} mismatches)", idMismatch));
    check(agentMismatch == 0, fmt::format("agent configs unchanged ({} mismatches)", agentMismatch));

    bool layersMatch = back.staticLayers.size() == scene.miStaticLayers.size();
    for (size_t i = 0; layersMatch && i < back.staticLayers.size(); ++i)
    {
        std::error_code ec;
        layersMatch = std::filesystem::equivalent(back.staticLayers[i].path, scene.miStaticLayers[i].path, ec) &&
                      back.staticLayers[i].statics.colliders.size() == scene.miStaticLayers[i].statics.colliders.size();
    }
    check(layersMatch, fmt::format("static layers written back as subLayers ({})", back.staticLayers.size()));

    if (!expected.empty())
    {
        const std::shared_ptr<ModelInstance> clone = mSceneEditor.CloneInstance(expected.front());
        const EntityId cloneId = clone->GetEntityId();
        check(cloneId != kInvalidEntityId && ids.count(cloneId) == 0, fmt::format("clone of entity {} gets the new id {}", expected.front()->GetEntityId(), cloneId));
        mSceneEditor.Undo();
        check(mSceneEditor.FindEntity(cloneId) == nullptr, "undo removes the clone");
    }
    fmt::print(ok ? fg(fmt::color::green) : fg(fmt::color::red), "smoke[level] round trip {}\n", ok ? "OK" : "FAILED");
    return ok;
}

void Application::verifyControllerLog()
{
    // Expected transition order (a subsequence of the log).
    // (the punch is still blending out when the crouch starts, so the Action region reports after it)
    const char* expected[] = {"Locomotion -> JumpStart", "JumpStart -> JumpLand", "JumpLand -> Stand", "Free -> UpperBodyBusy",
                              "Stand -> Crouch", "UpperBodyBusy -> Free", "Crouch -> Stand", "Unarmed -> Pistol",
                              "Free -> FullBodyBusy", "FullBodyBusy -> Free", "Locomotion -> Dead"};
    Agent* agent = scriptAgent();
    if (agent == nullptr)
    {
        fmt::print(fg(fmt::color::red), "smoke[controller] the scripted agent is gone\n");
        mVerifyFailed = true;
        return;
    }
    CharacterController& controller = *agent->controller;
    const std::deque<std::string>& log = controller.Log();
    size_t next = 0;
    for (const std::string& line : log)
    {
        if (next < std::size(expected) && line == expected[next])
        {
            ++next;
        }
    }
    fmt::print("smoke[controller] final: {}\n", controller.ActiveStatesText());
    for (const std::string& line : log)
    {
        fmt::print("smoke[controller]   {}\n", line);
    }
    for (const std::string& line : controller.Notifies())
    {
        fmt::print("smoke[controller]   notify {}\n", line);
    }
    const bool notified = !controller.Notifies().empty();
    if (!notified)
    {
        fmt::print(fg(fmt::color::red), "smoke[controller] no montage notify was reported (expected HitWindow from punch)\n");
    }
    const bool pass = next == std::size(expected) && notified;
    fmt::print(pass ? fg(fmt::color::green) : fg(fmt::color::red), "smoke[controller] transition sequence {} ({}/{} expected steps)\n",
               pass ? "OK" : "FAILED", next, std::size(expected));
    mVerifyFailed = mVerifyFailed || !pass;
}

void Application::runVerification()
{
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    if (scene.miModelInstances.empty())
    {
        fmt::print(stderr, fg(fmt::color::red), "verify: no instances\n");
        mVerifyFailed = true;
        return;
    }
    // Tolerances: 30 Hz resampling of 24 fps keys costs up to a few cm on fast clips (see TrsTextureBaker
    // --check); the CPU tier must agree to half-float precision only.
    const float positionTolerance = scene.miEvalMode == EvalMode::Cpu ? 0.005f : 0.06f;
    const int indices[] = {0, static_cast<int>(scene.miModelInstances.size()) / 2, static_cast<int>(scene.miModelInstances.size()) - 1};
    for (int index : indices)
    {
        Renderer::BoneVerifyResult result;
        if (!mRenderer->VerifyBoneMatrices(scene, index, result))
        {
            fmt::print(stderr, fg(fmt::color::red), "verify: FAILED ({})\n", result.info);
            mVerifyFailed = true;
            continue;
        }
        // Kernel correctness is judged against the CPU mirror of the exact GPU blend (tight tolerance);
        // the keyframe comparison is reported as the resampling cost of the chosen bake rate.
        constexpr float mirrorTolerance = 0.005f;
        const bool pass = result.hasMirror ? result.mirrorPositionError <= mirrorTolerance
                                           : result.maxPositionError <= positionTolerance;
        fmt::print(pass ? fmt::text_style{} : fg(fmt::color::red),
                   "verify[{}]: {} | vs GPU mirror {:.4f} m (tol {:.3f}{}) | vs CPU keyframes {:.4f} m (resampling cost) | max element {:.4f} -> {}\n",
                   EvalModeName(scene.miEvalMode),
                   result.info,
                   result.mirrorPositionError,
                   mirrorTolerance,
                   result.hasMirror ? "" : ", n/a",
                   result.maxPositionError,
                   result.maxElementError,
                   pass ? "PASS" : "FAIL");
        mVerifyFailed = mVerifyFailed || !pass;
    }
}

void Application::printFrameStats(const char* tag) const
{
    const RRenderData& rd = mRenderer->GetRenderData();
    const HybridEvalStats& hs = rd.rdHybridStats;
    const ModelAndInstanceData& scene = const_cast<SceneEditor&>(mSceneEditor).ModelData();
    const ModelAndInstanceData::AnimStats& as = scene.miAnimStats;
    fmt::print("smoke[{}] frame {} | mode {} | dispatches {} | groups {} | works {} | terms {} | slots {} | upload {} KB | "
               "cpu {:.3f} ms (ticked {}, tracked {}, failed {}) | crowd {} | frame {:.2f} ms\n",
               tag,
               mFramesRendered,
               EvalModeName(scene.miEvalMode),
               hs.dispatches,
               hs.threadGroups,
               hs.works,
               hs.terms,
               hs.slotTransforms,
               hs.uploadBytes / 1024,
               as.cpuMs,
               as.instancesTicked,
               as.instancesTracked,
               as.instancesFailed,
               hs.crowdInstances,
               rd.rdFrameTime);
    if (mCollisionSystem.IsInitialized())
    {
        const CollisionFrameTiming& c = mCollisionSystem.LastTiming();
        fmt::print("smoke[{}] collision: bodies {} | manifolds {} | pushes {} | {} | pose {:.0f} us | write {:.0f} us | update {:.0f} us | resolve {:.0f} us | lines {} ({:.0f} us)\n",
                   tag,
                   c.bodies,
                   c.manifolds,
                   c.pushes,
                   c.usedThreadPool ? "pool" : "single",
                   c.poseUs,
                   c.writeUs,
                   c.updateUs,
                   c.resolveUs,
                   scene.miDebugLines.LineCount(),
                   c.drawUs);
    }
}
