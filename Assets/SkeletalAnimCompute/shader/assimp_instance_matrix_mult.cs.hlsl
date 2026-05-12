#include "NRI.hlsl"

struct Constants
{
    uint modelOffset;
    uint numberOfBones;
};

NRI_ROOT_CONSTANTS(Constants, g_PushConstants, 7, 0);
NRI_RESOURCE(StructuredBuffer<float4x4>, g_trsMat, t, 0, 0);
NRI_RESOURCE(RWStructuredBuffer<float4x4>, g_nodeMatrices, u, 1, 0);
NRI_RESOURCE(StructuredBuffer<int>, g_parentMatrixIndices, t, 0, 1);
NRI_RESOURCE(StructuredBuffer<float4x4>, g_boneOffsets, t, 1, 1);

[numthreads(1, 32, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint node = dispatchThreadID.x;
    uint instance = dispatchThreadID.y;

    uint index = node + g_PushConstants.numberOfBones * instance + g_PushConstants.modelOffset;

    /* get node matrix, always valid */
    float4x4 nodeMatrix = g_trsMat[index];
    
    uint parent = 0;

    int parentIndex = g_parentMatrixIndices[node];
    while (parentIndex >= 0)
    {
        parent = (uint)parentIndex + g_PushConstants.numberOfBones * instance + g_PushConstants.modelOffset;
        nodeMatrix = mul(g_trsMat[parent], nodeMatrix);
        parentIndex = g_parentMatrixIndices[parentIndex];
    }

    /* root node has index -1 */
    if (parentIndex == -1)
    {
        g_nodeMatrices[index] = mul(nodeMatrix, g_boneOffsets[node]);
    }
}