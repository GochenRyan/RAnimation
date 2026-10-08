#include <AnimGraph/HybridEvaluator.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/GraphBaker.h>
#include <AnimGraph/GraphRunner.h>
#include <AnimGraph/HybridWork.h>
#include <MemStack/MemStack.h>
#include <Model/Model.h>

namespace RAnimation
{
    HybridEvaluator::HybridEvaluator(AnimGraphInstance& instance) : mInstance(instance)
    {
        if (mInstance.Baked() == nullptr)
        {
            mError = "graph is not baked";
            return;
        }
        // Static limits are known from the plan alone; term count is checked every frame (weights move).
        std::vector<HybridSample> probe;
        std::string err;
        if (!BuildHybridSamples(mInstance, probe, err))
        {
            mError = err;
            return;
        }
        NormalizeHybrid(probe);
        if (!CheckHybridLimits(mInstance.Plan(), probe, static_cast<int>(kMaxTermsPerWork), static_cast<int>(kMaxSlotsPerWork), err))
        {
            mError = err;
        }
    }

    void HybridEvaluator::Tick(float deltaTime, HybridWorkCpu& out)
    {
        out.Clear();
        out.nodeCount = mInstance.NodeCount();
        if (!Usable())
        {
            return;
        }

        // Pass 1 (whole graph): advance clocks and weights. Pass 1b: produce the cached poses.
        GraphRunner::Update(mInstance, deltaTime);
        GraphRunner::DrainCachedPoses(mInstance);

        // Pass 2 (CPU half only): evaluate every seam producer into its upload slot. Slots are allocated in
        // plan order so local slot s == plan.SlotSourceNodeIds[s].
        const ExecPlan& plan = mInstance.Plan();
        for (int producer : plan.SlotSourceNodeIds)
        {
            Pose pose;
            pose.bones = out.AllocateSlot();
            pose.count = out.nodeCount;
            GraphRunner::EvaluateNode(mInstance, producer, pose);
        }

        // GPU half: flatten to weighted terms using the nodes' current state.
        std::string err;
        if (!BuildHybridSamples(mInstance, mSamples, err))
        {
            mError = err;
            out.Clear();
            return;
        }
        NormalizeHybrid(mSamples);
        if (!CheckHybridLimits(plan, mSamples, static_cast<int>(kMaxTermsPerWork), static_cast<int>(kMaxSlotsPerWork), err))
        {
            mError = err;
            out.Clear();
            return;
        }
        for (const HybridSample& s : mSamples)
        {
            out.terms.push_back({s.bExternal, static_cast<uint32_t>(s.Index), s.TimeSec, s.Weight});
        }
    }

    CostModel HybridEvaluator::Cost() const
    {
        CostModel cost;
        const Model* model = mInstance.GetModel();
        if (model != nullptr && model->IsTrsTexReady())
        {
            cost.ResidentBytes = model->GetTrsTexCpu().TexelBytes();
        }
        cost.SlotUploadBytes = static_cast<size_t>(mInstance.Plan().NumUploadSlots) * mInstance.NodeCount() * 48u;
        cost.CpuPosesPerFrame = static_cast<size_t>(mInstance.Plan().NumUploadSlots);
        cost.GpuTerms = mSamples.size();
        return cost;
    }
} // namespace RAnimation
