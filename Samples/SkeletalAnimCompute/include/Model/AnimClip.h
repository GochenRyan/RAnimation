#pragma once
#include <memory>
#include <string>
#include <vector>

#include <assimp/anim.h>

#include <Model/AnimChannel.h>

namespace RAnimation
{
    class AnimClip
    {
    public:
        void AddChannels(aiAnimation* animation);
        const std::vector<std::shared_ptr<AnimChannel>>& GetChannels();

        std::string GetClipName();
        float GetClipDuration();
        float GetClipTicksPerSecond();

        void SetClipName(std::string name);

    private:
        std::string mClipName;
        double mClipDuration = 0.0f;
        double mClipTicksPerSecond = 0.0f;

        std::vector<std::shared_ptr<AnimChannel>> mAnimChannels{};
    };
} // namespace RAnimation
