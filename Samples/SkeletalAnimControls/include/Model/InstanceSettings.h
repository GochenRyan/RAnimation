#pragma once

#include <glm/glm.hpp>

struct InstanceSettings final
{
    glm::vec3 mWorldPosition = glm::vec3(0.0f);
    glm::vec3 mWorldRotation = glm::vec3(0.0f);
    float mScale = 1.0f;

    unsigned int mAnimClipNr = 0;
    float mAnimPlayTimePos = 0.0f;
    float mAnimSpeedFactor = 1.0f;
    // Normalised start phase in [0,1): seeds the graph clocks and selects the tier-C track bucket.
    float mAnimPhase = 0.0f;
};
