#include <AnimGraph/AnimGraphInstance.h>

#include <chrono>
#include <cmath>
#include <unordered_map>

#include <AnimGraph/AnimGraphAsset.h>
#include <AnimGraph/AnimNode.h>
#include <AnimGraph/GraphBaker.h>
#include <AnimGraph/GraphCpuEvaluator.h>
#include <AnimGraph/GraphRunner.h>
#include <AnimGraph/HybridEvaluator.h>
#include <Model/Model.h>

namespace RAnimation
{
    AnimGraphInstance::AnimGraphInstance() = default;
    AnimGraphInstance::~AnimGraphInstance() = default;

    AnimNode* AnimGraphInstance::GetNode(int id) const
    {
        return mBaked ? mBaked->Get(id) : nullptr;
    }

    std::string_view AnimGraphInstance::EvaluatorName() const
    {
        return mEvaluator ? mEvaluator->Name() : "none";
    }

    Pose AnimGraphInstance::AllocPose()
    {
        Pose p;
        p.count = mNodeCount;
        p.bones = mStack.Alloc<Transform>(mNodeCount);
        return p;
    }

    bool AnimGraphInstance::Rebuild(std::shared_ptr<AnimGraphAsset> asset,
                                    const Model& model,
                                    EvalMode mode,
                                    float seedPhase,
                                    float seedGlobalTimeSec,
                                    std::string& outError)
    {
        // Carry node clocks across a rebuild of the same asset (parameter tweaks must not restart clips).
        std::unordered_map<int, float> previousTimes;
        const bool sameAsset = mBaked && mAsset == asset;
        if (sameAsset)
        {
            for (int id = 0; id < static_cast<int>(mBaked->Storage.size()); ++id)
            {
                if (auto* clip = dynamic_cast<ClipPlayer*>(mBaked->Get(id)))
                {
                    previousTimes[id] = clip->Time;
                }
            }
        }

        mError.clear();
        mEvaluator.reset();
        mBaked.reset();
        mPlan = ExecPlan{};
        mAsset = std::move(asset);
        mModel = &model;
        mMode = mode;
        mNodeCount = static_cast<uint32_t>(model.GetNodeList().size());
        mWork.Clear();
        mWork.nodeCount = mNodeCount;

        if (!mAsset)
        {
            mError = "no graph asset";
            outError = mError;
            return false;
        }

        mBaked = GraphBaker::Bake(mAsset->RuntimeDesc, model, mError);
        if (!mBaked)
        {
            outError = mError;
            return false;
        }

        InitContext init;
        init.Instance = this;
        for (int id : mBaked->TopoOrder)
        {
            init.SetNodeId(id);
            mBaked->Get(id)->Initialize_AnyThread(init);
        }

        for (int id = 0; id < static_cast<int>(mBaked->Storage.size()); ++id)
        {
            auto* clip = dynamic_cast<ClipPlayer*>(mBaked->Get(id));
            if (clip == nullptr)
            {
                continue;
            }
            const auto prev = previousTimes.find(id);
            if (prev != previousTimes.end())
            {
                clip->Time = prev->second;
            }
            else
            {
                // Seed from the shared track clock + instance phase so A/B/C start in step (C quantises the
                // phase into buckets, so the residual difference is at most one bucket).
                const float duration = clip->DurationSec();
                clip->Time = duration > 0.0f ? std::fmod(seedGlobalTimeSec * clip->PlayRate + seedPhase * duration, duration) : 0.0f;
            }
            clip->NormalizeTime();
        }

        mPlan = AnalyzeExecPlan(mAsset->RuntimeDesc);

        switch (mode)
        {
        case EvalMode::PerCharacter:
        case EvalMode::PerNpc:
            mEvaluator = std::make_unique<HybridEvaluator>(*this);
            break;
        case EvalMode::Tracks: // per-instance graphs are not evaluated in C; keep the CPU evaluator for the camera pose
        case EvalMode::Cpu:
        default:
            mEvaluator = std::make_unique<GraphCpuEvaluator>(*this);
            break;
        }
        if (!mEvaluator->Usable())
        {
            mError = mEvaluator->Why();
            outError = mError;
            return false;
        }
        return true;
    }

    void AnimGraphInstance::Tick(float deltaTime)
    {
        NumUpdateVisits = 0;
        NumEvaluateVisits = 0;
        if (!mEvaluator)
        {
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        mStack.Reset();
        mEvaluator->Tick(deltaTime, mWork);
        if (!mEvaluator->Usable())
        {
            mError = mEvaluator->Why();
        }
        LastTickMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    bool AnimGraphInstance::EvaluateFullPose(std::vector<Transform>& outPose)
    {
        if (!mBaked)
        {
            return false;
        }
        outPose.resize(mNodeCount);
        Pose pose;
        pose.bones = outPose.data();
        pose.count = mNodeCount;
        // The frame arena is only used for intermediate poses; the Work data lives in its own vectors, so
        // resetting here is safe even after Tick().
        mStack.Reset();
        GraphRunner::DrainCachedPoses(*this);
        GraphRunner::EvaluateRoot(*this, pose);
        return true;
    }
} // namespace RAnimation
