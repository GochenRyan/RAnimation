#pragma once
#include <vector>

#include <assimp/anim.h>
#include <glm/glm.hpp>

namespace RAnimation
{
    class Bone
    {
    public:
        Bone(unsigned int id, std::string name, glm::mat4 matrix);

        unsigned int GetBoneId();
        std::string GetBoneName();
        glm::mat4 GetOffsetMatrix();

    private:
        unsigned int mBoneId = 0;
        std::string mNodeName;
        glm::mat4 mOffsetMatrix = glm::mat4(1.0f);
    };
} // namespace RAnimation
