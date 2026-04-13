#pragma once
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace RAnimation
{
    class Node : public std::enable_shared_from_this<Node>
    {
    public:
        static std::shared_ptr<Node> CreateNode(std::string nodeName);

        Node(std::string nodeName);

        std::shared_ptr<Node> AddChild(std::string childName);
        void AddChilds(std::vector<std::string> childNodes);

        void SetTranslation(glm::vec3 translation);
        void SetRotation(glm::quat rotation);
        void SetScaling(glm::vec3 scaling);
        void SetLocalTransform(glm::mat4 transform);
        void ResetToBindPose();

        void SetRootTransformMatrix(glm::mat4 matrix);

        void UpdateTRSMatrix();
        glm::mat4 GetTRSMatrix();

        std::string GetNodeName();
        std::shared_ptr<Node> GetParentNode();
        std::string GetParentNodeName();

        std::vector<std::shared_ptr<Node>> GetChilds();
        std::vector<std::string> GetChildNames();

    private:
        std::string mNodeName = "(invalid)";
        std::weak_ptr<Node> mParentNode{};
        std::vector<std::shared_ptr<Node>> mChildNodes{};

        glm::vec3 mTranslation = glm::vec3(0.0f);
        glm::quat mRotation = glm::identity<glm::quat>();
        glm::vec3 mScaling = glm::vec3(1.0f);

        glm::vec3 mBindTranslation = glm::vec3(0.0f);
        glm::quat mBindRotation = glm::identity<glm::quat>();
        glm::vec3 mBindScaling = glm::vec3(1.0f);

        glm::mat4 mTranslationMatrix = glm::mat4(1.0f);
        glm::mat4 mRotationMatrix = glm::mat4(1.0f);
        glm::mat4 mScalingMatrix = glm::mat4(1.0f);

        glm::mat4 mParentGlobalMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalTRSMatrix = glm::mat4(1.0f);

        /* extra matrix to move model instances around */
        glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);
    };
} // namespace RAnimation
