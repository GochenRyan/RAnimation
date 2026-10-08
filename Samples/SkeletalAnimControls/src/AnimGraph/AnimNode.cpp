#include <AnimGraph/AnimNode.h>

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
