#include "NRI.hlsl"

struct PSInput
{
    float3 color : COLOR0;
};

struct PSOutput
{
    float4 color : SV_Target0;
    uint   id    : SV_Target1;
};

PSOutput main(PSInput input)
{
    PSOutput output;
    output.color = float4(input.color, 1.0f);
    output.id = 0; // gizmo lines are not pickable
    return output;
}
