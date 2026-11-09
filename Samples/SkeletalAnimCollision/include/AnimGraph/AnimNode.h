#pragma once
#include <string>
#include <string_view>
#include <vector>

#include <AnimGraph/Context.h>
#include <AnimGraph/CrossfadeTracker.h>
#include <AnimGraph/MontagePlayback.h>
#include <AnimGraph/PolarGrid.h>
#include <AnimGraph/PoseLink.h>

namespace RAnimation
{
    class AnimClip;
    class Model;

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

    // Animation slot: passes Base through until a montage is played into it; then blends the montage's
    // clip over the base with the montage's envelope (blend in / hold / blend out). Several montages may
    // overlap while one fades out and the next fades in. Notifies crossed during Update are reported to
    // the owning AnimGraphInstance (OnNotify). Where the slot sits in the graph decides what it covers:
    // inside a LayeredBlend layer = upper body, in front of Root = full body.
    class AnimSlot : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "AnimSlot"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void CacheBones_AnyThread(const CacheBonesContext& C) override { Base.CacheBones(C); }
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        void GatherLinks(std::vector<PoseLink*>& outLinks) override { outLinks.push_back(&Base); }
        bool CanEvaluateOnGpu() const override { return true; }

        // Starts desc (clipName resolved against the model; false if unknown). A playing montage of
        // higher priority rejects the request; otherwise it blends out while the new one blends in.
        bool Play(const MontageDesc& desc, const Model& model);
        void Stop(float blendOutSec);
        bool IsIdle() const { return Playing.empty(); }
        bool IsBlendingOut() const;
        const MontagePlayback* Current() const; // the most recent montage still playing, or null
        float MontageWeight() const;             // sum of the playing montages' weights (clamped to 1)

        PoseLink Base;
        std::string SlotName;
        std::vector<MontagePlayback> Playing;

    private:
        AnimGraphInstance* mInstance = nullptr;
        std::vector<std::string> mCrossed;
    };

    // 2D blend space on a polar grid: samples at (x = direction, y = speed), rows by y, columns by x with
    // wrap-around (ResolvePolarGrid). Up to four samples blend: bilinear between two rows and two columns.
    class BlendSpace2D : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "BlendSpace2D"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void CacheBones_AnyThread(const CacheBonesContext& C) override;
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        void GatherLinks(std::vector<PoseLink*>& outLinks) override;
        bool CanEvaluateOnGpu() const override { return true; }
        // The samples bracketing (InputX, InputY) and their weights; returns the count (0..4).
        int Resolve(GridWeight out[4]) const;

        struct Sample
        {
            GridSample Position;
            PoseLink Link;
        };

        std::vector<Sample> Samples;
        float InputX = 0.0f;      // e.g. direction in degrees
        float InputY = 0.0f;      // e.g. speed
        bool bWrapX = true;       // x is a ring of WrapPeriod
        float WrapPeriod = 360.0f;
    };

    // N-way selector with a time-driven crossfade: inputs In0..InN (optionally named), one of them active.
    // Update ramps the active input to 1 and the others to 0 over BlendTimeSec (CrossfadeTracker), still
    // updating hidden inputs at weight 0 so their clocks keep running. Evaluate / the GPU flatten take the
    // weighted sum of the inputs in play, renormalised. SetActive(index, restart) rewinds the ClipPlayers of
    // the newly active branch when asked; IsActiveDone() tells whether every non-looping ClipPlayer under
    // the active input has reached its end (the state machine's completion query).
    class CrossfadeSelect : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "CrossfadeSelect"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void CacheBones_AnyThread(const CacheBonesContext& C) override;
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        void GatherLinks(std::vector<PoseLink*>& outLinks) override;
        bool CanEvaluateOnGpu() const override { return true; }

        // Switch (by index or by authored input name; false if unknown). restart rewinds the branch's
        // non-looping and looping clips alike so one-shot clips (Jump_Start) play from the top.
        bool SetActive(int index, bool restart);
        bool SetActiveByName(std::string_view name, bool restart);
        int FindInput(std::string_view name) const;
        bool IsActiveDone() const;
        bool IsFading() const { return !mSettled; }
        const CrossfadeTracker& Tracker() const { return mTracker; }

        std::vector<PoseLink> Inputs;         // In0..InN
        std::vector<std::string> InputNames;  // optional, parallel to Inputs (ranim:inputNames)
        int ActiveIndex = 0;                  // authored / runtime parameter; Update follows changes
        float BlendTimeSec = 0.2f;

    private:
        static void RewindClips(AnimNode* node, int depth);
        static bool ClipsDone(const AnimNode* node, int depth, bool& sawOneShot);

        CrossfadeTracker mTracker;
        bool mSettled = true;
        bool mStarted = false;
    };

    // Layered blend with a bone mask: out[b] = lerp(Base[b], Layer[b], Alpha * Mask[b]) in local space. The
    // mask is 1 under MaskRoot (ramping in over BlendDepth generations), 0 elsewhere; it is built once from
    // the model's hierarchy in Initialize. Base is always fully updated, Layer with weight Alpha. On the GPU
    // the mask becomes a per-term row of per-bone factors (HybridTerm::maskRow), so this node flattens like
    // any other blend.
    class LayeredBlend : public AnimNode
    {
    public:
        std::string_view TypeName() const override { return "LayeredBlend"; }
        void Initialize_AnyThread(const InitContext& C) override;
        void CacheBones_AnyThread(const CacheBonesContext& C) override;
        void Update_AnyThread(const UpdateContext& C) override;
        void Evaluate_AnyThread(PoseContext& C) override;
        void GatherLinks(std::vector<PoseLink*>& outLinks) override;
        bool CanEvaluateOnGpu() const override { return true; }

        PoseLink Base;
        PoseLink Layer;
        float Alpha = 1.0f;       // layer weight, runtime parameter
        std::string MaskRoot;     // node name; the layer covers its subtree
        int BlendDepth = 0;       // generations under MaskRoot that ramp from 0 to 1
        std::vector<float> Mask;  // per node, built in Initialize (BuildBoneMask)
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
