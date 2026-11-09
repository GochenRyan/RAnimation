#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Collision/CollisionProfile.h>

namespace RAnimation
{
    // How PhysicsSystem::Update is scheduled. Two job systems stay resident; Auto picks the thread pool only
    // once the body count reaches joltThreshold (measured with --collision-bench, see CLAUDE.md).
    struct CollisionThreadingPolicy
    {
        enum class Mode : uint8_t
        {
            Auto,
            Single,
            Pool
        };
        Mode mode = Mode::Auto;
        int joltThreads = 0;      // thread pool size; 0 = hardware_concurrency - 1
        int joltThreshold = 32;   // Auto: bodies >= threshold -> thread pool (crossover from --collision-bench, 2026-11-09)
    };

    // World-space pose of one sub shape for SetBodyPose (same order as the profile's shapes with nodeIndex >= 0).
    struct CollisionShapePose
    {
        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    // Per-body depenetration from Resolve: the sum over this body's manifolds of half the penetration vector,
    // with the vertical component removed (the floor stays the controller's business).
    struct CollisionPush
    {
        uint32_t slot = 0;
        glm::vec3 delta = glm::vec3(0.0f);
    };

    // One manifold as seen by the contact listener (debug drawing only).
    struct CollisionContactDebug
    {
        uint32_t slotA = 0, slotB = 0;
        uint8_t shapeA = 0, shapeB = 0;
        glm::vec3 point = glm::vec3(0.0f);   // world-space contact point on A
        glm::vec3 normal = glm::vec3(0.0f);  // from A towards B
        float depth = 0.0f;
    };

    struct CollisionStepTiming
    {
        double writeUs = 0.0;    // SetBodyPose total (ModifyShapes + NotifyShapeChanged)
        double updateUs = 0.0;   // PhysicsSystem::Update
        double resolveUs = 0.0;  // Resolve
        bool usedThreadPool = false;
        uint32_t manifolds = 0;  // contact manifolds reported this step (depth > 0 only)
        uint32_t bodies = 0;
    };

    // Jolt behind a pimpl: one kinematic body per slot, each a MutableCompoundShape built from a
    // CollisionProfile. Instance-agnostic so the self test and the benchmark can drive it directly;
    // CollisionSystem maps ModelInstances onto slots. No Jolt type leaks through this header.
    class CollisionWorld final
    {
    public:
        CollisionWorld();
        ~CollisionWorld();
        CollisionWorld(const CollisionWorld&) = delete;
        CollisionWorld& operator=(const CollisionWorld&) = delete;

        bool Init(uint32_t maxBodies, const CollisionThreadingPolicy& threading, std::string& outError);
        void Shutdown();
        bool IsInitialized() const;

        // Bodies are addressed by caller-chosen slots in [0, maxBodies). Shapes with nodeIndex < 0 are skipped;
        // the body keeps the remaining shapes in profile order.
        bool AddBody(uint32_t slot, const CollisionProfile& profile, const glm::vec3& position, std::string& outError);
        void RemoveBody(uint32_t slot);
        bool HasBody(uint32_t slot) const;
        uint32_t BodyCount() const;
        uint32_t ShapeCount(uint32_t slot) const;
        void OptimizeBroadPhase();

        // Per frame, in this order. SetBodyPose takes world-space sub-shape poses; the body itself sits at
        // bodyPosition with identity rotation, so the compound's bounds are the exact union of the shapes'.
        void SetBodyPose(uint32_t slot, const glm::vec3& bodyPosition, std::span<const CollisionShapePose> worldShapePoses);
        void Step(float deltaTime);
        std::span<const CollisionPush> Resolve();

        // Debug / telemetry (valid until the next Step)
        bool GetBodyBounds(uint32_t slot, glm::vec3& outMin, glm::vec3& outMax) const;
        bool BroadPhaseOverlapsOthers(uint32_t slot) const;   // BroadPhaseQuery::CollideAABox against the body's bounds
        uint32_t ContactShapeMask(uint32_t slot) const;        // bit i = shape i had a manifold with depth > 0
        std::span<const CollisionContactDebug> Contacts() const;
        const CollisionStepTiming& LastTiming() const;

        CollisionThreadingPolicy& Threading();
        const CollisionThreadingPolicy& Threading() const;
        int ThreadPoolSize() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
} // namespace RAnimation
