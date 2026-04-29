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

    mModel = model;

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
    mLocalTransformMatrix = mLocalTranslationMatrix * mLocalRotationMatrix * mLocalSwapAxisMatrix * mLocalScaleMatrix;
    mInstanceRootMatrix = mLocalTransformMatrix * mModelRootMatrix;
}

void ModelInstance::UpdateAnimation(float deltaTime)
{
    const auto& animClips = mModel->GetAnimClips();
    if (animClips.empty())
    {
        return;
    }

    if (mInstanceSettings.mAnimClipNr >= animClips.size())
    {
        fmt::print(stderr,
                   "{} warning: invalid animation clip index {} for model '{}', clamping to 0\n",
                   __FUNCTION__,
                   mInstanceSettings.mAnimClipNr,
                   mModel->GetModelFileName());
        mInstanceSettings.mAnimClipNr = 0;
        mInstanceSettings.mAnimPlayTimePos = 0.0f;
    }

    const float clipTicksPerSecond =
            animClips.at(mInstanceSettings.mAnimClipNr)->GetClipTicksPerSecond() > 0.0f
                    ? animClips.at(mInstanceSettings.mAnimClipNr)->GetClipTicksPerSecond()
                    : 25.0f;
    const float clipDuration = animClips.at(mInstanceSettings.mAnimClipNr)->GetClipDuration();

    mInstanceSettings.mAnimPlayTimePos += deltaTime * clipTicksPerSecond * mInstanceSettings.mAnimSpeedFactor;
    if (clipDuration > 0.0f)
    {
        mInstanceSettings.mAnimPlayTimePos = std::fmod(mInstanceSettings.mAnimPlayTimePos, clipDuration);
    }
    else
    {
        mInstanceSettings.mAnimPlayTimePos = 0.0f;
    }

    const std::vector<std::shared_ptr<AnimChannel>>& animChannels =
            animClips.at(mInstanceSettings.mAnimClipNr)->GetChannels();

    for (const auto& node : mModel->GetNodeList())
    {
        node->ResetToBindPose();
    }

    /* animate clip via channels */
    for (const auto& channel : animChannels)
    {
        std::string nodeNameToAnimate = channel->GetTargetNodeName();
        const auto nodeIter = mModel->GetNodeMap().find(nodeNameToAnimate);
        if (nodeIter == mModel->GetNodeMap().end())
        {
            fmt::print(stderr, "{} warning: animation channel targets missing node '{}'\n", __FUNCTION__, nodeNameToAnimate);
            continue;
        }

        std::shared_ptr<Node> node = nodeIter->second;

        if (channel->HasRotationKeys())
        {
            node->SetRotation(channel->GetRotation(mInstanceSettings.mAnimPlayTimePos));
        }

        if (channel->HasScalingKeys())
        {
            node->SetScaling(channel->GetScaling(mInstanceSettings.mAnimPlayTimePos));
        }

        if (channel->HasTranslationKeys())
        {
            node->SetTranslation(channel->GetTranslation(mInstanceSettings.mAnimPlayTimePos));
        }
    }

    /* apply only the per-instance transform here; the root node already stores the imported local transform */
    mModel->GetRootNode()->SetRootTransformMatrix(mLocalTransformMatrix);

    /* flat node map contains nodes in parent->child order, starting with root node, update matrices down the skeleton
     * tree */
    for (auto& node : mModel->GetNodeList())
    {
        node->UpdateTRSMatrix();
    }

    mBoneMatrices.assign(mModel->GetBoneList().size(), glm::mat4(1.0f));
    for (const auto& bone : mModel->GetBoneList())
    {
        const auto nodeIter = mModel->GetNodeMap().find(bone->GetBoneName());
        if (nodeIter == mModel->GetNodeMap().end())
        {
            fmt::print(stderr,
                       "{} warning: bone '{}' has no matching node in model '{}'\n",
                       __FUNCTION__,
                       bone->GetBoneName(),
                       mModel->GetModelFileName());
            continue;
        }

        mBoneMatrices.at(bone->GetBoneId()) = nodeIter->second->GetTRSMatrix() * bone->GetOffsetMatrix();
    }
}
