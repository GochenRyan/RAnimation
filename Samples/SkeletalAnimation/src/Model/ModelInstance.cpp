#include <fmt/base.h>
#include <fmt/color.h>
#include <glm/glm.hpp>

#include <Model/ModelInstance.h>

using namespace RAnimation;

ModelInstance::ModelInstance(std::shared_ptr<Model> model, glm::vec3 position, glm::vec3 rotation, float modelScale)
{
    if (!model)
    {
        fmt::print(stderr, "{} error: invalid model given\n", __FUNCTION__);
        return;
    }
    mInstanceSettings.mWorldPosition = position;
    mInstanceSettings.mWorldRotation = rotation;
    mInstanceSettings.mScale = modelScale;

    /* we need one 4x4 matrix for every bone */
    mBoneMatrices.resize(mModel->GetBoneList().size());
    std::fill(mBoneMatrices.begin(), mBoneMatrices.end(), glm::mat4(1.0f));

    /* save model root matrix */
    mModelRootMatrix = mModel->GetRootTranformationMatrix();

    UpdateModelRootMatrix();
}

std::shared_ptr<Model> ModelInstance::GetModel()
{
    return mModel;
}

glm::vec3 ModelInstance::GetWorldPosition()
{
    return mInstanceSettings.mWorldPosition;
}

glm::mat4 ModelInstance::GetWorldTransformMatrix()
{
    return mInstanceRootMatrix;
}

void ModelInstance::SetTranslation(glm::vec3 position)
{
    mInstanceSettings.mWorldPosition = position;
    UpdateModelRootMatrix();
}

void ModelInstance::SetRotation(glm::vec3 rotation)
{
    mInstanceSettings.mWorldRotation = rotation;
    UpdateModelRootMatrix();
}

void ModelInstance::SetScale(float scale)
{
    mInstanceSettings.mScale = scale;
    UpdateModelRootMatrix();
}

void ModelInstance::SetSwapYZAxis(bool value)
{
    mInstanceSettings.mSwapYZAxis = value;
    UpdateModelRootMatrix();
}

glm::vec3 ModelInstance::GetTranslation()
{
    return mInstanceSettings.mWorldPosition;
}

glm::vec3 ModelInstance::GetRotation()
{
    return mInstanceSettings.mWorldRotation;
}

float ModelInstance::GetScale()
{
    return mInstanceSettings.mScale;
}

bool ModelInstance::GetSwapYZAxis()
{
    return mInstanceSettings.mSwapYZAxis;
}

std::vector<glm::mat4> ModelInstance::GetBoneMatrices()
{
    return mBoneMatrices;
}

void ModelInstance::SetInstanceSettings(InstanceSettings settings)
{
    mInstanceSettings = settings;
    UpdateModelRootMatrix();
}

InstanceSettings ModelInstance::GetInstanceSettings()
{
    return mInstanceSettings;
}

void ModelInstance::UpdateModelRootMatrix()
{
    if (!mModel)
    {
        fmt::print(stderr, "{} error: invalid model\n", __FUNCTION__);
        return;
    }

    mLocalScaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(mInstanceSettings.mScale));

    if (mInstanceSettings.mSwapYZAxis)
    {
        glm::mat4 flipMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        mLocalSwapAxisMatrix = glm::rotate(flipMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    else
    {
        mLocalSwapAxisMatrix = glm::mat4(1.0f);
    }

    glm::vec3 radians = glm::radians(mInstanceSettings.mWorldRotation);
    glm::quat q = glm::quat(radians);
    mLocalRotationMatrix = glm::mat4_cast(q);

    mLocalTranslationMatrix = glm::translate(glm::mat4(1.0f), mInstanceSettings.mWorldPosition);
    mInstanceRootMatrix = mLocalTransformMatrix * mModelRootMatrix;
}

void ModelInstance::UpdateAnimation(float deltaTime)
{
    mInstanceSettings.mAnimPlayTimePos +=
            deltaTime * mModel->GetAnimClips().at(mInstanceSettings.mAnimClipNr)->GetClipTicksPerSecond() *
            mInstanceSettings.mAnimSpeedFactor;
    mInstanceSettings.mAnimPlayTimePos =
            std::fmod(mInstanceSettings.mAnimPlayTimePos,
                      mModel->GetAnimClips().at(mInstanceSettings.mAnimClipNr)->GetClipDuration());

    std::vector<std::shared_ptr<AnimChannel>> animChannels =
            mModel->GetAnimClips().at(mInstanceSettings.mAnimClipNr)->GetChannels();

    /* animate clip via channels */
    for (const auto& channel : animChannels)
    {
        std::string nodeNameToAnimate = channel->GetTargetNodeName();
        std::shared_ptr<Node> node = mModel->GetNodeMap().at(nodeNameToAnimate);

        node->SetRotation(channel->GetRotation(mInstanceSettings.mAnimPlayTimePos));
        node->SetScaling(channel->GetScaling(mInstanceSettings.mAnimPlayTimePos));
        node->SetTranslation(channel->GetTranslation(mInstanceSettings.mAnimPlayTimePos));
    }

    /* set root node transform matrix, enabling instance movement */
    mModel->GetRootNode()->SetRootTransformMatrix(mLocalTransformMatrix * mModel->GetRootTranformationMatrix());

    /* flat node map contains nodes in parent->child order, starting with root node, update matrices down the skeleton
     * tree */
    mBoneMatrices.clear();
    for (auto& node : mModel->GetNodeList())
    {
        std::string nodeName = node->GetNodeName();

        node->UpdateTRSMatrix();
        if (mModel->GetInverseBindMatrices().count(nodeName) > 0)
        {
            mBoneMatrices.emplace_back(mModel->GetNodeMap().at(nodeName)->GetTRSMatrix() *
                                       mModel->GetInverseBindMatrices().at(nodeName));
        }
    }
}
