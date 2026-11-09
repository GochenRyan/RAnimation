Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct PSInput
{
    float4 color    : COLOR0;
    float3 normal   : TEXCOORD1;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint pickID : TEXCOORD2;
};

struct PSOutput
{
    float4 color : SV_Target0;
    uint   id    : SV_Target1;
};

// World-space light direction shared with assimp_skinning.fs.hlsl - keep both in sync.
static const float3 lightDir = normalize(float3(4.0, 3.0, 6.0));
static const float3 lightColor = float3(1.0, 1.0, 1.0);

float toSRGB(float x)
{
    if (x <= 0.0031308)
        return 12.92 * x;
    else
        return 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

PSOutput main(PSInput input)
{
    float3 norm = normalize(input.normal);

    float diff = max(dot(norm, lightDir), 0.0);

    float4 texColor = tex.Sample(samp, input.texCoord);

    float3 result = texColor.rgb * diff * lightColor;

    PSOutput output;
    output.color = float4(result, texColor.a);
    output.id = input.pickID;
    return output;
}