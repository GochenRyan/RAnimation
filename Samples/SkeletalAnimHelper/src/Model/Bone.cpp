#include <fmt/base.h>
#include <Model/Bone.h>

using namespace RAnimation;

Bone::Bone(unsigned int id, std::string name, glm::mat4 matrix) : mBoneId(id), mNodeName(name), mOffsetMatrix(matrix)
{
    fmt::print("{}: --- added bone {} for node name '{}'\n", __FUNCTION__, mBoneId, mNodeName);
}

unsigned int Bone::GetBoneId()
{
    return mBoneId;
}

std::string Bone::GetBoneName()
{
    return mNodeName;
}

glm::mat4 Bone::GetOffsetMatrix()
{
    return mOffsetMatrix;
}
