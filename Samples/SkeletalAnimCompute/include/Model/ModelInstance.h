#pragma once

#include <memory>

#include <Model/InstanceSettings.h>
#include <Model/Model.h>

namespace RAnimation
{
    class ModelInstance final
    {
    public:
        ModelInstance(std::shared_ptr<Model> model,
                      glm::vec3 position = glm::vec3(0.0f),
                      glm::vec3 rotation = glm::vec3(0.0f),
                      float modelScale = 1.0f);
        std::shared_ptr<Model> GetModel();
        glm::vec3 GetWorldPosition();
        glm::mat4 GetWorldTransformMatrix();

        void SetTranslation(glm::vec3 position);
        void SetRotation(glm::vec3 rotation);
        void SetScale(float scale);
        void SetSwapYZAxis(bool value);

        glm::vec3 GetTranslation();
        glm::vec3 GetRotation();
        float GetScale();
        bool GetSwapYZAxis();

        std::vector<glm::mat4> GetBoneMatrices();

        void SetInstanceSettings(InstanceSettings settings);
        InstanceSettings GetInstanceSettings();

        void UpdateModelRootMatrix();
        void UpdateAnimation(float deltaTime);

    private:
        std::shared_ptr<Model> mModel = nullptr;

        InstanceSettings mInstanceSettings{};

        glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalScaleMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalSwapAxisMatrix = glm::mat4(1.0f);

        glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);

        glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
        glm::mat4 mModelRootMatrix = glm::mat4(1.0f);

        std::vector<glm::mat4> mBoneMatrices{};
    };
} // namespace RAnimation