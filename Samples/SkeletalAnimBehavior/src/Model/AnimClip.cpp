#include <Model/AnimClip.h>

using namespace RAnimation;

void AnimClip::AddChannel(std::shared_ptr<AnimChannel> channel)
{
    mAnimChannels.emplace_back(std::move(channel));
}

const std::vector<std::shared_ptr<AnimChannel>>& AnimClip::GetChannels() const
{
    return mAnimChannels;
}

std::string AnimClip::GetClipName() const
{
    return mClipName;
}

float AnimClip::GetClipDuration() const
{
    return static_cast<float>(mClipDuration);
}

float AnimClip::GetClipTicksPerSecond() const
{
    return static_cast<float>(mClipTicksPerSecond);
}

void AnimClip::SetClipName(std::string name)
{
    mClipName = name;
}

void AnimClip::SetClipDuration(float duration)
{
    mClipDuration = duration;
}

void AnimClip::SetClipTicksPerSecond(float ticksPerSecond)
{
    mClipTicksPerSecond = ticksPerSecond;
}
