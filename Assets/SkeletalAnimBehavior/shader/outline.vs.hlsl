#include "NRI.hlsl"

struct VSOutput
{
    float4 position : SV_Position;
};

// Fullscreen triangle from the vertex id (no vertex buffer).
VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}
