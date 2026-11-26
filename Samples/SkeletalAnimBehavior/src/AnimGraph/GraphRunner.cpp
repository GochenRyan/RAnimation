#include <AnimGraph/GraphRunner.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/AnimNode.h>
#include <AnimGraph/GraphBaker.h>
#include <AnimGraph/PoseBlend.h>
#include <MemStack/MemStack.h>
#include <Model/Model.h>

namespace RAnimation
{
    void GraphRunner::Update(AnimGraphInstance& instance, float deltaTime)
    {
        BakedGraph* baked = instance.Baked();
        if (baked == nullptr)
        {
            return;
        }
        UpdateContext ctx;
        ctx.Instance = &instance;
        ctx.DeltaTime = deltaTime;
        ctx.Weight = 1.0f;

        // Cached-pose producers are not reachable through PoseLinks from Root; drive them explicitly so their
        // subtrees advance their clocks exactly once per frame.
        for (SaveCachedPose* save : baked->SavedPoseNodes)
        {
            save->LastWeight = 1.0f;
            save->Update_AnyThread(ctx.WithNodeId(save->NodeId));
        }
        if (AnimNode* root = baked->Get(baked->RootNodeId))
        {
            ++instance.NumUpdateVisits;
            root->LastWeight = 1.0f;
            root->Update_AnyThread(ctx.WithNodeId(baked->RootNodeId));
        }
    }

    void GraphRunner::DrainCachedPoses(AnimGraphInstance& instance)
    {
        BakedGraph* baked = instance.Baked();
        if (baked == nullptr)
        {
            return;
        }
        for (SaveCachedPose* save : baked->SavedPoseNodes)
        {
            if (!save->bCacheValid)
            {
                save->EvaluateCache(instance);
            }
        }
    }

    void GraphRunner::EvaluateNode(AnimGraphInstance& instance, int nodeId, Pose& outPose)
    {
        AnimNode* node = instance.GetNode(nodeId);
        if (node == nullptr)
        {
            return;
        }
        PoseContext ctx(&instance, instance.Stack(), outPose);
        ctx.SetNodeId(nodeId);
        ++instance.NumEvaluateVisits;
        node->Evaluate_AnyThread(ctx);
    }

    void GraphRunner::EvaluateRoot(AnimGraphInstance& instance, Pose& outPose)
    {
        BakedGraph* baked = instance.Baked();
        if (baked == nullptr)
        {
            return;
        }
        EvaluateNode(instance, baked->RootNodeId, outPose);
    }

    void GraphRunner::RunFrame(AnimGraphInstance& instance, float deltaTime, Pose& outPose)
    {
        Update(instance, deltaTime);
        DrainCachedPoses(instance);
        EvaluateRoot(instance, outPose);
    }
} // namespace RAnimation
