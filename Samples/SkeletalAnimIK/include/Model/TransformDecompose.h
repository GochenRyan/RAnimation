#pragma once

#include <glm/glm.hpp>

#include <AnimGraph/Pose.h>

namespace RAnimation
{
    // Decomposes a node's local (bind / rest) matrix into T, R, S exactly the way the runtime Node does it.
    // Shared by Node::SetLocalTransform and the TrsTextureBaker so both agree bit-for-bit on the bind pose.
    // Returns false when glm::decompose fails; out is still filled with the best-effort result.
    bool DecomposeBindTransform(const glm::mat4& transform, Transform& out);
} // namespace RAnimation
