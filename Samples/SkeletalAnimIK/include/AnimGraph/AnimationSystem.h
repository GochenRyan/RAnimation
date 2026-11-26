#pragma once
#include <cstddef>
#include <cstdint>

#include <AnimGraph/EvalMode.h>

struct ModelAndInstanceData;
struct InstanceSettings;

namespace RAnimation
{
    class AnimClip;
    class Model;

    // Explicit per-frame animation stage (Application::MainLoop: UI -> AnimationSystem::Update -> camera ->
    // Renderer::Draw). GPU-free: advances each animated instance's graph, evaluates the CPU subgraph and
    // produces its Work; tier C only computes track indices. HybridEvalComputePass::Upload merely packs.
    class AnimationSystem
    {
    public:
        void Update(float deltaTime, ModelAndInstanceData& scene);

        // A model without a usable TRS texture can only run the CPU tier (also in C).
        static EvalMode EffectiveMode(const Model& model, EvalMode requested);

        // Tier C helpers (shared with the renderer so the drawn pose and the camera pose agree).
        static int TrackBucket(float phase, int phases);
        static int TrackIndexFor(const InstanceSettings& settings, int phases);
        static float TrackClipTimeSec(const AnimClip& clip, int bucket, int phases, float globalTimeSec);

        struct Stats
        {
            int instancesTicked = 0;
            int instancesTracked = 0;
            int instancesFailed = 0;
            int updateVisits = 0;
            int evaluateVisits = 0;
            size_t slotTransforms = 0;
            size_t terms = 0;
            float cpuMs = 0.0f;
        };
        const Stats& LastStats() const { return mStats; }

    private:
        Stats mStats;
    };
} // namespace RAnimation
