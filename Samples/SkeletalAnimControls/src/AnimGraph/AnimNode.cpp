#include <AnimGraph/AnimNode.h>

#include <AnimGraph/BoneMask.h>

#include <algorithm>
#include <cmath>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/PoseBlend.h>
#include <MemStack/MemStack.h>
#include <Model/ClipSampling.h>
#include <Model/Model.h>

namespace RAnimation
{
    namespace
    {
        void FillBindPose(const PoseContext& C)
        {
            const Model* model = C.Instance != nullptr ? C.Instance->GetModel() : nullptr;
            if (model == nullptr)
            {
                return;
            }
            const std::vector<Transform>& bind = model->GetBindPose();
            for (uint32_t i = 0; i < C.OutPose.count && i < bind.size(); ++i)
            {
                C.OutPose.bones[i] = bind[i];
            }
        }
    } // namespace

    // ---- Root -------------------------------------------------------------------------------------------

    void Root::Update_AnyThread(const UpdateContext& C)
    {
        Result.Update(C);
    }

    void Root::Evaluate_AnyThread(PoseContext& C)
    {
        Result.Evaluate(C);
    }

    // ---- ClipPlayer -------------------------------------------------------------------------------------

    void ClipPlayer::Initialize_AnyThread(const InitContext& C)
    {
        Clip = nullptr;
        const Model* model = C.Instance != nullptr ? C.Instance->GetModel() : nullptr;
        if (model != nullptr && ClipIndex >= 0 && static_cast<size_t>(ClipIndex) < model->GetAnimClips().size())
        {
            Clip = model->GetAnimClips()[static_cast<size_t>(ClipIndex)].get();
        }
    }

    float ClipPlayer::DurationSec() const
    {
        return Clip != nullptr ? ClipDurationSeconds(*Clip) : 0.0f;
    }

    void ClipPlayer::NormalizeTime()
    {
        const float duration = DurationSec();
        if (duration <= 0.0f)
        {
            Time = 0.0f;
            return;
        }
        if (bLooping)
        {
            Time = std::fmod(Time, duration);
            if (Time < 0.0f)
            {
                Time += duration;
            }
        }
        else
        {
            Time = std::clamp(Time, 0.0f, duration);
        }
    }

    void ClipPlayer::Update_AnyThread(const UpdateContext& C)
    {
        Time += C.DeltaTime * PlayRate;
        NormalizeTime();
    }

    void ClipPlayer::Evaluate_AnyThread(PoseContext& C)
    {
        const Model* model = C.Instance != nullptr ? C.Instance->GetModel() : nullptr;
        if (model == nullptr || Clip == nullptr)
        {
            FillBindPose(C);
            return;
        }
        SampleClipPose(model->GetBindPose(),
                       *Clip,
                       model->GetChannelToNodeTable(static_cast<size_t>(ClipIndex)),
                       ClipSecondsToFrames(*Clip, Time),
                       C.OutPose.bones);
    }

    // ---- TwoWayBlend ------------------------------------------------------------------------------------

    void TwoWayBlend::Initialize_AnyThread(const InitContext& C)
    {
        A.Initialize(C);
        B.Initialize(C);
    }

    void TwoWayBlend::CacheBones_AnyThread(const CacheBonesContext& C)
    {
        A.CacheBones(C);
        B.CacheBones(C);
    }

    void TwoWayBlend::GatherLinks(std::vector<PoseLink*>& outLinks)
    {
        outLinks.push_back(&A);
        outLinks.push_back(&B);
    }

    void TwoWayBlend::Update_AnyThread(const UpdateContext& C)
    {
        const float alpha = std::clamp(Alpha, 0.0f, 1.0f);
        // Update always propagates, even at weight 0, so hidden branches keep advancing their clocks.
        A.Update(C.FractionalWeight(1.0f - alpha));
        B.Update(C.FractionalWeight(alpha));
    }

    void TwoWayBlend::Evaluate_AnyThread(PoseContext& C)
    {
        const float alpha = std::clamp(Alpha, 0.0f, 1.0f);
        if (alpha <= kSmallestRelevantWeight)
        {
            A.Evaluate(C);
            return;
        }
        if (1.0f - alpha <= kSmallestRelevantWeight)
        {
            B.Evaluate(C);
            return;
        }
        PoseContext a = C.Child();
        PoseContext b = C.Child();
        A.Evaluate(a);
        B.Evaluate(b);
        BlendPoses(a.OutPose, b.OutPose, alpha, C.OutPose);
    }

    // ---- BlendSpace1D -----------------------------------------------------------------------------------

    void BlendSpace1D::Initialize_AnyThread(const InitContext& C)
    {
        for (Sample& s : Samples)
        {
            s.Link.Initialize(C);
        }
    }

    void BlendSpace1D::CacheBones_AnyThread(const CacheBonesContext& C)
    {
        for (Sample& s : Samples)
        {
            s.Link.CacheBones(C);
        }
    }

    void BlendSpace1D::GatherLinks(std::vector<PoseLink*>& outLinks)
    {
        for (Sample& s : Samples)
        {
            outLinks.push_back(&s.Link);
        }
    }

    void BlendSpace1D::Resolve(int& OutLow, int& OutHigh, float& OutFrac) const
    {
        OutLow = OutHigh = 0;
        OutFrac = 0.0f;
        if (Samples.empty())
        {
            OutLow = OutHigh = -1;
            return;
        }
        const int last = static_cast<int>(Samples.size()) - 1;
        if (Input <= Samples.front().Position)
        {
            return;
        }
        if (Input >= Samples[static_cast<size_t>(last)].Position)
        {
            OutLow = OutHigh = last;
            return;
        }
        for (int i = 0; i < last; ++i)
        {
            const float p0 = Samples[static_cast<size_t>(i)].Position;
            const float p1 = Samples[static_cast<size_t>(i) + 1].Position;
            if (Input >= p0 && Input <= p1)
            {
                OutLow = i;
                OutHigh = i + 1;
                OutFrac = p1 > p0 ? (Input - p0) / (p1 - p0) : 0.0f;
                return;
            }
        }
    }

    void BlendSpace1D::Update_AnyThread(const UpdateContext& C)
    {
        int low, high;
        float frac;
        Resolve(low, high, frac);
        for (int i = 0; i < static_cast<int>(Samples.size()); ++i)
        {
            float w = 0.0f;
            if (i == low)
            {
                w += 1.0f - frac;
            }
            if (i == high)
            {
                w += frac;
            }
            Samples[static_cast<size_t>(i)].Link.Update(C.FractionalWeight(w));
        }
    }

    void BlendSpace1D::Evaluate_AnyThread(PoseContext& C)
    {
        int low, high;
        float frac;
        Resolve(low, high, frac);
        if (low < 0)
        {
            FillBindPose(C);
            return;
        }
        if (low == high || frac <= TwoWayBlend::kSmallestRelevantWeight)
        {
            Samples[static_cast<size_t>(low)].Link.Evaluate(C);
            return;
        }
        if (1.0f - frac <= TwoWayBlend::kSmallestRelevantWeight)
        {
            Samples[static_cast<size_t>(high)].Link.Evaluate(C);
            return;
        }
        PoseContext a = C.Child();
        PoseContext b = C.Child();
        Samples[static_cast<size_t>(low)].Link.Evaluate(a);
        Samples[static_cast<size_t>(high)].Link.Evaluate(b);
        BlendPoses(a.OutPose, b.OutPose, frac, C.OutPose);
    }

    // ---- AnimSlot -----------------------------------------------------------------------------------

    void AnimSlot::Initialize_AnyThread(const InitContext& C)
    {
        Base.Initialize(C);
        mInstance = C.Instance;
        Playing.clear();
    }

    bool AnimSlot::Play(const MontageDesc& request, const Model& model)
    {
        MontageDesc desc = request;
        if (desc.clipIndex < 0)
        {
            desc.clipIndex = model.FindClipIndex(desc.clipName);
        }
        if (desc.clipIndex < 0 || static_cast<size_t>(desc.clipIndex) >= model.GetAnimClips().size())
        {
            return false;
        }
        desc.durationSec = ClipDurationSeconds(*model.GetAnimClips()[static_cast<size_t>(desc.clipIndex)]);
        for (const MontagePlayback& p : Playing)
        {
            if (!p.Finished() && !p.BlendingOut() && p.Desc().priority > desc.priority)
            {
                return false; // something more important owns the slot
            }
        }
        for (MontagePlayback& p : Playing)
        {
            p.Stop(desc.blendInSec); // the old one fades out as fast as the new one fades in
        }
        Playing.emplace_back(desc);
        return true;
    }

    void AnimSlot::Stop(float blendOutSec)
    {
        for (MontagePlayback& p : Playing)
        {
            p.Stop(blendOutSec);
        }
    }

    bool AnimSlot::IsBlendingOut() const
    {
        const MontagePlayback* current = Current();
        return current != nullptr && current->BlendingOut();
    }

    const MontagePlayback* AnimSlot::Current() const
    {
        for (auto it = Playing.rbegin(); it != Playing.rend(); ++it)
        {
            if (!it->Finished())
            {
                return &*it;
            }
        }
        return nullptr;
    }

    float AnimSlot::MontageWeight() const
    {
        float sum = 0.0f;
        for (const MontagePlayback& p : Playing)
        {
            sum += p.Weight();
        }
        return std::min(1.0f, sum);
    }

    void AnimSlot::Update_AnyThread(const UpdateContext& C)
    {
        mCrossed.clear();
        for (MontagePlayback& p : Playing)
        {
            p.Advance(C.DeltaTime, &mCrossed);
        }
        Playing.erase(std::remove_if(Playing.begin(), Playing.end(), [](const MontagePlayback& p) { return p.Finished(); }),
                      Playing.end());
        if (mInstance != nullptr)
        {
            for (const std::string& name : mCrossed)
            {
                mInstance->DispatchNotify(SlotName, name);
            }
        }
        Base.Update(C.FractionalWeight(1.0f - MontageWeight()));
    }

    void AnimSlot::Evaluate_AnyThread(PoseContext& C)
    {
        const Model* model = C.Instance != nullptr ? C.Instance->GetModel() : nullptr;
        const float montageWeight = MontageWeight();
        if (model == nullptr || montageWeight <= TwoWayBlend::kSmallestRelevantWeight)
        {
            Base.Evaluate(C);
            return;
        }
        PoseAccumulator acc(C.OutPose);
        const float baseWeight = 1.0f - montageWeight;
        if (baseWeight > TwoWayBlend::kSmallestRelevantWeight && Base.IsLinked())
        {
            PoseContext base = C.Child();
            Base.Evaluate(base);
            acc.Add(base.OutPose, baseWeight);
        }
        for (const MontagePlayback& p : Playing)
        {
            if (p.Weight() <= TwoWayBlend::kSmallestRelevantWeight)
            {
                continue;
            }
            const AnimClip& clip = *model->GetAnimClips()[static_cast<size_t>(p.Desc().clipIndex)];
            PoseContext child = C.Child();
            SampleClipPose(model->GetBindPose(),
                           clip,
                           model->GetChannelToNodeTable(static_cast<size_t>(p.Desc().clipIndex)),
                           ClipSecondsToFrames(clip, p.Time()),
                           child.OutPose.bones);
            acc.Add(child.OutPose, p.Weight());
        }
        acc.Finish();
    }

    // ---- BlendSpace2D -------------------------------------------------------------------------------

    void BlendSpace2D::Initialize_AnyThread(const InitContext& C)
    {
        for (Sample& s : Samples)
        {
            s.Link.Initialize(C);
        }
    }

    void BlendSpace2D::CacheBones_AnyThread(const CacheBonesContext& C)
    {
        for (Sample& s : Samples)
        {
            s.Link.CacheBones(C);
        }
    }

    void BlendSpace2D::GatherLinks(std::vector<PoseLink*>& outLinks)
    {
        for (Sample& s : Samples)
        {
            outLinks.push_back(&s.Link);
        }
    }

    int BlendSpace2D::Resolve(GridWeight out[4]) const
    {
        std::vector<GridSample> positions;
        positions.reserve(Samples.size());
        for (const Sample& s : Samples)
        {
            positions.push_back(s.Position);
        }
        return ResolvePolarGrid(positions, InputX, InputY, bWrapX, WrapPeriod, out);
    }

    void BlendSpace2D::Update_AnyThread(const UpdateContext& C)
    {
        GridWeight weights[4];
        const int count = Resolve(weights);
        for (int i = 0; i < static_cast<int>(Samples.size()); ++i)
        {
            float w = 0.0f;
            for (int k = 0; k < count; ++k)
            {
                if (weights[k].index == i)
                {
                    w += weights[k].weight;
                }
            }
            Samples[static_cast<size_t>(i)].Link.Update(C.FractionalWeight(w));
        }
    }

    void BlendSpace2D::Evaluate_AnyThread(PoseContext& C)
    {
        GridWeight weights[4];
        int count = Resolve(weights);
        // drop samples that are unconnected or negligible
        int live = 0;
        for (int k = 0; k < count; ++k)
        {
            if (weights[k].weight > TwoWayBlend::kSmallestRelevantWeight && weights[k].index >= 0 &&
                static_cast<size_t>(weights[k].index) < Samples.size() && Samples[static_cast<size_t>(weights[k].index)].Link.IsLinked())
            {
                weights[live++] = weights[k];
            }
        }
        count = live;
        if (count == 0)
        {
            FillBindPose(C);
            return;
        }
        if (count == 1)
        {
            Samples[static_cast<size_t>(weights[0].index)].Link.Evaluate(C);
            return;
        }
        PoseAccumulator acc(C.OutPose);
        for (int k = 0; k < count; ++k)
        {
            PoseContext child = C.Child();
            Samples[static_cast<size_t>(weights[k].index)].Link.Evaluate(child);
            acc.Add(child.OutPose, weights[k].weight);
        }
        acc.Finish();
    }

    // ---- CrossfadeSelect ---------------------------------------------------------------------------

    void CrossfadeSelect::Initialize_AnyThread(const InitContext& C)
    {
        for (PoseLink& link : Inputs)
        {
            link.Initialize(C);
        }
        mTracker.Clear();
        mStarted = false;
        mSettled = true;
    }

    void CrossfadeSelect::CacheBones_AnyThread(const CacheBonesContext& C)
    {
        for (PoseLink& link : Inputs)
        {
            link.CacheBones(C);
        }
    }

    void CrossfadeSelect::GatherLinks(std::vector<PoseLink*>& outLinks)
    {
        for (PoseLink& link : Inputs)
        {
            outLinks.push_back(&link);
        }
    }

    int CrossfadeSelect::FindInput(std::string_view name) const
    {
        for (size_t i = 0; i < InputNames.size() && i < Inputs.size(); ++i)
        {
            if (!name.empty() && InputNames[i] == name)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void CrossfadeSelect::RewindClips(AnimNode* node, int depth)
    {
        if (node == nullptr || depth > 64)
        {
            return;
        }
        if (auto* clip = dynamic_cast<ClipPlayer*>(node))
        {
            clip->Time = 0.0f;
        }
        std::vector<PoseLink*> links;
        node->GatherLinks(links);
        for (PoseLink* link : links)
        {
            RewindClips(link->LinkedNode, depth + 1);
        }
    }

    bool CrossfadeSelect::ClipsDone(const AnimNode* node, int depth, bool& sawOneShot)
    {
        if (node == nullptr || depth > 64)
        {
            return true;
        }
        if (const auto* clip = dynamic_cast<const ClipPlayer*>(node))
        {
            if (clip->bLooping)
            {
                return true;
            }
            sawOneShot = true;
            return clip->Time >= clip->DurationSec() - 1e-4f;
        }
        std::vector<PoseLink*> links;
        const_cast<AnimNode*>(node)->GatherLinks(links);
        bool done = true;
        for (PoseLink* link : links)
        {
            done = ClipsDone(link->LinkedNode, depth + 1, sawOneShot) && done;
        }
        return done;
    }

    bool CrossfadeSelect::SetActive(int index, bool restart)
    {
        if (index < 0 || static_cast<size_t>(index) >= Inputs.size())
        {
            return false;
        }
        if (restart)
        {
            RewindClips(Inputs[static_cast<size_t>(index)].LinkedNode, 0);
        }
        ActiveIndex = index;
        mTracker.SetActive(index, /*seedFull*/ !mStarted);
        mSettled = false;
        return true;
    }

    bool CrossfadeSelect::SetActiveByName(std::string_view name, bool restart)
    {
        const int index = FindInput(name);
        return index >= 0 && SetActive(index, restart);
    }

    bool CrossfadeSelect::IsActiveDone() const
    {
        if (ActiveIndex < 0 || static_cast<size_t>(ActiveIndex) >= Inputs.size())
        {
            return false;
        }
        bool sawOneShot = false;
        const bool done = ClipsDone(Inputs[static_cast<size_t>(ActiveIndex)].LinkedNode, 0, sawOneShot);
        return sawOneShot && done; // a branch of looping clips never completes
    }

    void CrossfadeSelect::Update_AnyThread(const UpdateContext& C)
    {
        const int count = static_cast<int>(Inputs.size());
        const int wanted = count > 0 ? std::clamp(ActiveIndex, 0, count - 1) : -1;
        if (!mStarted)
        {
            // First update: the authored input is in play at full weight, no fade from nothing.
            mTracker.SetActive(wanted, /*seedFull*/ true);
            mStarted = true;
        }
        else if (wanted != mTracker.Active())
        {
            mTracker.SetActive(wanted, false); // parameter edited directly (editor / controller)
        }
        mSettled = mTracker.Step(C.DeltaTime, BlendTimeSec);
        // Every input keeps its clock running; weight 0 for the ones not in play.
        for (int i = 0; i < count; ++i)
        {
            Inputs[static_cast<size_t>(i)].Update(C.FractionalWeight(mTracker.WeightOf(i)));
        }
    }

    void CrossfadeSelect::Evaluate_AnyThread(PoseContext& C)
    {
        const auto& layers = mTracker.Layers();
        std::vector<const CrossfadeTracker::Layer*> live;
        for (const auto& l : layers)
        {
            if (l.weight > TwoWayBlend::kSmallestRelevantWeight && l.index >= 0 &&
                static_cast<size_t>(l.index) < Inputs.size() && Inputs[static_cast<size_t>(l.index)].IsLinked())
            {
                live.push_back(&l);
            }
        }
        if (live.empty())
        {
            // Nothing in play yet (Evaluate before the first Update): the authored input, or bind pose.
            if (ActiveIndex >= 0 && static_cast<size_t>(ActiveIndex) < Inputs.size() &&
                Inputs[static_cast<size_t>(ActiveIndex)].IsLinked())
            {
                Inputs[static_cast<size_t>(ActiveIndex)].Evaluate(C);
            }
            else
            {
                FillBindPose(C);
            }
            return;
        }
        if (live.size() == 1)
        {
            Inputs[static_cast<size_t>(live.front()->index)].Evaluate(C);
            return;
        }
        // Weighted sum with the same conventions as the kernel: hemisphere alignment towards the first
        // term, then per-node renormalisation by the total weight.
        PoseAccumulator acc(C.OutPose);
        for (const auto* l : live)
        {
            PoseContext child = C.Child();
            Inputs[static_cast<size_t>(l->index)].Evaluate(child);
            acc.Add(child.OutPose, l->weight);
        }
        acc.Finish();
    }

    // ---- LayeredBlend ------------------------------------------------------------------------------

    void LayeredBlend::Initialize_AnyThread(const InitContext& C)
    {
        Base.Initialize(C);
        Layer.Initialize(C);
        Mask.clear();
        const Model* model = C.Instance != nullptr ? C.Instance->GetModel() : nullptr;
        if (model != nullptr)
        {
            Mask = BuildBoneMask(model->GetNodeParentIndices(), model->FindNodeIndex(MaskRoot), BlendDepth);
        }
    }

    void LayeredBlend::CacheBones_AnyThread(const CacheBonesContext& C)
    {
        Base.CacheBones(C);
        Layer.CacheBones(C);
    }

    void LayeredBlend::GatherLinks(std::vector<PoseLink*>& outLinks)
    {
        outLinks.push_back(&Base);
        outLinks.push_back(&Layer);
    }

    void LayeredBlend::Update_AnyThread(const UpdateContext& C)
    {
        const float alpha = std::clamp(Alpha, 0.0f, 1.0f);
        // The base always shows through where the mask is 0, so it keeps the full weight; the layer
        // carries Alpha. Hidden layers keep advancing their clocks (weight 0 still updates).
        Base.Update(C);
        Layer.Update(C.FractionalWeight(alpha));
    }

    void LayeredBlend::Evaluate_AnyThread(PoseContext& C)
    {
        const float alpha = std::clamp(Alpha, 0.0f, 1.0f);
        if (alpha <= TwoWayBlend::kSmallestRelevantWeight || Mask.size() != C.OutPose.count || !Layer.IsLinked())
        {
            Base.Evaluate(C);
            return;
        }
        PoseContext base = C.Child();
        PoseContext layer = C.Child();
        Base.Evaluate(base);
        Layer.Evaluate(layer);
        for (uint32_t n = 0; n < C.OutPose.count; ++n)
        {
            C.OutPose[n] = BlendTransform(base.OutPose[n], layer.OutPose[n], alpha * Mask[n]);
        }
    }

    // ---- SaveCachedPose / UseCachedPose -----------------------------------------------------------------

    void SaveCachedPose::Initialize_AnyThread(const InitContext& C)
    {
        Source.Initialize(C);
        bCacheValid = false;
    }

    void SaveCachedPose::Update_AnyThread(const UpdateContext& C)
    {
        bCacheValid = false;
        Source.Update(C);
    }

    void SaveCachedPose::EvaluateCache(AnimGraphInstance& Instance)
    {
        Cached.resize(Instance.NodeCount());
        Pose p;
        p.bones = Cached.data();
        p.count = static_cast<uint32_t>(Cached.size());
        PoseContext ctx(&Instance, Instance.Stack(), p);
        ctx.SetNodeId(NodeId);
        Source.Evaluate(ctx);
        bCacheValid = true;
    }

    void SaveCachedPose::Evaluate_AnyThread(PoseContext& C)
    {
        if (!bCacheValid && C.Instance != nullptr)
        {
            EvaluateCache(*C.Instance);
        }
        Pose cached;
        cached.bones = Cached.data();
        cached.count = static_cast<uint32_t>(Cached.size());
        CopyPose(cached, C.OutPose);
    }

    void UseCachedPose::Evaluate_AnyThread(PoseContext& C)
    {
        if (Source == nullptr)
        {
            FillBindPose(C);
            return;
        }
        Source->Evaluate_AnyThread(C);
    }
} // namespace RAnimation
