#define MAX_BONES 100

cbuffer CameraBuffer : register(b0)
{
    float4x4 view;
    float4x4 proj;
};

cbuffer ModelBuffer : register(b1)
{
    float4x4 model;
};

cbuffer BoneBuffer : register(b2)
{
    float4x4 bones[MAX_BONES];
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 weights  : BLENDWEIGHT;
    uint4  boneIDs  : BLENDINDICES;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal   : TEXCOORD1;
    float2 texCoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 skinMat =
        input.weights.x * bones[input.boneIDs.x] +
        input.weights.y * bones[input.boneIDs.y] +
        input.weights.z * bones[input.boneIDs.z] +
        input.weights.w * bones[input.boneIDs.w];

    float4 pos = mul(float4(input.position, 1.0), skinMat);
    float4 worldPos = mul(pos, model);

    float4 viewPos = mul(worldPos, view);
    output.position = mul(viewPos, proj);

    output.normal = mul(input.normal, (float3x3)skinMat);
    output.texCoord = input.texCoord;

    return output;
}