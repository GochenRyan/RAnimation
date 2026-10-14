#pragma once

#include <algorithm>
#include <vector>

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

    // Flattened weighted sum of several poses, the CPU twin of the kernel's inner loop: hemisphere
    // alignment towards the first added pose, then per-node division by the summed weight (so weights
    // need not sum to 1). Finish() leaves identity on nodes that received no weight.
    class PoseAccumulator
    {
    public:
        explicit PoseAccumulator(Pose& out) : mOut(out), mFirst(true), mWeight(out.count, 0.0f)
        {
            for (uint32_t i = 0; i < mOut.count; ++i)
            {
                mOut[i].T = glm::vec3(0.0f);
                mOut[i].S = glm::vec3(0.0f);
                mOut[i].R = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
            }
        }

        void Add(const Pose& pose, float weight)
        {
            if (mFirst)
            {
                mReference.assign(pose.bones, pose.bones + std::min(pose.count, mOut.count));
                mFirst = false;
            }
            for (uint32_t i = 0; i < mOut.count && i < pose.count; ++i)
            {
                const glm::quat aligned = AlignHemisphere(mReference[i].R, pose[i].R);
                mOut[i].T += pose[i].T * weight;
                mOut[i].S += pose[i].S * weight;
                mOut[i].R += aligned * weight;
                mWeight[i] += weight;
            }
        }

        void Finish()
        {
            for (uint32_t i = 0; i < mOut.count; ++i)
            {
                if (mWeight[i] <= 0.0f)
                {
                    mOut[i] = Transform{};
                    continue;
                }
                mOut[i].T /= mWeight[i];
                mOut[i].S /= mWeight[i];
                mOut[i].R = glm::normalize(mOut[i].R);
            }
        }

    private:
        Pose& mOut;
        bool mFirst;
        std::vector<Transform> mReference;
        std::vector<float> mWeight;
    };

    inline void CopyPose(const Pose& from, Pose& to)
    {
        for (uint32_t i = 0; i < to.count && i < from.count; ++i)
        {
            to.bones[i] = from.bones[i];
        }
    }
} // namespace RAnimation
