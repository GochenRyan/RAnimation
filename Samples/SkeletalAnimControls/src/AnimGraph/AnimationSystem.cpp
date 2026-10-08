#include <AnimGraph/AnimationSystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include <AnimGraph/AnimGraphInstance.h>
#include <Model/AnimClip.h>
#include <Model/ClipSampling.h>
#include <Model/InstanceSettings.h>
#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>

namespace RAnimation
{
    EvalMode AnimationSystem::EffectiveMode(const Model& model, EvalMode requested)
    {
        if (EvalModeUsesTrsTexture(requested) && !model.IsTrsTexReady())
        {
            return EvalMode::Cpu;
        }
        return requested;
    }

    int AnimationSystem::TrackBucket(float phase, int phases)
    {
        phases = std::max(1, phases);
        const float p = phase - std::floor(phase); // [0,1)
        return std::min(static_cast<int>(p * static_cast<float>(phases)), phases - 1);
    }

    int AnimationSystem::TrackIndexFor(const InstanceSettings& settings, int phases)
    {
        return static_cast<int>(settings.mAnimClipNr) * std::max(1, phases) + TrackBucket(settings.mAnimPhase, phases);
    }

    float AnimationSystem::TrackClipTimeSec(const AnimClip& clip, int bucket, int phases, float globalTimeSec)
    {
        const float duration = ClipDurationSeconds(clip);
        if (duration <= 0.0f)
        {
            return 0.0f;
        }
        phases = std::max(1, phases);
        // Tracks always loop: a frozen track would make every bucket collapse onto the same last row.
        const float t = std::fmod(globalTimeSec + static_cast<float>(bucket) / static_cast<float>(phases) * duration, duration);
        return t < 0.0f ? t + duration : t;
    }

    void AnimationSystem::Update(float deltaTime, ModelAndInstanceData& scene)
    {
        const auto t0 = std::chrono::steady_clock::now();
        mStats = Stats{};

        scene.miTrackGlobalTimeSec += deltaTime;
        const EvalMode requested = scene.miEvalMode;
        const int phases = std::max(1, scene.miTrackPhases);

        for (const std::shared_ptr<ModelInstance>& instance : scene.miModelInstances)
        {
            if (!instance)
            {
                continue;
            }
            std::shared_ptr<Model> model = instance->GetModel();
            if (!model || !model->HasAnimations() || model->GetAnimClips().empty())
            {
                continue;
            }

            const EvalMode mode = EffectiveMode(*model, requested);
            instance->SetEffectiveEvalMode(mode);

            if (mode == EvalMode::Tracks)
            {
                // No per-instance graph work: the drawn pose is the track's clip at the track's time.
                const InstanceSettings settings = instance->GetInstanceSettings();
                const int clip = std::min<int>(static_cast<int>(settings.mAnimClipNr), static_cast<int>(model->GetAnimClips().size()) - 1);
                const int bucket = TrackBucket(settings.mAnimPhase, phases);
                const float timeSec = TrackClipTimeSec(*model->GetAnimClips()[static_cast<size_t>(clip)], bucket, phases, scene.miTrackGlobalTimeSec);
                instance->SetTrackPoseSource(clip, timeSec);
                ++mStats.instancesTracked;
                continue;
            }

            AnimGraphInstance* graph = instance->EnsureGraph(mode, scene.miTrackGlobalTimeSec);
            if (graph == nullptr)
            {
                ++mStats.instancesFailed;
                continue;
            }
            graph->Tick(deltaTime);
            instance->MarkCpuPoseDirty();

            ++mStats.instancesTicked;
            mStats.updateVisits += graph->NumUpdateVisits;
            mStats.evaluateVisits += graph->NumEvaluateVisits;
            mStats.slotTransforms += graph->Work().slotData.size();
            mStats.terms += graph->Work().terms.size();
        }

        mStats.cpuMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();

        scene.miAnimStats.instancesTicked = mStats.instancesTicked;
        scene.miAnimStats.instancesTracked = mStats.instancesTracked;
        scene.miAnimStats.instancesFailed = mStats.instancesFailed;
        scene.miAnimStats.updateVisits = mStats.updateVisits;
        scene.miAnimStats.evaluateVisits = mStats.evaluateVisits;
        scene.miAnimStats.slotTransforms = mStats.slotTransforms;
        scene.miAnimStats.terms = mStats.terms;
        scene.miAnimStats.cpuMs = mStats.cpuMs;
    }
} // namespace RAnimation
