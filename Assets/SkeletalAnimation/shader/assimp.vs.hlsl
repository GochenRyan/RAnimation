#include "NRI.hlsl"

struct Constants
{
    int modelStride;
    int worldPosOffset;
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
    float4 color    : COLOR0;
    float3 normal   : TEXCOORD1;
    float2 texCoord : TEXCOORD0;
};

NRI_RESOURCE(ConstantBuffer<CameraBuffer>, g_Camera, b, 0, 1);
NRI_RESOURCE(StructuredBuffer<float4x4>, g_WorldMatrices, t, 1, 1);
NRI_ROOT_CONSTANTS(Constants, g_PushConstants, 0, 2);

VSOutput main(VSInput input, NRI_DECLARE_DRAW_PARAMETERS)
{
    VSOutput output;

    float4x4 modelMat = g_WorldMatrices[NRI_INSTANCE_ID + g_PushConstants.worldPosOffset];
    float4 worldPos = mul(float4(input.position, 1.0f), modelMat);
    float4 viewPos = mul(worldPos, g_Camera.view);

    output.position = mul(viewPos, g_Camera.proj);
    output.color = input.color;
    output.normal = mul(input.normal, (float3x3) modelMat);
    output.texCoord = input.texCoord;

    return output;
}
