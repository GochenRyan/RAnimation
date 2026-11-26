#pragma once
#include <string>
#include <string_view>

#include <AnimGraph/IPoseEvaluator.h>

namespace RAnimation
{
    class AnimGraphInstance;

    // Whole graph on the CPU; the instance is baked once, Tick reuses it every frame.
    class GraphCpuEvaluator final : public IPoseEvaluator
    {
    public:
        explicit GraphCpuEvaluator(AnimGraphInstance& instance);
        GraphCpuEvaluator(const GraphCpuEvaluator&) = delete;
        GraphCpuEvaluator& operator=(const GraphCpuEvaluator&) = delete;

        std::string_view Name() const override { return "CPU node graph"; }
        bool Usable() const override { return mError.empty(); }
        const std::string& Why() const override { return mError; }
        void Tick(float deltaTime, HybridWorkCpu& out) override;
        CostModel Cost() const override;

    private:
        AnimGraphInstance& mInstance;
        std::string mError;
    };
} // namespace RAnimation
