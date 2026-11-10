#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Model/InstanceSettings.h>
#include <Model/Model.h>
#include <AnimGraph/EvalMode.h>
#include <AnimGraph/Pose.h>

namespace RAnimation
{
    struct AnimGraphAsset;
    class AnimGraphInstance;

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
        glm::mat4 GetLocalTransformMatrix();

        void SetTranslation(glm::vec3 position);
        void SetRotation(glm::vec3 rotation);
        void SetScale(float scale);

        glm::vec3 GetTranslation();
        glm::vec3 GetRotation();
        float GetScale();

        std::vector<glm::mat4> GetBoneMatrices();

        void SetInstanceSettings(InstanceSettings settings);
        InstanceSettings GetInstanceSettings();

        ~ModelInstance();

        void UpdateModelRootMatrix();
        void UpdateAnimation(float deltaTime);
        void UpdateAnimationState(float deltaTime);

        // ---- AnimGraph ----------------------------------------------------------------------------
        // Authored graph asset (null = implicit single-clip graph driven by mAnimClipNr / mAnimSpeedFactor).
        void SetGraphAsset(std::shared_ptr<AnimGraphAsset> asset);
        std::shared_ptr<AnimGraphAsset> GetGraphAsset() const;
        AnimGraphInstance* GetGraph() const;
        // (Re)builds the per-instance graph for mode when the asset / mode / implicit params changed.
        // Returns null when the graph is unusable (see GetGraphError()).
        AnimGraphInstance* EnsureGraph(EvalMode mode, float trackGlobalTimeSec);
        const std::string& GetGraphError() const { return mGraphError; }
        void SetEffectiveEvalMode(EvalMode mode);
        EvalMode GetEffectiveEvalMode() const { return mEffectiveMode; }

        // Tier C: what the renderer actually draws for this instance (track clip + time).
        void SetTrackPoseSource(int clipIndex, float timeSec);
        int GetTrackClip() const { return mTrackClip; }

        // CPU pose of what is being drawn, evaluated on demand (camera follow, focus, CPU tier).
        void MarkCpuPoseDirty() { mCpuPoseDirty = true; }
        const std::vector<Transform>& EnsureCpuPose();
        Pose GetCpuPose();

    private:
        std::shared_ptr<Model> mModel = nullptr;

        InstanceSettings mInstanceSettings{};

        glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalScaleMatrix = glm::mat4(1.0f);

        glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);

        glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
        glm::mat4 mModelRootMatrix = glm::mat4(1.0f);

        std::vector<glm::mat4> mBoneMatrices{};

        std::shared_ptr<AnimGraphAsset> mGraphAsset;
        std::shared_ptr<AnimGraphAsset> mImplicitAsset;
        std::unique_ptr<AnimGraphInstance> mGraph;
        std::string mGraphError;
        bool mGraphDirty = true;
        uint64_t mGraphRevision = 0;
        int mImplicitClip = -1;
        float mImplicitRate = 0.0f;
        EvalMode mEffectiveMode = EvalMode::Cpu;

        std::vector<Transform> mCpuPose;
        bool mCpuPoseDirty = true;
        bool mTrackPoseActive = false;
        int mTrackClip = 0;
        float mTrackTimeSec = 0.0f;
    };
} // namespace RAnimation
