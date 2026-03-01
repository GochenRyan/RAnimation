#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <assimp/anim.h>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

class AnimChannel {
  public:
    void LoadChannelData(aiNodeAnim* nodeAnim);
    std::string GetTargetNodeName();
    float GetMaxTime();

    glm::mat4 GetTRSMatrix(float time);

    glm::vec3 GetTranslation(float time);
    glm::vec3 GetScaling(float time);
    glm::quat GetRotation(float time);

  private:
    std::string mNodeName;

    /* use separate timinigs vectors, just in case not all keys have the same time */
    std::vector<float> mTranslationTiminngs{};
    std::vector<float> mInverseTranslationTimeDiffs{};
    std::vector<float> mRotationTiminigs{};
    std::vector<float> mInverseRotationTimeDiffs{};
    std::vector<float> mScaleTimings{};
    std::vector<float> mInverseScaleTimeDiffs{};

    /* every entry here has the same index as the timing for that key type */
    std::vector<glm::vec3> mTranslations{};
    std::vector<glm::vec3> mScalings{};
    std::vector<glm::quat> mRotations{};

    unsigned int mPreState = 0;
    unsigned int mPostState = 0;
};
