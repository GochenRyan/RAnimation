Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PSInput
{
    float3 normal   : TEXCOORD1;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint pickID : TEXCOORD2;
};

struct PSOutput
{
    float4 color : SV_Target0;
    uint   id    : SV_Target1;
};

PSOutput main(PSInput input)
{
    // World-space light direction shared with assimp.fs.hlsl - keep both in sync.
    const float3 lightDir = normalize(float3(4.0, 3.0, 6.0));
    float3 norm = normalize(input.normal);
    float diff = max(dot(norm, lightDir), 0.0);

    float4 texColor = tex.Sample(samp, input.texCoord);

    PSOutput output;
    output.color = float4(texColor.rgb * diff, texColor.a);
    output.id = input.pickID;
    return output;
}
