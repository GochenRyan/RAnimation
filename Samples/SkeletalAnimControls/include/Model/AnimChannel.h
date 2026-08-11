#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/glm.hpp>

namespace RAnimation
{
    // Behaviour of a channel outside its authored time range. First-party replacement for assimp's
    // aiAnimBehaviour; the integer values match the pre/post-state semantics the interpolation relies on.
    enum class AnimBehaviour : uint8_t
    {
        Default = 0,
        Constant = 1,
        Linear = 2,
        Repeat = 3
    };

    class AnimChannel
    {
    public:
        // Populates the channel from already-sampled keyframes (e.g. produced by the USD loader). Timing
        // vectors are in clip ticks/frames and must be sorted ascending; value vectors are index-aligned
        // with their timings. The inverse per-segment time diffs used for interpolation are derived here.
        void SetChannelData(std::string nodeName,
                            std::vector<float> translationTimings,
                            std::vector<glm::vec3> translations,
                            std::vector<float> rotationTimings,
                            std::vector<glm::quat> rotations,
                            std::vector<float> scaleTimings,
                            std::vector<glm::vec3> scalings,
                            AnimBehaviour preState = AnimBehaviour::Default,
                            AnimBehaviour postState = AnimBehaviour::Default);

        std::string GetTargetNodeName();
        float GetMaxTime();
        bool HasTranslationKeys() const;
        bool HasScalingKeys() const;
        bool HasRotationKeys() const;

        glm::mat4 GetTRSMatrix(float time);

        glm::vec3 GetTranslation(float time);
        glm::vec3 GetScaling(float time);
        glm::quat GetRotation(float time);

    private:
        std::string mNodeName;

        /* use separate timinigs vectors, just in case not all keys have the same time */
        std::vector<float> mTranslationTimings{};
        std::vector<float> mInverseTranslationTimeDiffs{};
        std::vector<float> mRotationTimings{};
        std::vector<float> mInverseRotationTimeDiffs{};
        std::vector<float> mScaleTimings{};
        std::vector<float> mInverseScaleTimeDiffs{};

        /* every entry here has the same index as the timing for that key type */
        std::vector<glm::vec3> mTranslations{};
        std::vector<glm::vec3> mScalings{};
        std::vector<glm::quat> mRotations{};

        AnimBehaviour mPreState = AnimBehaviour::Default;
        AnimBehaviour mPostState = AnimBehaviour::Default;
    };
} // namespace RAnimation
