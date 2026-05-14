#include <Model/AnimClip.h>

using namespace RAnimation;

void AnimClip::AddChannels(aiAnimation* animation)
{
    mClipName = animation->mName.C_Str();
    mClipDuration = animation->mDuration;
    mClipTicksPerSecond = animation->mTicksPerSecond;

    for (unsigned int i = 0; i < animation->mNumChannels; ++i)
    {
        std::shared_ptr<AnimChannel> channel = std::make_shared<AnimChannel>();

        channel->LoadChannelData(animation->mChannels[i]);
        mAnimChannels.emplace_back(channel);
    }
}

const std::vector<std::shared_ptr<AnimChannel>>& AnimClip::GetChannels()
{
    return mAnimChannels;
}

std::string AnimClip::GetClipName()
{
    return mClipName;
}

float AnimClip::GetClipDuration()
{
    return static_cast<float>(mClipDuration);
}

float AnimClip::GetClipTicksPerSecond()
{
    return static_cast<float>(mClipTicksPerSecond);
}

void AnimClip::SetClipName(std::string name)
{
    mClipName = name;
}
