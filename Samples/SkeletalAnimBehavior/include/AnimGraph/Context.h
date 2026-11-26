#pragma once

#include <AnimGraph/Pose.h>

namespace RAnimation
{
    class AnimGraphInstance;
    class MemStack;

    struct BaseContext
    {
        AnimGraphInstance* Instance = nullptr;
        int CurrentNodeId = -1;
        int PreviousNodeId = -1;

        void SetNodeId(int Id)
        {
            PreviousNodeId = CurrentNodeId;
            CurrentNodeId = Id;
        }
    };

    struct InitContext : BaseContext
    {
    };

    struct CacheBonesContext : BaseContext
    {
    };

    // Carries one output pose. Storage comes from the instance's MemStack and is allocated by the caller
    // before Evaluate.
    struct PoseContext : BaseContext
    {
        PoseContext(AnimGraphInstance* In, MemStack& InStack, const Pose& InOutPose) : Stack(InStack), OutPose(InOutPose)
        {
            Instance = In;
        }

        // Fork a child context with a fresh pose of the same size (allocated from the stack).
        PoseContext Child() const;

        MemStack& Stack;
        Pose OutPose;
    };

    // Carries dt and the accumulated weight of this branch. All derivations are const and return copies
    // (fork), never mutate the original.
    struct UpdateContext : BaseContext
    {
        UpdateContext FractionalWeight(float Multiplier) const
        {
            UpdateContext R = *this;
            R.Weight = Weight * Multiplier;
            return R;
        }
        UpdateContext WithNodeId(int Id) const
        {
            UpdateContext R = *this;
            R.SetNodeId(Id);
            return R;
        }
        float GetFinalBlendWeight() const { return Weight; }

        float DeltaTime = 0.0f;
        float Weight = 1.0f;
    };
} // namespace RAnimation
