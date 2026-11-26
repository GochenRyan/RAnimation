#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace RAnimation
{
    // One node's local (relative to parent) transform. Quaternion memory order is x,y,z,w (glm default);
    // when uploading to the GPU pack it explicitly (see RNodeTransformData) instead of memcpy'ing.
    struct Transform
    {
        glm::vec3 T = glm::vec3(0.0f);
        glm::quat R = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 S = glm::vec3(1.0f);
    };

    // A whole-skeleton pose: one Transform per node in Model::GetNodeList() order. Non-owning view; the
    // storage comes from a MemStack (per-frame arena) or a std::vector owned by the caller.
    struct Pose
    {
        Transform* bones = nullptr;
        uint32_t count = 0;

        Transform& operator[](uint32_t i) { return bones[i]; }
        const Transform& operator[](uint32_t i) const { return bones[i]; }
    };
} // namespace RAnimation
