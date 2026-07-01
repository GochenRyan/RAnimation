#pragma once
#include <memory>
#include <string>
#include <vector>

#include <Model/AnimChannel.h>

namespace RAnimation
{
    class AnimClip
    {
    public:
        void AddChannel(std::shared_ptr<AnimChannel> channel);
        const std::vector<std::shared_ptr<AnimChannel>>& GetChannels();

        std::string GetClipName();
        float GetClipDuration();
        float GetClipTicksPerSecond();

        void SetClipName(std::string name);
        void SetClipDuration(float duration);
        void SetClipTicksPerSecond(float ticksPerSecond);

    private:
        std::string mClipName;
        double mClipDuration = 0.0f;
        double mClipTicksPerSecond = 0.0f;

        std::vector<std::shared_ptr<AnimChannel>> mAnimChannels{};
    };
} // namespace RAnimation
