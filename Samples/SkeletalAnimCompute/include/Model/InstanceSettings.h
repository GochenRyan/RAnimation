#pragma once

#include <glm/glm.hpp>

struct InstanceSettings final
{
    glm::vec3 mWorldPosition = glm::vec3(0.0f);
    glm::vec3 mWorldRotation = glm::vec3(0.0f);
    float mScale = 1.0f;
    bool mSwapYZAxis = false;

    unsigned int mAnimClipNr = 0;
    float mAnimPlayTimePos = 0.0f;
    float mAnimSpeedFactor = 1.0f;
};
