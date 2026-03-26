cbuffer CameraBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

struct VSInput
{
    float4 color    : COLOR0;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD0;
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float3 normal   : TEXCOORD1;
    float2 texCoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.position, 1.0), model);
    float4 viewPos  = mul(worldPos, view);
    output.position = mul(viewPos, proj);

    output.color = input.color;
    output.normal = mul(input.normal, (float3x3)model);
    output.texCoord = input.texCoord;

    return output;
}