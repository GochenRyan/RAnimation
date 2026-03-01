#pragma once

#include <string>
#include <vector>
#include <memory>

#include <assimp/anim.h>

#include <Model/AnimChannel.h>

class AssimpAnimClip {
  public:
    void AddChannels(aiAnimation* animation);
    std::vector<std::shared_ptr<AssimpAnimChannel>> GetChannels();

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
