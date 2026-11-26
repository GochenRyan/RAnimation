#pragma once
#include <string>
#include <string_view>
#include <vector>

#include <AnimGraph/HybridWork.h>

namespace RAnimation
{
    struct CostModel
    {
        std::size_t ResidentBytes = 0;    // resident data (TRS texture / keyframes)
        std::size_t SlotUploadBytes = 0;  // pose bytes uploaded per instance per frame (the cost of crossing a seam)
        std::size_t CpuPosesPerFrame = 0; // poses the CPU evaluates per frame
        std::size_t GpuTerms = 0;         // terms the GPU blends per node per frame
    };

    // Produces one Work record (terms + slot poses) per frame. Matrix composition and hierarchy
    // concatenation are left to the GPU.
    //   GraphCpuEvaluator - whole graph on the CPU -> one slot, one term
    //   HybridEvaluator   - ExecPlan split -> CPU subgraph into slots, GPU subgraph flattened into texture terms
    class IPoseEvaluator
    {
    public:
        virtual ~IPoseEvaluator() = default;
        virtual std::string_view Name() const = 0;
        virtual bool Usable() const = 0;
        virtual const std::string& Why() const = 0;
        virtual void Tick(float deltaTime, HybridWorkCpu& out) = 0;
        virtual CostModel Cost() const = 0;
    };
} // namespace RAnimation
