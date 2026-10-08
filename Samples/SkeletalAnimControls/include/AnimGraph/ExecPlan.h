#pragma once
#include <string>
#include <vector>

#include <AnimGraph/GraphDesc.h>

namespace RAnimation
{
    class AnimGraphInstance;

    // Hybrid execution plan: splits a graph into a CPU half and a GPU half. Root is always on the GPU; the
    // GPU property propagates upstream along inputs and stops at nodes that cannot run on the GPU or carry
    // bForceCpu; such a node and its whole upstream run on the CPU. Every CPU -> GPU edge on the cut is an
    // upload slot (one slot per CPU producer, even with several consumers).
    enum class NodeSide
    {
        Cpu,
        Gpu
    };

    struct SeamEdge
    {
        int FromNodeId = 0; // CPU side (produces the pose)
        int ToNodeId = 0;   // GPU side (consumes it)
        std::string ToPin;
        int SlotIndex = 0;  // upload slot, local to the instance
    };

    struct ExecPlan
    {
        std::vector<NodeSide> Sides;  // per GraphDesc node
        std::vector<SeamEdge> Seams;  // sorted by SlotIndex
        std::vector<int> CpuNodeIds;
        std::vector<int> GpuNodeIds;
        std::vector<int> SlotSourceNodeIds; // slot s is produced by node SlotSourceNodeIds[s]
        int NumUploadSlots = 0;
        bool bAllCpu = false;
        bool bAllGpu = false;
        std::string Summary;

        int SlotOf(int cpuNodeId) const
        {
            for (size_t s = 0; s < SlotSourceNodeIds.size(); ++s)
            {
                if (SlotSourceNodeIds[s] == cpuNodeId)
                {
                    return static_cast<int>(s);
                }
            }
            return -1;
        }
    };

    ExecPlan AnalyzeExecPlan(const GraphDesc& desc);

    // One flattened GPU term: a CPU node on the cut becomes "slot s", a ClipPlayer becomes "clip c".
    struct HybridSample
    {
        bool bExternal = false; // true = upload slot (CPU result), false = TRS texture
        int Index = 0;          // slot index or clip index
        float Weight = 0.0f;
        float TimeSec = 0.0f;   // texture terms: wrapped/clamped clip time
    };

    // Flattens the GPU half from Root using the baked nodes' current state (ClipPlayer::Time, blend
    // params). Root is always GPU, so an all-CPU graph flattens to the single term {slot 0, weight 1}.
    bool BuildHybridSamples(const AnimGraphInstance& instance, std::vector<HybridSample>& out, std::string& outError);

    // Merge terms with the same source, drop zero weights, normalise.
    void NormalizeHybrid(std::vector<HybridSample>& samples);

    // The kernel's per-Work capacity is fixed (kMaxTermsPerWork, kMaxSlotsPerWork); graphs beyond it are
    // rejected with a message rather than silently truncated.
    bool CheckHybridLimits(const ExecPlan& plan,
                           const std::vector<HybridSample>& work,
                           int maxTerms,
                           int maxSlots,
                           std::string& outError);
} // namespace RAnimation
