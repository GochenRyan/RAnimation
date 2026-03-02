#include <Model/AnimChannel.h>

using namespace RAnimation;

void AnimChannel::LoadChannelData(aiNodeAnim* nodeAnim)
{
    mNodeName = nodeAnim->mNodeName.C_Str();
    unsigned int numTranslations = nodeAnim->mNumPositionKeys;
    unsigned int numRotations = nodeAnim->mNumRotationKeys;
    unsigned int numScalings = nodeAnim->mNumScalingKeys;
    unsigned int preState = nodeAnim->mPreState;
    unsigned int postState = nodeAnim->mPostState;

    for (size_t i = 0; i < numTranslations; ++i)
    {
        mTranslationTimings.emplace_back(static_cast<float>(nodeAnim->mPositionKeys[i].mTime));
        mTranslations.emplace_back(glm::vec3(nodeAnim->mPositionKeys[i].mValue.x,
                                             nodeAnim->mPositionKeys[i].mValue.y,
                                             nodeAnim->mPositionKeys[i].mValue.z));
    }

    for (size_t i = 0; i < numRotations; ++i)
    {
        mRotationTimings.emplace_back(static_cast<float>(nodeAnim->mRotationKeys[i].mTime));
        mRotations.emplace_back(glm::vec3(nodeAnim->mRotationKeys[i].mValue.x,
                                          nodeAnim->mRotationKeys[i].mValue.y,
                                          nodeAnim->mRotationKeys[i].mValue.z));
    }

    for (size_t i = 0; i < numTranslations; ++i)
    {
        mScaleTimings.emplace_back(static_cast<float>(nodeAnim->mScalingKeys[i].mTime));
        mScalings.emplace_back(glm::vec3(nodeAnim->mScalingKeys[i].mValue.x,
                                         nodeAnim->mScalingKeys[i].mValue.y,
                                         nodeAnim->mScalingKeys[i].mValue.z));
    }

    for (size_t i = 0; i < mTranslationTimings.size() - 1; ++i)
    {
        mInverseTranslationTimeDiffs.emplace_back(1.0f / (mTranslationTimings.at(i + 1) - mTranslationTimings.at(i)));
    }

    for (size_t i = 0; i < mRotationTimings.size() - 1; ++i)
    {
        mInverseRotationTimeDiffs.emplace_back(1.0f / (mRotationTimings.at(i + 1) - mRotationTimings.at(i)));
    }

    for (size_t i = 0; i < mScaleTimings.size() - 1; ++i)
    {
        mInverseScaleTimeDiffs.emplace_back(1.0f / (mScaleTimings.at(i + 1) - mScaleTimings.at(i)));
    }
}

std::string AnimChannel::GetTargetNodeName()
{
    return mNodeName;
}

float AnimChannel::GetMaxTime()
{
    float maxTranslationTime = mTranslationTimings.at(mTranslationTimings.size() - 1);
    float maxRotationTime = mRotationTimings.at(mRotationTimings.size() - 1);
    float maxScaleTime = mScaleTimings.at(mScaleTimings.size() - 1);

    return std::max(std::max(maxRotationTime, maxTranslationTime), maxScaleTime);
}

glm::mat4 AnimChannel::GetTRSMatrix(float time)
{
    return glm::translate(glm::mat4_cast(GetRotation(time)) * glm::scale(glm::mat4(1.0f), GetScaling(time)),
                          GetTranslation(time));
}

glm::vec3 AnimChannel::GetTranslation(float time)
{
    if (mTranslations.empty())
    {
        return glm::vec3(0.0f);
    }

    /* handle time before and after */
    switch (mPreState)
    {
        case 0:
            /* do not change vertex position-> aiAnimBehaviour_DEFAULT */
            if (time < mTranslationTimings.at(0))
            {
                return glm::vec3(0.0f);
            }
            break;
        case 1:
            /* use value at zero time "aiAnimBehaviour_CONSTANT" */
            if (time < mTranslationTimings.at(0))
            {
                return mTranslations.at(0);
            }
            break;
        default:
            break;
    }

    switch (mPostState)
    {
        case 0:
            if (time > mTranslationTimings.at(mTranslationTimings.size() - 1))
            {
                return glm::vec3(0.0f);
            }
            break;
        case 1:
            if (time >= mTranslationTimings.at(mTranslationTimings.size() - 1))
            {
                return mTranslations.at(mTranslations.size() - 1);
            }
            break;
        default:
            break;
    }

    auto timeIndexPos = std::lower_bound(mTranslationTimings.begin(), mTranslationTimings.end(), time);
    /* catch rare cases where time is exaclty zero */
    int timeIndex = std::max(static_cast<int>(std::distance(mTranslationTimings.begin(), timeIndexPos)) - 1, 0);
    float interpolatedTime = (time - mTranslationTimings.at(timeIndex)) * mInverseTranslationTimeDiffs.at(timeIndex);

    return glm::mix(mTranslations.at(timeIndex), mTranslations.at(timeIndex + 1), interpolatedTime);
}

glm::vec3 AnimChannel::GetScaling(float time)
{
    if (mScalings.empty())
    {
        return glm::vec3(0.0f);
    }

    /* handle time before and after */
    switch (mPreState)
    {
        case 0:
            /* do not change vertex position-> aiAnimBehaviour_DEFAULT */
            if (time < mScaleTimings.at(0))
            {
                return glm::vec3(0.0f);
            }
            break;
        case 1:
            /* use value at zero time "aiAnimBehaviour_CONSTANT" */
            if (time < mScaleTimings.at(0))
            {
                return mScalings.at(0);
            }
            break;
        default:
            break;
    }

    switch (mPostState)
    {
        case 0:
            if (time > mScaleTimings.at(mScaleTimings.size() - 1))
            {
                return glm::vec3(0.0f);
            }
            break;
        case 1:
            if (time >= mScaleTimings.at(mScaleTimings.size() - 1))
            {
                return mScalings.at(mScalings.size() - 1);
            }
            break;
        default:
            break;
    }

    auto timeIndexPos = std::lower_bound(mScaleTimings.begin(), mScaleTimings.end(), time);
    /* catch rare cases where time is exaclty zero */
    int timeIndex = std::max(static_cast<int>(std::distance(mScaleTimings.begin(), timeIndexPos)) - 1, 0);
    float interpolatedTime = (time - mScaleTimings.at(timeIndex)) * mInverseScaleTimeDiffs.at(timeIndex);

    return glm::mix(mScalings.at(timeIndex), mScalings.at(timeIndex + 1), interpolatedTime);
}

glm::quat AnimChannel::GetRotation(float time)
{
    if (mRotations.empty())
    {
        return glm::identity<glm::quat>();
    }

    /* handle time before and after */
    switch (mPreState)
    {
        case 0:
            /* do not change vertex position-> aiAnimBehaviour_DEFAULT */
            if (time < mRotationTimings.at(0))
            {
                return glm::identity<glm::quat>();
            }
            break;
        case 1:
            /* use value at zero time "aiAnimBehaviour_CONSTANT" */
            if (time < mRotationTimings.at(0))
            {
                return mRotations.at(0);
            }
            break;
        default:
            break;
    }

    switch (mPostState)
    {
        case 0:
            if (time > mRotationTimings.at(mRotationTimings.size() - 1))
            {
                return glm::identity<glm::quat>();
            }
            break;
        case 1:
            if (time >= mRotationTimings.at(mRotationTimings.size() - 1))
            {
                return mRotations.at(mRotations.size() - 1);
            }
            break;
        default:
            break;
    }

    auto timeIndexPos = std::lower_bound(mRotationTimings.begin(), mRotationTimings.end(), time);
    /* catch rare cases where time is exaclty zero */
    int timeIndex = std::max(static_cast<int>(std::distance(mRotationTimings.begin(), timeIndexPos)) - 1, 0);
    float interpolatedTime = (time - mRotationTimings.at(timeIndex)) * mInverseRotationTimeDiffs.at(timeIndex);

    /* rotations are interpolated via SLERP */
    return glm::normalize(glm::slerp(mRotations.at(timeIndex), mRotations.at(timeIndex + 1), interpolatedTime));
}
