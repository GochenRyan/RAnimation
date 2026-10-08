// ModelInstance: per-instance AnimGraph ownership and the on-demand CPU pose (split out of ModelInstance.cpp).

#include <fmt/base.h>
#include <fmt/color.h>

#include <AnimGraph/AnimGraphAsset.h>
#include <AnimGraph/AnimGraphInstance.h>
#include <Model/ClipSampling.h>
#include <Model/ModelInstance.h>

using namespace RAnimation;

ModelInstance::~ModelInstance() = default;

void ModelInstance::SetGraphAsset(std::shared_ptr<AnimGraphAsset> asset)
{
    if (mGraphAsset == asset)
    {
        return;
    }
    mGraphAsset = std::move(asset);
    mGraphDirty = true;
    mCpuPoseDirty = true;
}

std::shared_ptr<AnimGraphAsset> ModelInstance::GetGraphAsset() const
{
    return mGraphAsset;
}

AnimGraphInstance* ModelInstance::GetGraph() const
{
    return mGraph.get();
}

AnimGraphInstance* ModelInstance::EnsureGraph(EvalMode mode, float trackGlobalTimeSec)
{
    if (!mModel || mModel->GetAnimClips().empty())
    {
        return nullptr;
    }

    // Instances without an authored graph run the implicit single-clip graph driven by the legacy
    // clip/speed settings; rebuild it when those change.
    std::shared_ptr<AnimGraphAsset> asset = mGraphAsset;
    if (!asset)
    {
        const int clip = static_cast<int>(mInstanceSettings.mAnimClipNr);
        const float rate = mInstanceSettings.mAnimSpeedFactor;
        if (!mImplicitAsset || mImplicitClip != clip || mImplicitRate != rate)
        {
            mImplicitAsset = AnimGraphAsset::MakeSingleClip(clip, rate, true);
            mImplicitClip = clip;
            mImplicitRate = rate;
            mGraphDirty = true;
        }
        asset = mImplicitAsset;
    }

    const bool needsRebuild = mGraphDirty || !mGraph || mGraph->Asset() != asset || mGraph->Mode() != mode ||
                              mGraphRevision != asset->Revision;
    if (!needsRebuild)
    {
        return mGraph->Usable() ? mGraph.get() : nullptr;
    }

    if (!mGraph)
    {
        mGraph = std::make_unique<AnimGraphInstance>();
    }
    std::string error;
    const bool ok = mGraph->Rebuild(asset, *mModel, mode, mInstanceSettings.mAnimPhase, trackGlobalTimeSec, error);
    mGraphDirty = false;
    mGraphRevision = asset->Revision;
    mCpuPoseDirty = true;
    if (!ok)
    {
        if (mGraphError != error)
        {
            fmt::print(stderr, fg(fmt::color::yellow), "ModelInstance: graph '{}' unusable: {}\n", asset->Name, error);
        }
        mGraphError = error;
        return nullptr;
    }
    mGraphError.clear();
    return mGraph.get();
}

void ModelInstance::SetTrackPoseSource(int clipIndex, float timeSec)
{
    mTrackPoseActive = true;
    mTrackClip = clipIndex;
    mTrackTimeSec = timeSec;
    mCpuPoseDirty = true;
}

void ModelInstance::SetEffectiveEvalMode(EvalMode mode)
{
    mEffectiveMode = mode;
    if (mode != EvalMode::Tracks)
    {
        mTrackPoseActive = false;
    }
}

const std::vector<Transform>& ModelInstance::EnsureCpuPose()
{
    if (!mCpuPoseDirty || !mModel)
    {
        return mCpuPose;
    }
    mCpuPoseDirty = false;

    const auto& clips = mModel->GetAnimClips();
    if (mTrackPoseActive && mTrackClip >= 0 && static_cast<size_t>(mTrackClip) < clips.size())
    {
        // Tier C: the drawn pose is the track's clip at the track's time.
        mCpuPose.resize(mModel->GetNodeList().size());
        const AnimClip& clip = *clips[static_cast<size_t>(mTrackClip)];
        SampleClipPose(mModel->GetBindPose(),
                       clip,
                       mModel->GetChannelToNodeTable(static_cast<size_t>(mTrackClip)),
                       ClipSecondsToFrames(clip, mTrackTimeSec),
                       mCpuPose.data());
        return mCpuPose;
    }

    if (mGraph && mGraph->Usable() && mGraph->EvaluateFullPose(mCpuPose))
    {
        return mCpuPose;
    }

    // No graph yet (e.g. focus right after import): fall back to the implicit single clip at its phase.
    if (!clips.empty())
    {
        const size_t clipIndex = std::min<size_t>(mInstanceSettings.mAnimClipNr, clips.size() - 1);
        const AnimClip& clip = *clips[clipIndex];
        mCpuPose.resize(mModel->GetNodeList().size());
        SampleClipPose(mModel->GetBindPose(),
                       clip,
                       mModel->GetChannelToNodeTable(clipIndex),
                       ClipSecondsToFrames(clip, mInstanceSettings.mAnimPhase * ClipDurationSeconds(clip)),
                       mCpuPose.data());
        return mCpuPose;
    }

    mCpuPose = mModel->GetBindPose();
    return mCpuPose;
}

Pose ModelInstance::GetCpuPose()
{
    const std::vector<Transform>& pose = EnsureCpuPose();
    Pose p;
    p.bones = const_cast<Transform*>(pose.data());
    p.count = static_cast<uint32_t>(pose.size());
    return p;
}
