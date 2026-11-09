#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Collision/CollisionProfile.h>
#include <Collision/CollisionWorld.h>
#include <Tools/DebugLines.h>

struct ModelAndInstanceData;

namespace RAnimation
{
    class Model;
    class ModelInstance;

    // Per-frame timing of the whole collision stage (CollisionWorld's numbers plus the pose work this
    // layer does). Microseconds; one frame.
    struct CollisionFrameTiming
    {
        double poseUs = 0.0;     // EnsureCpuPose + ComputeNodeGlobals + shape poses, all instances
        double writeUs = 0.0;    // CollisionWorld::SetBodyPose total
        double updateUs = 0.0;   // PhysicsSystem::Update
        double resolveUs = 0.0;  // CollisionWorld::Resolve
        double drawUs = 0.0;     // debug line generation (0 when both toggles are off)
        bool usedThreadPool = false;
        uint32_t bodies = 0;
        uint32_t manifolds = 0;
        uint32_t pushes = 0;
    };

    // Character-vs-character collision on top of CollisionWorld: maps ModelInstances to body slots, feeds the
    // animated joints into the compound shapes, turns manifolds into per-instance pushes and builds the debug
    // lines. Lives in Application; the renderer only sees the resulting DebugLineList.
    class CollisionSystem final
    {
    public:
        struct Settings
        {
            bool enabled = true;
            bool pushApart = true;   // apply Resolve() deltas to the instances (half to each side)
            bool showAABB = false;   // body bounds (white; red when the broad phase pairs it with another body)
            bool showShapes = false; // sub-shape wireframes (white; red when the shape has a manifold) + penetration vectors
            float lineBudgetFraction = 1.0f; // unused today; reserved for LOD of the wireframes
        };

        bool Init(uint32_t maxBodies, const CollisionThreadingPolicy& threading, std::string& outError);
        void Shutdown();
        bool IsInitialized() const { return mWorld.IsInitialized(); }

        // Per frame, after AnimationSystem::Update and before the renderer reads instance transforms:
        //   Sync -> UpdateBodies -> Step -> Resolve -> (caller applies pushes) -> BuildDebugLines
        // Sync reconciles bodies with scene.miModelInstances (adds / removes), loading each model's
        // collision profile on first sight. Models without a profile simply do not collide.
        void Sync(ModelAndInstanceData& scene);
        void UpdateBodies(ModelAndInstanceData& scene);
        void Step(float deltaTime);
        // Pushes already mapped to instances (CollisionPush::slot is the body slot; use InstanceForSlot).
        std::span<const CollisionPush> Resolve();
        ModelInstance* InstanceForSlot(uint32_t slot) const;
        // World-space bounds of the instance's body after UpdateBodies (false when the instance has no body).
        bool GetInstanceBounds(ModelInstance* instance, glm::vec3& outMin, glm::vec3& outMax) const;

        // Clears and refills `out` according to Settings::showAABB / showShapes.
        void BuildDebugLines(DebugLineList& out);

        Settings& GetSettings() { return mSettings; }
        const Settings& GetSettings() const { return mSettings; }
        CollisionWorld& World() { return mWorld; }
        const CollisionWorld& World() const { return mWorld; }
        const CollisionFrameTiming& LastTiming() const { return mTiming; }
        uint32_t BodyCount() const { return mWorld.BodyCount(); }
        uint32_t LinesDropped() const { return mLinesDropped; }

        // Status text per model ("18 shapes" / "no profile" / an error), for the UI.
        const std::map<const Model*, std::string>& ModelStatus() const { return mModelStatus; }

        // Exposed for the benchmark: how many microseconds the last UpdateBodies spent on CPU poses.
        static constexpr uint32_t kMaxLineVertices = 1u << 17;

    private:
        struct ModelEntry
        {
            std::shared_ptr<CollisionProfile> profile; // null when the model has no usable profile
            std::string status;
        };
        struct InstanceEntry
        {
            uint32_t slot = 0;
            std::shared_ptr<Model> model;
            const ModelEntry* modelEntry = nullptr;
            std::vector<CollisionShapePose> worldPoses;    // last frame's shape poses (debug drawing)
            std::vector<const CollisionShapeDesc*> shapes; // profile shapes with nodeIndex >= 0, body order
            uint32_t frameSeen = 0;
        };

        const ModelEntry& EnsureModel(const std::shared_ptr<Model>& model);
        void AppendShapeWireframe(DebugLineList& out, const CollisionShapeDesc& shape, const CollisionShapePose& pose, const glm::vec3& color);

        CollisionWorld mWorld;
        Settings mSettings;
        CollisionFrameTiming mTiming;
        std::map<const Model*, ModelEntry> mModels;
        std::map<ModelInstance*, InstanceEntry> mInstances;
        std::vector<ModelInstance*> mSlotToInstance;
        std::vector<uint32_t> mFreeSlots;
        std::vector<glm::mat4> mScratchGlobals;
        std::vector<CollisionShapePose> mScratchPoses;
        std::map<const Model*, std::string> mModelStatus;
        uint32_t mFrame = 0;
        uint32_t mLinesDropped = 0;
    };
} // namespace RAnimation
