#pragma once

#include <cstdint>
#include <vector>

#include <AnimGraph/Pose.h>

namespace RAnimation
{
    // ---- GPU layouts (mirrored by hybrid_common.hlsli) ---------------------------------------------------

    // One blend term of a Work record. bit31 of kindAndIndex selects the source:
    //   0 -> baked TRS texture, low bits = clip index, timeSec = clip time (already wrapped/clamped by the CPU)
    //   1 -> upload slot, low bits = absolute element index of the slot's node 0 in the slot buffer
    struct HybridTerm
    {
        uint32_t kindAndIndex = 0;
        float timeSec = 0.0f;
        float weight = 0.0f;
        float _pad = 0.0f;
    };
    static_assert(sizeof(HybridTerm) == 16, "HybridTerm must match the HLSL layout");

    // One thread group's worth of work: blend termCount terms and write nodeCount local matrices starting at
    // outputBase in the TRSMatrix buffer.
    struct HybridWork
    {
        uint32_t firstTerm = 0;
        uint32_t termCount = 0;
        uint32_t outputBase = 0;
        uint32_t _pad = 0;
    };
    static_assert(sizeof(HybridWork) == 16, "HybridWork must match the HLSL layout");

    constexpr uint32_t kHybridTermSlotBit = 0x80000000u;
    constexpr uint32_t kHybridTermIndexMask = 0x7FFFFFFFu;
    constexpr uint32_t kMaxTermsPerWork = 8;  // buffer capacity per Work; the editor refuses graphs beyond it
    constexpr uint32_t kMaxSlotsPerWork = 4;  // CPU->GPU seams per instance

    // ---- CPU side ----------------------------------------------------------------------------------------

    struct HybridTermCpu
    {
        bool bSlot = false;   // true = upload slot (index is the LOCAL slot 0..slotCount-1), false = clip
        uint32_t index = 0;
        float timeSec = 0.0f;
        float weight = 0.0f;
    };

    // What one instance hands to the renderer each frame: the terms plus the CPU-evaluated slot poses
    // (slot i occupies [i * nodeCount, (i + 1) * nodeCount) of slotData). The renderer assigns the absolute
    // slot element bases when it packs the frame.
    struct HybridWorkCpu
    {
        std::vector<HybridTermCpu> terms;
        std::vector<Transform> slotData;
        uint32_t slotCount = 0;
        uint32_t nodeCount = 0;

        void Clear()
        {
            terms.clear();
            slotData.clear();
            slotCount = 0;
        }

        Transform* AllocateSlot()
        {
            const size_t base = slotData.size();
            slotData.resize(base + nodeCount);
            ++slotCount;
            return slotData.data() + base;
        }
    };
} // namespace RAnimation
