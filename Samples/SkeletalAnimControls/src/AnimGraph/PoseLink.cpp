#include <AnimGraph/PoseLink.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/AnimNode.h>
#include <AnimGraph/PoseBlend.h>
#include <MemStack/MemStack.h>
#include <Model/Model.h>

namespace RAnimation
{
    PoseContext PoseContext::Child() const
    {
        Pose child;
        child.count = OutPose.count;
        child.bones = Stack.Alloc<Transform>(OutPose.count);
        PoseContext c(Instance, Stack, child);
        c.CurrentNodeId = CurrentNodeId;
        c.PreviousNodeId = PreviousNodeId;
        return c;
    }

    void PoseLink::Initialize(const InitContext& Ctx)
    {
        // Pointers are resolved by the baker (ids are GraphDesc indices); re-resolve defensively.
        if (LinkedNode == nullptr && Ctx.Instance != nullptr)
        {
            LinkedNode = Ctx.Instance->GetNode(LinkID);
        }
        bProcessed = false;
    }

    void PoseLink::CacheBones(const CacheBonesContext& Ctx)
    {
        if (LinkedNode != nullptr)
        {
            CacheBonesContext c = Ctx;
            c.SetNodeId(LinkID);
            LinkedNode->CacheBones_AnyThread(c);
        }
    }

    void PoseLink::Update(const UpdateContext& Ctx)
    {
        if (LinkedNode == nullptr)
        {
            return;
        }
        if (Ctx.Instance != nullptr)
        {
            ++Ctx.Instance->NumUpdateVisits;
        }
        LinkedNode->LastWeight = Ctx.GetFinalBlendWeight();
        LinkedNode->Update_AnyThread(Ctx.WithNodeId(LinkID));
    }

    void PoseLink::Evaluate(PoseContext& Ctx)
    {
        if (LinkedNode == nullptr)
        {
            // Unconnected pin: bind pose, so a half-built graph still shows something sensible.
            if (Ctx.Instance != nullptr && Ctx.Instance->GetModel() != nullptr)
            {
                const std::vector<Transform>& bind = Ctx.Instance->GetModel()->GetBindPose();
                for (uint32_t i = 0; i < Ctx.OutPose.count && i < bind.size(); ++i)
                {
                    Ctx.OutPose.bones[i] = bind[i];
                }
            }
            return;
        }
        if (Ctx.Instance != nullptr)
        {
            ++Ctx.Instance->NumEvaluateVisits;
        }
        Ctx.SetNodeId(LinkID);
        LinkedNode->Evaluate_AnyThread(Ctx);
        bProcessed = true;
    }
} // namespace RAnimation
