#include "NRI.hlsl"
#include "hybrid_common.hlsli"

// Tier C skinning: bone matrices are per TRACK (evaluated with an identity model root), and each crowd
// member supplies its own world matrix + track index. Position AND normal go through world * skin.

struct Constants
{
    int modelStride;
    int worldPosOffset;
    int pickIDBase;
    int crowdInstanceBase;
};

struct CameraBuffer
{
    float4x4 view;
    float4x4 proj;
};

struct VSInput
{
    float3 position   : POSITION;
    float4 color      : COLOR0;
    float3 normal     : NORMAL;
    float2 texCoord   : TEXCOORD0;
    uint4 boneIDs     : BLENDINDICES;
    float4 weights    : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : TEXCOORD1;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint pickID : TEXCOORD2;
};

NRI_RESOURCE(ConstantBuffer<CameraBuffer>, g_Camera, b, 0, 1);
NRI_RESOURCE(StructuredBuffer<float4x4>, g_BoneMatrices, t, 1, 1);
NRI_RESOURCE(StructuredBuffer<CrowdInstance>, g_Instances, t, 2, 1);
NRI_ROOT_CONSTANTS(Constants, g_PushConstants, 0, 2);

VSOutput main(VSInput input, NRI_DECLARE_DRAW_PARAMETERS)
{
    VSOutput output;

    CrowdInstance inst = g_Instances[NRI_INSTANCE_ID + uint(g_PushConstants.crowdInstanceBase)];
    int skinMatOffset = int(inst.trackIndex) * g_PushConstants.modelStride + g_PushConstants.worldPosOffset;

    float4x4 skinMat =
        input.weights.x * g_BoneMatrices[input.boneIDs.x + skinMatOffset] +
        input.weights.y * g_BoneMatrices[input.boneIDs.y + skinMatOffset] +
        input.weights.z * g_BoneMatrices[input.boneIDs.z + skinMatOffset] +
        input.weights.w * g_BoneMatrices[input.boneIDs.w + skinMatOffset];

    float4x4 M = mul(inst.world, skinMat);

    float4 worldPos = mul(M, float4(input.position, 1.0f));
    float4 viewPos = mul(g_Camera.view, worldPos);

    output.position = mul(g_Camera.proj, viewPos);
    output.normal = mul((float3x3) M, input.normal);
    output.texCoord = input.texCoord;
    output.pickID = NRI_INSTANCE_ID + uint(g_PushConstants.pickIDBase) + 1;

    return output;
}
