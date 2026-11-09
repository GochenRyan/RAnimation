#pragma once
#include <string>
#include <string_view>
#include <vector>

#include <AnimGraph/ExecPlan.h>
#include <AnimGraph/IPoseEvaluator.h>

namespace RAnimation
{
    class AnimGraphInstance;

    // Hybrid execution: the CPU evaluates everything above the cut into upload slots; the GPU samples the TRS
    // texture and blends it with the slots.
    class HybridEvaluator final : public IPoseEvaluator
    {
    public:
        explicit HybridEvaluator(AnimGraphInstance& instance);
        HybridEvaluator(const HybridEvaluator&) = delete;
        HybridEvaluator& operator=(const HybridEvaluator&) = delete;

        std::string_view Name() const override { return "hybrid (CPU seams + GPU terms)"; }
        bool Usable() const override { return mError.empty(); }
        const std::string& Why() const override { return mError; }
        void Tick(float deltaTime, HybridWorkCpu& out) override;
        CostModel Cost() const override;

        const std::vector<HybridSample>& LastSamples() const { return mSamples; }

    private:
        AnimGraphInstance& mInstance;
        std::vector<HybridSample> mSamples;
        std::string mError;
    };
} // namespace RAnimation
