#include <Model/Node.h>
#include <fmt/base.h>

using namespace RAnimation;

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
    mTranslationMatrix = glm::translate(glm::mat4(1.0f), mTranslation);
}

void Node::SetRotation(glm::quat rotation)
{
    mRotation = rotation;
    mRotationMatrix = glm::mat4_cast(mRotation);
}

void Node::SetScaling(glm::vec3 scaling)
{
    mScaling = scaling;
    mScalingMatrix = glm::scale(glm::mat4(1.0f), mScaling);
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

    mLocalTRSMatrix = mRootTransformMatrix * mParentGlobalMatrix * mTranslationMatrix * mRotationMatrix * mScalingMatrix;
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
