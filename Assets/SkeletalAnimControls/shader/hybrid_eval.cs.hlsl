#include "NRI.hlsl"
#include "hybrid_common.hlsli"

// The one pose-evaluation kernel shared by all three tiers:
//   thread group = one Work record (a character, an NPC or a crowd track), thread = node.
//   Each term is either a row-interpolated sample of the baked TRS texture or a CPU-uploaded slot pose;
//   terms are blended with a flattened weighted nlerp and composed into the node's local T*R*S matrix.
//   A term may carry a mask row (LayeredBlend): its weight is multiplied by a per-node factor, and the sum
//   is renormalised per node so masked-out contributions do not darken the result.
// The tiers only differ in how the CPU issues the dispatch (see HybridEvalComputePass::Record).

struct Constants
{
    uint workBase;  // first Work of this dispatch
    uint nodeCount; // nodes per instance of this model
    uint flags;     // HYBRID_FLAG_INTERPOLATE
    uint _pad;
};

NRI_ROOT_CONSTANTS(Constants, g_PushConstants, 0, 2);

NRI_RESOURCE(StructuredBuffer<HybridWork>, g_Works, t, 0, 0);
NRI_RESOURCE(StructuredBuffer<HybridTerm>, g_Terms, t, 1, 0);
NRI_RESOURCE(StructuredBuffer<NodeTransformData>, g_Slots, t, 2, 0);
NRI_RESOURCE(RWStructuredBuffer<float4x4>, g_trsMat, u, 3, 0);
NRI_RESOURCE(StructuredBuffer<float>, g_Masks, t, 4, 0);

NRI_RESOURCE(Texture2D<float4>, g_TrsTex, t, 0, 1);
NRI_RESOURCE(StructuredBuffer<TrsClipRecord>, g_ClipTable, t, 1, 1);

[numthreads(64, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    HybridWork work = g_Works[g_PushConstants.workBase + groupId.x];
    const bool interpolate = (g_PushConstants.flags & HYBRID_FLAG_INTERPOLATE) != 0u;

    for (uint node = groupThreadId.x; node < g_PushConstants.nodeCount; node += 64u)
    {
        float3 T = 0.0f;
        float4 R = 0.0f;
        float3 S = 0.0f;
        float4 reference = float4(0.0f, 0.0f, 0.0f, 1.0f);
        float wsum = 0.0f;
        bool first = true;

        for (uint i = 0; i < work.termCount; ++i)
        {
            HybridTerm term = g_Terms[work.firstTerm + i];
            Trs v;
            if ((term.kindAndIndex & HYBRID_TERM_SLOT_BIT) != 0u)
            {
                v = LoadSlot(g_Slots, term.kindAndIndex & HYBRID_TERM_INDEX_MASK, node);
            }
            else
            {
                v = SampleTrs(g_TrsTex, g_ClipTable[term.kindAndIndex & HYBRID_TERM_INDEX_MASK], node, term.timeSec, interpolate);
            }

            if (first)
            {
                reference = v.r;
                first = false;
            }
            float w = term.weight;
            if (term.maskRow != 0u)
            {
                w *= g_Masks[term.maskRow - 1u + node];
            }
            T += v.t * w;
            S += v.s * w;
            R += AlignHemisphere(reference, v.r) * w;
            wsum += w;
        }

        if (first || wsum <= 0.0f)
        {
            // No terms (or every term masked out): identity so a broken Work is visible rather than garbage.
            T = 0.0f;
            S = 1.0f;
            R = float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        else
        {
            T /= wsum;
            S /= wsum;
        }

        g_trsMat[work.outputBase + node] = ComposeTRS(T, R, S);
    }
}
