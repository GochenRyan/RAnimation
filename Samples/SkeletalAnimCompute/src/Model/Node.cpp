#include <Model/Node.h>
#include <fmt/base.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

using namespace RAnimation;

namespace
{
    void UpdateLocalMatrices(glm::vec3 translation,
                             glm::quat rotation,
                             glm::vec3 scaling,
                             glm::mat4& translationMatrix,
                             glm::mat4& rotationMatrix,
                             glm::mat4& scalingMatrix)
    {
        translationMatrix = glm::translate(glm::mat4(1.0f), translation);
        rotationMatrix = glm::mat4_cast(glm::normalize(rotation));
        scalingMatrix = glm::scale(glm::mat4(1.0f), scaling);
    }
}

std::shared_ptr<Node> Node::CreateNode(std::string nodeName)
{
    return std::make_shared<Node>(nodeName);
}

Node::Node(std::string nodeName) : mNodeName(nodeName)
{
}

std::shared_ptr<Node> Node::AddChild(std::string childName)
{
    std::shared_ptr<Node> child = std::make_shared<Node>(childName);
    child->mParentNode = shared_from_this();

    fmt::print("{}: -- adding child {} to parent {}\n",
               __FUNCTION__,
               childName,
               child->GetParentNodeName());

    mChildNodes.push_back(child);
    return child;
}

void Node::AddChilds(std::vector<std::string> childNodes)
{
    for (const auto& childName : childNodes)
    {
        std::shared_ptr<Node> child = std::make_shared<Node>(childName);
        child->mParentNode = shared_from_this();

        fmt::print("{}: -- adding child {} to parent {}\n",
                   __FUNCTION__,
                   childName,
                   child->GetParentNodeName());

        mChildNodes.push_back(child);
    }
}

void Node::SetTranslation(glm::vec3 translation)
{
    mTranslation = translation;
    UpdateLocalMatrices(mTranslation, mRotation, mScaling, mTranslationMatrix, mRotationMatrix, mScalingMatrix);
}

void Node::SetRotation(glm::quat rotation)
{
    mRotation = glm::normalize(rotation);
    UpdateLocalMatrices(mTranslation, mRotation, mScaling, mTranslationMatrix, mRotationMatrix, mScalingMatrix);
}

void Node::SetScaling(glm::vec3 scaling)
{
    mScaling = scaling;
    UpdateLocalMatrices(mTranslation, mRotation, mScaling, mTranslationMatrix, mRotationMatrix, mScalingMatrix);
}

void Node::SetLocalTransform(glm::mat4 transform)
{
    glm::vec3 skew = glm::vec3(0.0f);
    glm::vec4 perspective = glm::vec4(0.0f);
    glm::vec3 translation = glm::vec3(0.0f);
    glm::vec3 scaling = glm::vec3(1.0f);
    glm::quat rotation = glm::identity<glm::quat>();

    if (!glm::decompose(transform, scaling, rotation, translation, skew, perspective))
    {
        fmt::print(stderr, "{} warning: failed to decompose node transform for '{}'\n", __FUNCTION__, mNodeName);
    }

    mBindTranslation = translation;
    mBindRotation = glm::normalize(rotation);
    mBindScaling = scaling;

    ResetToBindPose();
}

void Node::ResetToBindPose()
{
    mTranslation = mBindTranslation;
    mRotation = mBindRotation;
    mScaling = mBindScaling;
    UpdateLocalMatrices(mTranslation, mRotation, mScaling, mTranslationMatrix, mRotationMatrix, mScalingMatrix);
}

void Node::SetRootTransformMatrix(glm::mat4 matrix)
{
    mRootTransformMatrix = matrix;
}

void Node::UpdateTRSMatrix()
{
    if (std::shared_ptr<Node> parentNode = mParentNode.lock())
    {
        mParentGlobalMatrix = parentNode->GetTRSMatrix();
    }

    mLocalTRSMatrix =
            mParentGlobalMatrix * mRootTransformMatrix * mTranslationMatrix * mRotationMatrix * mScalingMatrix;
}

glm::mat4 Node::GetTRSMatrix()
{
    return mLocalTRSMatrix;
}

std::string Node::GetNodeName()
{
    return mNodeName;
}

std::shared_ptr<Node> Node::GetParentNode()
{
    if (std::shared_ptr<Node> pNode = mParentNode.lock())
    {
        return pNode;
    }
    return nullptr;
}

std::string Node::GetParentNodeName()
{
    if (std::shared_ptr<Node> pNode = mParentNode.lock())
    {
        return pNode->GetNodeName();
    }
    return std::string("(invalid)");
}

std::vector<std::shared_ptr<Node>> Node::GetChilds()
{
    return mChildNodes;
}

std::vector<std::string> Node::GetChildNames()
{
    std::vector<std::string> childNames{};
    for (const auto& childNode : mChildNodes)
    {
        childNames.push_back(childNode->GetNodeName());
    }
    return childNames;
}
