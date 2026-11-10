#pragma once
#include <vector>

#include <AnimGraph/Pose.h>

namespace RAnimation
{
    class AnimGraphInstance;

    // Frame driver: Update (whole graph) -> drain cached poses -> Evaluate (on demand, prunable).
    class GraphRunner
    {
    public:
        static void Update(AnimGraphInstance& instance, float deltaTime);
        static void DrainCachedPoses(AnimGraphInstance& instance);
        // Evaluates one node into outPose (storage owned by the caller, nodeCount entries).
        static void EvaluateNode(AnimGraphInstance& instance, int nodeId, Pose& outPose);
        static void EvaluateRoot(AnimGraphInstance& instance, Pose& outPose);
        static void RunFrame(AnimGraphInstance& instance, float deltaTime, Pose& outPose);
    };
} // namespace RAnimation
