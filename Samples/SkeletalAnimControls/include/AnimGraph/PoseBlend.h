#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <AnimGraph/Pose.h>

namespace RAnimation
{
    // The one blend formula shared by the CPU nodes and (in HLSL) the hybrid kernel: linear T/S, nlerp R with
    // hemisphere alignment towards the first operand. Deliberately NOT slerp so the GPU's flattened weighted
    // sum and the CPU's nested blends agree (see the plan's parity notes).
    inline glm::quat AlignHemisphere(const glm::quat& reference, const glm::quat& q)
    {
        return glm::dot(reference, q) < 0.0f ? -q : q;
    }

    inline Transform BlendTransform(const Transform& a, const Transform& b, float alpha)
    {
        Transform out;
        out.T = glm::mix(a.T, b.T, alpha);
        out.S = glm::mix(a.S, b.S, alpha);
        const glm::quat qb = AlignHemisphere(a.R, b.R);
        out.R = glm::normalize(a.R * (1.0f - alpha) + qb * alpha);
        return out;
    }

    inline void BlendPoses(const Pose& a, const Pose& b, float alpha, Pose& out)
    {
        for (uint32_t i = 0; i < out.count; ++i)
        {
            out.bones[i] = BlendTransform(a.bones[i], b.bones[i], alpha);
        }
    }

    inline void CopyPose(const Pose& from, Pose& to)
    {
        for (uint32_t i = 0; i < to.count && i < from.count; ++i)
        {
            to.bones[i] = from.bones[i];
        }
    }
} // namespace RAnimation
