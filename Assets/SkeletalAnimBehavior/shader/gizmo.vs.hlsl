#include "NRI.hlsl"

struct CameraBuffer
{
    float4x4 view;
    float4x4 proj;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color    : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

NRI_RESOURCE(ConstantBuffer<CameraBuffer>, g_Camera, b, 0, 0);

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 viewPos = mul(g_Camera.view, float4(input.position, 1.0f));
    output.position = mul(g_Camera.proj, viewPos);
    output.color = input.color;
    return output;
}
