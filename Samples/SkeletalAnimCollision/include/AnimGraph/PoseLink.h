#pragma once
#include <AnimGraph/Context.h>

namespace RAnimation
{
    class AnimNode;

    // Pose connection: an integer LinkID (= GraphDesc index) at bake time, resolved to a pointer at runtime.
    struct PoseLink
    {
        void Initialize(const InitContext& Ctx);
        void CacheBones(const CacheBonesContext& Ctx);
        void Update(const UpdateContext& Ctx);
        void Evaluate(PoseContext& Ctx);

        bool IsLinked() const { return LinkedNode != nullptr; }

        int LinkID = -1;
        AnimNode* LinkedNode = nullptr;
        bool bProcessed = false;
    };
} // namespace RAnimation
