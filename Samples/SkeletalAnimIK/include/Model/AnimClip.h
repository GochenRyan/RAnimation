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
        const std::vector<std::shared_ptr<AnimChannel>>& GetChannels() const;

        std::string GetClipName() const;
        float GetClipDuration() const;
        float GetClipTicksPerSecond() const;
        bool GetClipLoop() const { return mLoop; }
        void SetClipLoop(bool loop) { mLoop = loop; }

        void SetClipName(std::string name);
        void SetClipDuration(float duration);
        void SetClipTicksPerSecond(float ticksPerSecond);

    private:
        std::string mClipName;
        double mClipDuration = 0.0f;
        double mClipTicksPerSecond = 0.0f;
        bool mLoop = true; // authoring hint from the clips json; runtime looping lives on the graph node

        std::vector<std::shared_ptr<AnimChannel>> mAnimChannels{};
    };
} // namespace RAnimation
