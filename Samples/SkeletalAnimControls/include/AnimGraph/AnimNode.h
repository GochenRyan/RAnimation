#pragma once
#include <string>
#include <string_view>
#include <vector>

#include <AnimGraph/Context.h>
#include <AnimGraph/PoseLink.h>

namespace RAnimation
{
    class AnimClip;

    // Node base class with the two-pass protocol: Update walks the whole graph (cheap: advances clocks,
    // computes weights), Evaluate produces poses only where needed (expensive, prunable).
    class AnimNode
    {
    public:
        virtual ~AnimNode() = default;

        virtual std::string_view TypeName() const = 0;
        virtual void Initialize_AnyThread(const InitContext&) {}
        virtual void CacheBones_AnyThread(const CacheBonesContext&) {}
        virtual void Update_AnyThread(const UpdateContext&) {}
        virtual void Evaluate_AnyThread(PoseContext&) {}

        // Pose input pins in pin order (used by the baker to resolve links and by traversals).
        virtual void GatherLinks(std::vector<PoseLink*>& outLinks) { (void)outLinks; }
        // Whether the GPU kernel can evaluate this node (pure sample + weighted blend). Marker and cache
        // nodes cannot.
        virtual bool CanEvaluateOnGpu() const { return false; }

        int NodeId = -1;
        float LastWeight = 0.0f;
        bool bForceCpu = false;
    };

    class Root : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "Root"; }
        void Initialize_AnyThread(const InitContext& C) override { Result.Initialize(C); }
        void CacheBones_AnyThread(const CacheBonesContext& C) override { Result.CacheBones(C); }
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        void GatherLinks(std::vector<PoseLink*>& outLinks) override { outLinks.push_back(&Result); }
        bool CanEvaluateOnGpu() const override { return true; }

        PoseLink Result;
    };

    // Clip player: Update advances Time (seconds), Evaluate samples. Looping is a node property, not a
    // property of the clip.
    class ClipPlayer : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "ClipPlayer"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        bool CanEvaluateOnGpu() const override { return true; }

        float DurationSec() const;
        // Keeps Time inside [0, duration]: wrap when looping, clamp otherwise.
        void NormalizeTime();

        int ClipIndex = 0;
        float PlayRate = 1.0f;
        bool bLooping = true;
        float Time = 0.0f; // seconds

        const AnimClip* Clip = nullptr; // resolved in Initialize from the owning model
    };

    // Two-way blend: nlerp between A and B by Alpha (0 = A, 1 = B).
    class TwoWayBlend : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "TwoWayBlend"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void CacheBones_AnyThread(const CacheBonesContext& C) override;
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        void GatherLinks(std::vector<PoseLink*>& outLinks) override;
        bool CanEvaluateOnGpu() const override { return true; }

        // Update always propagates, even at weight 0, so hidden branches keep advancing their clocks and
        // resume seamlessly; Evaluate and GPU term building prune below this threshold.
        static constexpr float kSmallestRelevantWeight = 0.0001f;

        PoseLink A;
        PoseLink B;
        float Alpha = 0.0f;
    };

    // 1D blend space: piecewise blend between samples ordered by Position, driven by Input.
    class BlendSpace1D : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "BlendSpace1D"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void CacheBones_AnyThread(const CacheBonesContext& C) override;
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        void GatherLinks(std::vector<PoseLink*>& outLinks) override;
        bool CanEvaluateOnGpu() const override { return true; }
        // The two samples bracketing Input and the blend fraction between them.
        void Resolve(int& OutLow, int& OutHigh, float& OutFrac) const;

        struct Sample
        {
            float Position = 0.0f;
            PoseLink Link;
        };

        std::vector<Sample> Samples; // ascending Position
        float Input = 0.0f;
    };

    // Cached pose: evaluated once per frame by GraphRunner (drained after Update); UseCachedPose reads it
    // by name. The two-pass protocol cannot be expressed on the GPU, so these stay on the CPU.
    class SaveCachedPose : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "SaveCachedPose"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void CacheBones_AnyThread(const CacheBonesContext& C) override { Source.CacheBones(C); }
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override; // copies the cache (evaluating it if needed)
        void GatherLinks(std::vector<PoseLink*>& outLinks) override { outLinks.push_back(&Source); }

        void InvalidateCache() { bCacheValid = false; }
        void EvaluateCache(AnimGraphInstance& Instance);

        PoseLink Source;
        std::string CacheName;
        std::vector<Transform> Cached;
        bool bCacheValid = false;
    };

    class UseCachedPose : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "UseCachedPose"; }
        void Evaluate_AnyThread(PoseContext& C) override;

        std::string CacheName;
        SaveCachedPose* Source = nullptr; // resolved by the baker by CacheName
    };
} // namespace RAnimation
