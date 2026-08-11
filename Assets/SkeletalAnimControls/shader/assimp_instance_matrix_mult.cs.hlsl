#include "NRI.hlsl"

struct Constants
{
    uint nodeTransformOffset;
    uint boneMatrixOffset;
    uint modelRootOffset;
    uint numberOfNodes;
    uint numberOfBones;
    uint instanceCount;
};

NRI_ROOT_CONSTANTS(Constants, g_PushConstants, 0, 2);
NRI_RESOURCE(StructuredBuffer<float4x4>, g_trsMat, t, 0, 0);
NRI_RESOURCE(RWStructuredBuffer<float4x4>, g_boneMatrices, u, 1, 0);
NRI_RESOURCE(StructuredBuffer<int>, g_parentMatrixIndices, t, 0, 1);
NRI_RESOURCE(StructuredBuffer<float4x4>, g_boneOffsets, t, 1, 1);
NRI_RESOURCE(StructuredBuffer<float4x4>, g_modelRootMatrices, t, 2, 1);
NRI_RESOURCE(StructuredBuffer<uint>, g_boneNodeIndices, t, 3, 1);

[numthreads(1, 32, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint node = dispatchThreadID.x;
    uint instance = dispatchThreadID.y;

    if (node >= g_PushConstants.numberOfBones || instance >= g_PushConstants.instanceCount)
    {
        return;
    }

    uint boneIndex = node + g_PushConstants.numberOfBones * instance + g_PushConstants.boneMatrixOffset;
    uint nodeIndex = g_boneNodeIndices[boneIndex];

    /* get node matrix, always valid */
    float4x4 nodeMatrix = g_trsMat[nodeIndex];
    
    int parentIndex = g_parentMatrixIndices[nodeIndex];
    while (parentIndex >= 0)
    {
        nodeMatrix = mul(g_trsMat[(uint)parentIndex], nodeMatrix);
        parentIndex = g_parentMatrixIndices[parentIndex];
    }

    float4x4 modelRootMatrix = g_modelRootMatrices[g_PushConstants.modelRootOffset + instance];
    g_boneMatrices[boneIndex] = mul(mul(modelRootMatrix, nodeMatrix), g_boneOffsets[boneIndex]);
}
