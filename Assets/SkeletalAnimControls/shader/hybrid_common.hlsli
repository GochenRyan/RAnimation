// Shared declarations for the hybrid pose-evaluation kernel. Layouts mirror AnimGraph/HybridWork.h and
// TrsTexture/TrsTextureFile.h on the CPU side; keep them in sync.
#ifndef HYBRID_COMMON_HLSLI
#define HYBRID_COMMON_HLSLI

struct HybridTerm
{
    uint kindAndIndex; // bit31: 1 = upload slot (low bits = absolute slot node base), 0 = clip index
    float timeSec;     // clip time, already wrapped/clamped by the CPU
    float weight;      // normalised on the CPU
    float _pad;
};

struct HybridWork
{
    uint firstTerm;
    uint termCount;
    uint outputBase; // first node index in the TRS matrix buffer
    uint _pad;
};

struct TrsClipRecord
{
    uint firstRow;
    uint frameCount;
    float durationSec;
    float rowsPerSec; // (frameCount - 1) / durationSec
};

// Upload slot element (same layout as the legacy NodeTransformData): T, S, R(xyzw)
struct NodeTransformData
{
    float4 translation;
    float4 scale;
    float4 rotation;
};

struct CrowdInstance
{
    float4x4 world;
    uint trackIndex;
    uint3 _pad;
};

#define HYBRID_TERM_SLOT_BIT   0x80000000u
#define HYBRID_TERM_INDEX_MASK 0x7FFFFFFFu
#define HYBRID_FLAG_INTERPOLATE 1u

struct Trs
{
    float3 t;
    float4 r; // xyzw
    float3 s;
};

float4 AlignHemisphere(float4 reference, float4 q)
{
    return dot(reference, q) < 0.0f ? -q : q;
}

// T * R * S with the rotation matrix written to match glm::mat4_cast (column-vector convention).
float4x4 ComposeTRS(float3 t, float4 q, float3 s)
{
    q = normalize(q);
    float qxx = q.x * q.x, qyy = q.y * q.y, qzz = q.z * q.z;
    float qxz = q.x * q.z, qxy = q.x * q.y, qyz = q.y * q.z;
    float qwx = q.w * q.x, qwy = q.w * q.y, qwz = q.w * q.z;

    float4x4 translation = float4x4(
        1.0f, 0.0f, 0.0f, t.x,
        0.0f, 1.0f, 0.0f, t.y,
        0.0f, 0.0f, 1.0f, t.z,
        0.0f, 0.0f, 0.0f, 1.0f);
    float4x4 rotation = float4x4(
        1.0f - 2.0f * (qyy + qzz), 2.0f * (qxy - qwz),        2.0f * (qxz + qwy),        0.0f,
        2.0f * (qxy + qwz),        1.0f - 2.0f * (qxx + qzz), 2.0f * (qyz - qwx),        0.0f,
        2.0f * (qxz - qwy),        2.0f * (qyz + qwx),        1.0f - 2.0f * (qxx + qyy), 0.0f,
        0.0f,                      0.0f,                      0.0f,                      1.0f);
    float4x4 scale = float4x4(
        s.x,  0.0f, 0.0f, 0.0f,
        0.0f, s.y,  0.0f, 0.0f,
        0.0f, 0.0f, s.z,  0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);
    return mul(mul(translation, rotation), scale);
}

// Integer Load of one node's three texels on one row. Never hardware-filter: the texture is data.
Trs LoadTrsTexel(Texture2D<float4> tex, uint node, uint row)
{
    Trs v;
    v.t = tex.Load(int3(int(node * 3u + 0u), int(row), 0)).xyz;
    v.r = tex.Load(int3(int(node * 3u + 1u), int(row), 0));
    v.s = tex.Load(int3(int(node * 3u + 2u), int(row), 0)).xyz;
    return v;
}

// Two-row sample at clip time t (rows span [0, duration] exactly; see TrsTexture::Bake).
Trs SampleTrs(Texture2D<float4> tex, TrsClipRecord clip, uint node, float timeSec, bool interpolate)
{
    float t = clamp(timeSec, 0.0f, clip.durationSec);
    float row = t * clip.rowsPerSec;
    uint last = clip.frameCount - 1u;
    uint r0 = min(uint(floor(row)), last);
    uint r1 = min(r0 + 1u, last);
    float frac = row - float(r0);
    if (!interpolate)
    {
        frac = frac >= 0.5f ? 1.0f : 0.0f;
    }

    Trs a = LoadTrsTexel(tex, node, clip.firstRow + r0);
    Trs b = LoadTrsTexel(tex, node, clip.firstRow + r1);
    Trs v;
    v.t = lerp(a.t, b.t, frac);
    v.s = lerp(a.s, b.s, frac);
    v.r = normalize(lerp(a.r, AlignHemisphere(a.r, b.r), frac));
    return v;
}

Trs LoadSlot(StructuredBuffer<NodeTransformData> slots, uint slotNodeBase, uint node)
{
    NodeTransformData d = slots[slotNodeBase + node];
    Trs v;
    v.t = d.translation.xyz;
    v.r = d.rotation;
    v.s = d.scale.xyz;
    return v;
}

#endif // HYBRID_COMMON_HLSLI
