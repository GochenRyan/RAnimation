Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PSInput
{
    float3 normal   : TEXCOORD1;
    float2 texCoord : TEXCOORD0;
};

float3 lightDir = normalize(float3(1.0, 1.0, 1.0));

float4 main(PSInput input) : SV_Target
{
    float3 norm = normalize(input.normal);
    float diff = max(dot(norm, lightDir), 0.0);

    float4 texColor = tex.Sample(samp, input.texCoord);

    return float4(texColor.rgb * diff, texColor.a);
}