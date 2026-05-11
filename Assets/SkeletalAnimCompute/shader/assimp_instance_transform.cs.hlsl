#include "NRI.hlsl"

struct Constants
{
    uint modelOffset;
    uint numberOfBones;
};

/* data format to be uploaded to compute shader */
struct NodeTransformData {
  float4 translation;
  float4 scale;
  float4 rotation; // this is is a quaternion
};

NRI_ROOT_CONSTANTS(Constants, g_PushConstants, 0, 1);
NRI_RESOURCE(StructuredBuffer<NodeTransformData>, g_NodeTransformData, t, 0, 0);
NRI_RESOURCE(RWStructuredBuffer<float4x4>, g_trsMat, u, 1, 0);

float4x4 getTranslationMatrix(uint index)
{
    float4 t = g_NodeTransformData[index].translation;
    return float4x4(
        1.0f, 0.0f, 0.0f, t.x,
        0.0f, 1.0f, 0.0f, t.y,
        0.0f, 0.0f, 1.0f, t.z,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

float4x4 getScaleMatrix(uint index)
{
    float4 s = g_NodeTransformData[index].scale;
    return float4x4(
        s.x,   0.0f, 0.0f, 0.0f,
        0.0f,  s.y,  0.0f, 0.0f,
        0.0f,  0.0f, s.z,  0.0f,
        0.0f,  0.0f, 0.0f, 1.0f
    );
}

float4x4 getRotationMatrix(uint index)
{
    float4 q = g_NodeTransformData[index].rotation;

    // Optional but safer if the source quaternion is not guaranteed normalized.
    q = normalize(q);

    float qxx = q.x * q.x;
    float qyy = q.y * q.y;
    float qzz = q.z * q.z;
    float qxz = q.x * q.z;
    float qxy = q.x * q.y;
    float qyz = q.y * q.z;
    float qwx = q.w * q.x;
    float qwy = q.w * q.y;
    float qwz = q.w * q.z;

    // This is the row-by-row HLSL form equivalent to the GLSL mat4 constructor.
    return float4x4(
        1.0f - 2.0f * (qyy + qzz), 2.0f * (qxy + qwz),        2.0f * (qxz - qwy),        0.0f,
        2.0f * (qxy - qwz),        1.0f - 2.0f * (qxx + qzz), 2.0f * (qyz + qwx),        0.0f,
        2.0f * (qxz + qwy),        2.0f * (qyz - qwx),        1.0f - 2.0f * (qxx + qyy), 0.0f,
        0.0f,                      0.0f,                      0.0f,                      1.0f
    );
}

[numthreads(1, 32, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint node = dispatchThreadID.x;
    uint instance = dispatchThreadID.y;

    uint index = node + g_PushConstants.numberOfBones * instance + g_PushConstants.modelOffset;

    float4x4 translationMat = GetTranslationMatrix(index);
    float4x4 rotationMat = GetRotationMatrix(index);
    float4x4 scaleMat = GetScaleMatrix(index);

    g_TrsMat[index] = mul(mul(translationMat, rotationMat), scaleMat);
}