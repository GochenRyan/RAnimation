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

    mFrameLimit = options.frames;
    mVerifyAtEnd = options.verify;
    return true;
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
