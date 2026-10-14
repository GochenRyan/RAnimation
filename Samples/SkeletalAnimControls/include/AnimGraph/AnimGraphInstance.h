#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <AnimGraph/EvalMode.h>
#include <AnimGraph/ExecPlan.h>
#include <AnimGraph/HybridWork.h>
#include <AnimGraph/MontagePlayback.h>
#include <AnimGraph/Pose.h>
#include <MemStack/MemStack.h>

namespace RAnimation
{
    class AnimNode;
    class AnimSlot;
    class Model;
    struct AnimGraphAsset;
    struct BakedGraph;
    class IPoseEvaluator;

    // One per ModelInstance: the node array baked from the shared asset (node state such as
    // ClipPlayer::Time is per instance), the execution plan, the evaluator and this frame's Work.
    class AnimGraphInstance
    {
    public:
        AnimGraphInstance();
        ~AnimGraphInstance();
        AnimGraphInstance(const AnimGraphInstance&) = delete;
        AnimGraphInstance& operator=(const AnimGraphInstance&) = delete;

        // Bakes asset for model under mode. Node clocks survive when the asset identity is unchanged
        // (parameter tweaks); otherwise every ClipPlayer is seeded from the shared track clock + the
        // instance phase so tiers A/B/C start in step.
        bool Rebuild(std::shared_ptr<AnimGraphAsset> asset,
                     const Model& model,
                     EvalMode mode,
                     float seedPhase,
                     float seedGlobalTimeSec,
                     std::string& outError);

        // One frame: advance time, run the CPU part, produce the Work record.
        void Tick(float deltaTime);

        // Full CPU evaluation of the graph (camera / focus / CPU tier). Uses the frame arena.
        bool EvaluateFullPose(std::vector<Transform>& outPose);

        AnimNode* GetNode(int id) const;
        // Node by its authored name (NodeDesc::Name of the runtime description); null if absent. Cheap but
        // linear: resolve once and keep the pointer while the asset revision is unchanged.
        AnimNode* FindNode(std::string_view name) const;
        template <typename T> T* FindNode(std::string_view name) const { return dynamic_cast<T*>(FindNode(name)); }

        // Montage requests go to the AnimSlot with that SlotName; false if no such slot, unknown clip or
        // a higher-priority montage owns the slot. Notifies crossed by any slot's montages are reported
        // through OnNotify (called from Tick, on the ticking thread).
        AnimSlot* FindSlot(std::string_view slotName) const;
        bool PlayMontage(std::string_view slotName, const MontageDesc& desc);
        bool StopMontage(std::string_view slotName, float blendOutSec);
        void DispatchNotify(std::string_view slotName, std::string_view notifyName);
        std::function<void(std::string_view slot, std::string_view notify)> OnNotify;
        BakedGraph* Baked() const { return mBaked.get(); }
        const ExecPlan& Plan() const { return mPlan; }
        const Model* GetModel() const { return mModel; }
        MemStack& Stack() { return mStack; }
        std::shared_ptr<AnimGraphAsset> Asset() const { return mAsset; }
        EvalMode Mode() const { return mMode; }
        uint32_t NodeCount() const { return mNodeCount; }

        bool Usable() const { return mError.empty(); }
        const std::string& Why() const { return mError; }
        std::string_view EvaluatorName() const;

        const HybridWorkCpu& Work() const { return mWork; }
        HybridWorkCpu& Work() { return mWork; }

        Pose AllocPose();

        // Per-frame statistics (reset in Tick)
        int NumUpdateVisits = 0;
        int NumEvaluateVisits = 0;
        float LastTickMs = 0.0f;

    private:
        std::shared_ptr<AnimGraphAsset> mAsset;
        std::unique_ptr<BakedGraph> mBaked;
        ExecPlan mPlan;
        std::unique_ptr<IPoseEvaluator> mEvaluator;
        HybridWorkCpu mWork;
        MemStack mStack;
        const Model* mModel = nullptr;
        EvalMode mMode = EvalMode::Cpu;
        uint32_t mNodeCount = 0;
        std::string mError;
    };
} // namespace RAnimation
