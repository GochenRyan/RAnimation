// Application: command-line smoke test (scene import, tier selection, graph assignment, crowd spawn,
// frame-limited run with statistics). Split out of Application.cpp.

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/color.h>

#include <AnimGraph/AnimGraphAsset.h>
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
        std::vector<ImportedSceneInstance> imported;
        CameraRig importedRig;
        if (!ImportSceneFromUsd(options.scenePath, imported, importedRig))
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: could not import scene '{}'\n", options.scenePath);
            return false;
        }
        renderData.rdCameraRig = importedRig;
        for (const ImportedSceneInstance& imp : imported)
        {
            std::shared_ptr<Model> model = mSceneEditor.GetModel(imp.assetPath);
            if (model == nullptr)
            {
                if (!mSceneEditor.AddModel(imp.assetPath))
                {
                    continue;
                }
                if (!scene.miModelInstances.empty())
                {
                    scene.miModelInstances.back()->SetInstanceSettings(imp.settings);
                }
            }
            else if (std::shared_ptr<ModelInstance> instance = mSceneEditor.AddInstance(model))
            {
                instance->SetInstanceSettings(imp.settings);
            }
        }
        fmt::print("smoke: imported {} instance(s) from '{}'\n", imported.size(), options.scenePath);
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
        if (!mCharacterController.SaveDefinition(options.saveStateMachinePath, error))
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: could not save the state machine: {}\n", error);
            return false;
        }
        fmt::print("smoke: wrote '{}'\n", options.saveStateMachinePath);
    }

    mFrameLimit = options.frames;
    mVerifyAtEnd = options.verify;
    mFixedDeltaTime = options.fixedDeltaTime;

    if (options.controller)
    {
        if (scene.miModelInstances.empty())
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: --controller needs an instance (use --scene)\n");
            return false;
        }
        std::string error;
        if (!mCharacterController.Attach(scene.miModelInstances.front(), error))
        {
            fmt::print(stderr, fg(fmt::color::red), "smoke: could not attach the character controller: {}\n", error);
            return false;
        }
        mControllerScript = true;
        if (mFixedDeltaTime <= 0.0f)
        {
            mFixedDeltaTime = 1.0f / 60.0f;
        }
        if (mFrameLimit < 0)
        {
            mFrameLimit = 600;
        }
        fmt::print("smoke: character controller attached (definition: {}), scripted input, dt {:.4f} s, {} frames\n",
                   mCharacterController.DefinitionSource(), mFixedDeltaTime, mFrameLimit);
    }
    return true;
}

// Frame-indexed script at 1/60 s. The same script runs on every model that has a profile; the expected
// transition sequence only names shared states, the timing assumes the UAL / animated_woman clip lengths.
//   0.5 s walk, 1.5 s jog, 2.5 s jump (Start -> Land on touchdown, Land -> Stand when the clip ends),
//   5.0 s punch (upper body slot), 5.5 s crouch, 6.5 s stand still, 7.0 s draw the pistol, 7.5 s hit
//   (full body slot, locks movement), 9.0 s die.
void Application::runControllerScript(int frame)
{
    CharacterInput input;
    if (frame >= 30 && frame < 390)
    {
        input.moveY = 1.0f;
    }
    if (frame >= 90 && frame < 390)
    {
        input.run = true;
    }
    if (frame >= 330 && frame < 390)
    {
        input.crouch = true;
    }
    mCharacterController.SetInput(input);
    if (frame == 150)
    {
        mCharacterController.Jump();
    }
    if (frame == 300)
    {
        mCharacterController.RequestAction("punch");
    }
    if (frame == 420)
    {
        mCharacterController.ToggleWeapon();
    }
    if (frame == 450)
    {
        mCharacterController.RequestAction("hit");
    }
    if (frame == 540)
    {
        mCharacterController.Die();
    }

    // Motion assertions: the state machine never owns gameplay state, so check the gameplay side too.
    const MotionState& motion = mCharacterController.Motion();
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
        expectMotion(motion.grounded && motion.position.z > 0.3f, "walked forward on the ground before the jump");
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
        fmt::print("smoke[controller] frame {}: {}\n", frame, mCharacterController.ActiveStatesText());
    }
}

void Application::verifyControllerLog()
{
    // Expected transition order (a subsequence of the log).
    // (the punch is still blending out when the crouch starts, so the Action region reports after it)
    const char* expected[] = {"Locomotion -> JumpStart", "JumpStart -> JumpLand", "JumpLand -> Stand", "Free -> UpperBodyBusy",
                              "Stand -> Crouch", "UpperBodyBusy -> Free", "Crouch -> Stand", "Unarmed -> Pistol",
                              "Free -> FullBodyBusy", "FullBodyBusy -> Free", "Locomotion -> Dead"};
    const std::deque<std::string>& log = mCharacterController.Log();
    size_t next = 0;
    for (const std::string& line : log)
    {
        if (next < std::size(expected) && line == expected[next])
        {
            ++next;
        }
    }
    fmt::print("smoke[controller] final: {}\n", mCharacterController.ActiveStatesText());
    for (const std::string& line : log)
    {
        fmt::print("smoke[controller]   {}\n", line);
    }
    for (const std::string& line : mCharacterController.Notifies())
    {
        fmt::print("smoke[controller]   notify {}\n", line);
    }
    const bool notified = !mCharacterController.Notifies().empty();
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
}
