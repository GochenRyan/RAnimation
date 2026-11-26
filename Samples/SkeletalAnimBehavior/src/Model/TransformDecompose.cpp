#include <Model/TransformDecompose.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

namespace RAnimation
{
    bool DecomposeBindTransform(const glm::mat4& transform, Transform& out)
    {
        glm::vec3 skew = glm::vec3(0.0f);
        glm::vec4 perspective = glm::vec4(0.0f);
        glm::vec3 translation = glm::vec3(0.0f);
        glm::vec3 scaling = glm::vec3(1.0f);
        glm::quat rotation = glm::identity<glm::quat>();

        const bool ok = glm::decompose(transform, scaling, rotation, translation, skew, perspective);

        out.T = translation;
        out.R = glm::normalize(rotation);
        out.S = scaling;
        return ok;
    }
} // namespace RAnimation
