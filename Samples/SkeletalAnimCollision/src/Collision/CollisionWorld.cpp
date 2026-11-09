#include <Collision/CollisionWorld.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>

#include <fmt/format.h>

// Jolt.h must come first; everything else follows the library's own include order.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>

namespace RAnimation
{
    namespace
    {
        // Process-wide Jolt bootstrap (allocator, factory, type registry), shared by every world.
        int gJoltUsers = 0;
        std::mutex gJoltBootstrapMutex;

        void JoltTrace(const char* fmtStr, ...)
        {
            va_list list;
            va_start(list, fmtStr);
            char buffer[1024];
            std::vsnprintf(buffer, sizeof(buffer), fmtStr, list);
            va_end(list);
            fmt::print("Jolt: {}\n", buffer);
        }

#ifdef JPH_ENABLE_ASSERTS
        bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            fmt::print(stderr, "Jolt assert {}:{}: ({}) {}\n", file, line, expression, message ? message : "");
            return true; // break
        }
#endif

        void AcquireJolt()
        {
            std::lock_guard<std::mutex> lock(gJoltBootstrapMutex);
            if (gJoltUsers++ == 0)
            {
                JPH::RegisterDefaultAllocator();
                JPH::Trace = JoltTrace;
                JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
        }

        void ReleaseJolt()
        {
            std::lock_guard<std::mutex> lock(gJoltBootstrapMutex);
            if (--gJoltUsers == 0)
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }

        inline JPH::Vec3 ToJph(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
        inline JPH::Quat ToJph(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
        inline glm::vec3 ToGlm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

        constexpr JPH::ObjectLayer kLayerCharacter = 0;
        constexpr uint32_t kMaxDebugContacts = 4096;
        constexpr uint32_t kMaxShapesPerBody = 32; // ContactShapeMask is a uint32 bit set

        inline double MicrosSince(std::chrono::steady_clock::time_point t0)
        {
            return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        }
    } // namespace

    struct CollisionWorld::Impl final : public JPH::ContactListener
    {
        struct Slot
        {
            bool used = false;
            JPH::BodyID body;
            uint32_t shapeCount = 0;
            std::vector<CollisionShapeDesc::Type> shapeTypes; // for debugging / telemetry
        };

        // Written from the contact callbacks (any worker thread), read on the main thread after Update.
        struct Accumulator
        {
            std::atomic<float> dx{0.0f}, dy{0.0f}, dz{0.0f};
            std::atomic<uint32_t> shapeMask{0};
            void Reset()
            {
                dx.store(0.0f, std::memory_order_relaxed);
                dy.store(0.0f, std::memory_order_relaxed);
                dz.store(0.0f, std::memory_order_relaxed);
                shapeMask.store(0, std::memory_order_relaxed);
            }
            void Add(const glm::vec3& v, uint32_t shapeBit)
            {
                dx.fetch_add(v.x, std::memory_order_relaxed);
                dy.fetch_add(v.y, std::memory_order_relaxed);
                dz.fetch_add(v.z, std::memory_order_relaxed);
                shapeMask.fetch_or(shapeBit, std::memory_order_relaxed);
            }
        };

        bool initialized = false;
        CollisionThreadingPolicy threading;
        int poolThreads = 0;

        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JPH::JobSystemSingleThreaded> jobsSingle;
        std::unique_ptr<JPH::JobSystemThreadPool> jobsPool;
        std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable> bpLayers;
        std::unique_ptr<JPH::ObjectLayerPairFilterTable> objectPairFilter;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> objectVsBpFilter;
        std::unique_ptr<JPH::PhysicsSystem> physics;

        std::vector<Slot> slots;
        std::vector<Accumulator> accum; // one per slot
        uint32_t bodyCount = 0;

        std::mutex contactMutex;
        std::vector<CollisionContactDebug> contacts;
        std::atomic<uint32_t> manifoldCount{0};

        std::vector<CollisionPush> pushes;
        CollisionStepTiming timing;

        // Scratch for SetBodyPose
        std::vector<JPH::Vec3> scratchPositions;
        std::vector<JPH::Quat> scratchRotations;

        ~Impl() override { ShutdownImpl(); }

        bool InitImpl(uint32_t maxBodies, const CollisionThreadingPolicy& policy, std::string& outError)
        {
            if (initialized)
            {
                outError = "CollisionWorld already initialized";
                return false;
            }
            if (maxBodies == 0)
            {
                outError = "maxBodies must be > 0";
                return false;
            }
            AcquireJolt();
            threading = policy;

            const int hw = static_cast<int>(std::thread::hardware_concurrency());
            poolThreads = policy.joltThreads > 0 ? policy.joltThreads : std::max(1, hw - 1);

            // PhysicsSystem::Update carves maxContactConstraints * ~1 KB out of the temp allocator every step
            // (ContactConstraintManager::PrepareConstraintBuffer), and the allocator returns null instead of
            // growing. Size both together: kinematic bodies never get a constraint solved, the budget only has to
            // cover the manifolds reported per step (18 shapes a side, a handful of neighbours).
            const JPH::uint maxBodyPairs = std::min<JPH::uint>(std::max<JPH::uint>(1024, maxBodies * 16), 65536);
            const JPH::uint maxContactConstraints = std::min<JPH::uint>(std::max<JPH::uint>(1024, maxBodies * 8), 16384);
            const size_t tempBytes = static_cast<size_t>(maxContactConstraints) * 2048 + 8 * 1024 * 1024;
            tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(static_cast<JPH::uint>(tempBytes));
            jobsSingle = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
            jobsPool = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, poolThreads);

            // One object layer and one broad-phase layer: characters only collide with characters.
            bpLayers = std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(1, 1);
            bpLayers->MapObjectToBroadPhaseLayer(kLayerCharacter, JPH::BroadPhaseLayer(0));
            objectPairFilter = std::make_unique<JPH::ObjectLayerPairFilterTable>(1);
            objectPairFilter->EnableCollision(kLayerCharacter, kLayerCharacter);
            objectVsBpFilter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(*bpLayers, 1, *objectPairFilter, 1);

            physics = std::make_unique<JPH::PhysicsSystem>();
            physics->Init(maxBodies, 0, maxBodyPairs, maxContactConstraints, *bpLayers, *objectVsBpFilter, *objectPairFilter);
            physics->SetGravity(JPH::Vec3::sZero());
            physics->SetContactListener(this);

            slots.assign(maxBodies, Slot{});
            accum = std::vector<Accumulator>(maxBodies);
            contacts.reserve(256);
            initialized = true;
            return true;
        }

        void ShutdownImpl()
        {
            if (!initialized)
            {
                return;
            }
            JPH::BodyInterface& bi = physics->GetBodyInterface();
            for (Slot& s : slots)
            {
                if (s.used)
                {
                    bi.RemoveBody(s.body);
                    bi.DestroyBody(s.body);
                    s = Slot{};
                }
            }
            bodyCount = 0;
            physics.reset();
            objectVsBpFilter.reset();
            objectPairFilter.reset();
            bpLayers.reset();
            jobsPool.reset();
            jobsSingle.reset();
            tempAllocator.reset();
            initialized = false;
            ReleaseJolt();
        }

        static JPH::RefConst<JPH::Shape> MakeShape(const CollisionShapeDesc& d)
        {
            switch (d.type)
            {
                case CollisionShapeDesc::Type::Capsule:
                    // Jolt asserts a strictly positive cylinder half height.
                    return new JPH::CapsuleShape(std::max(d.halfHeight, 1e-3f), d.radius);
                case CollisionShapeDesc::Type::Sphere:
                    return new JPH::SphereShape(d.radius);
                case CollisionShapeDesc::Type::Box:
                {
                    const float minExtent = std::min({d.halfExtents.x, d.halfExtents.y, d.halfExtents.z});
                    const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * minExtent);
                    return new JPH::BoxShape(ToJph(d.halfExtents), convexRadius);
                }
            }
            return nullptr;
        }

        bool AddBodyImpl(uint32_t slot, const CollisionProfile& profile, const glm::vec3& position, std::string& outError)
        {
            if (!initialized || slot >= slots.size())
            {
                outError = fmt::format("slot {} out of range (max {})", slot, slots.size());
                return false;
            }
            if (slots[slot].used)
            {
                outError = fmt::format("slot {} already has a body", slot);
                return false;
            }

            JPH::MutableCompoundShapeSettings settings;
            std::vector<CollisionShapeDesc::Type> types;
            for (const CollisionShapeDesc& d : profile.shapes)
            {
                if (d.nodeIndex < 0)
                {
                    continue;
                }
                if (types.size() >= kMaxShapesPerBody)
                {
                    outError = fmt::format("'{}': more than {} shapes per body", profile.path, kMaxShapesPerBody);
                    return false;
                }
                // Sub-shape poses are rewritten every frame by SetBodyPose; start at the profile's bone-local
                // pose so a body that is never posed still has a sane layout.
                settings.AddShape(ToJph(d.center), ToJph(d.rotation), MakeShape(d).GetPtr());
                types.push_back(d.type);
            }
            if (types.empty())
            {
                outError = fmt::format("'{}': no resolvable shapes", profile.path);
                return false;
            }
            JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                outError = fmt::format("'{}': compound shape: {}", profile.path, result.GetError().c_str());
                return false;
            }

            JPH::BodyCreationSettings bcs(result.Get(), ToJph(position), JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, kLayerCharacter);
            bcs.mCollideKinematicVsNonDynamic = true; // kinematic vs kinematic contacts are off by default
            bcs.mAllowSleeping = false;               // a still pose must not drop out of the active set
            bcs.mUserData = slot;
            JPH::BodyInterface& bi = physics->GetBodyInterface();
            JPH::Body* body = bi.CreateBody(bcs);
            if (body == nullptr)
            {
                outError = "Jolt could not create a body (max bodies reached?)";
                return false;
            }
            bi.AddBody(body->GetID(), JPH::EActivation::Activate);

            Slot& s = slots[slot];
            s.used = true;
            s.body = body->GetID();
            s.shapeCount = static_cast<uint32_t>(types.size());
            s.shapeTypes = std::move(types);
            accum[slot].Reset();
            ++bodyCount;
            return true;
        }

        void RemoveBodyImpl(uint32_t slot)
        {
            if (!initialized || slot >= slots.size() || !slots[slot].used)
            {
                return;
            }
            JPH::BodyInterface& bi = physics->GetBodyInterface();
            bi.RemoveBody(slots[slot].body);
            bi.DestroyBody(slots[slot].body);
            slots[slot] = Slot{};
            accum[slot].Reset();
            --bodyCount;
        }

        void SetBodyPoseImpl(uint32_t slot, const glm::vec3& bodyPosition, std::span<const CollisionShapePose> poses)
        {
            if (!initialized || slot >= slots.size() || !slots[slot].used)
            {
                return;
            }
            const auto t0 = std::chrono::steady_clock::now();
            Slot& s = slots[slot];
            const uint32_t n = std::min<uint32_t>(s.shapeCount, static_cast<uint32_t>(poses.size()));
            scratchPositions.resize(n);
            scratchRotations.resize(n);
            for (uint32_t i = 0; i < n; ++i)
            {
                scratchPositions[i] = ToJph(poses[i].position - bodyPosition);
                scratchRotations[i] = ToJph(glm::normalize(poses[i].rotation));
            }

            JPH::Vec3 previousCom = JPH::Vec3::sZero();
            {
                // ModifyShapes is not thread safe: hold the write lock for exactly this call, release it before
                // touching the (locking) BodyInterface again.
                JPH::BodyLockWrite lock(physics->GetBodyLockInterface(), s.body);
                if (!lock.Succeeded())
                {
                    return;
                }
                JPH::Body& body = lock.GetBody();
                auto* compound = static_cast<JPH::MutableCompoundShape*>(const_cast<JPH::Shape*>(body.GetShape()));
                previousCom = compound->GetCenterOfMass();
                if (n > 0)
                {
                    compound->ModifyShapes(0, n, scratchPositions.data(), scratchRotations.data());
                }
            }
            JPH::BodyInterface& bi = physics->GetBodyInterface();
            bi.SetPositionAndRotation(s.body, ToJph(bodyPosition), JPH::Quat::sIdentity(), JPH::EActivation::Activate);
            bi.NotifyShapeChanged(s.body, previousCom, false, JPH::EActivation::Activate);
            timing.writeUs += MicrosSince(t0);
        }

        void StepImpl(float deltaTime)
        {
            if (!initialized)
            {
                return;
            }
            for (Accumulator& a : accum)
            {
                a.Reset();
            }
            {
                std::lock_guard<std::mutex> lock(contactMutex);
                contacts.clear();
            }
            manifoldCount.store(0, std::memory_order_relaxed);

            bool usePool = false;
            switch (threading.mode)
            {
                case CollisionThreadingPolicy::Mode::Single: usePool = false; break;
                case CollisionThreadingPolicy::Mode::Pool: usePool = true; break;
                case CollisionThreadingPolicy::Mode::Auto:
                    usePool = static_cast<int>(bodyCount) >= std::max(1, threading.joltThreshold);
                    break;
            }
            JPH::JobSystem* jobs = usePool ? static_cast<JPH::JobSystem*>(jobsPool.get()) : static_cast<JPH::JobSystem*>(jobsSingle.get());

            const auto t0 = std::chrono::steady_clock::now();
            physics->Update(std::max(deltaTime, 1.0f / 240.0f), 1, tempAllocator.get(), jobs);
            timing.updateUs = MicrosSince(t0);
            timing.usedThreadPool = usePool;
            timing.bodies = bodyCount;
            timing.manifolds = manifoldCount.load(std::memory_order_relaxed);
        }

        std::span<const CollisionPush> ResolveImpl()
        {
            const auto t0 = std::chrono::steady_clock::now();
            pushes.clear();
            for (uint32_t slot = 0; slot < slots.size(); ++slot)
            {
                if (!slots[slot].used)
                {
                    continue;
                }
                const Accumulator& a = accum[slot];
                if (a.shapeMask.load(std::memory_order_relaxed) == 0)
                {
                    continue;
                }
                glm::vec3 delta(a.dx.load(std::memory_order_relaxed), 0.0f, a.dz.load(std::memory_order_relaxed));
                if (glm::dot(delta, delta) > 0.0f)
                {
                    pushes.push_back({slot, delta});
                }
            }
            timing.resolveUs = MicrosSince(t0);
            // writeUs accumulated across this frame's SetBodyPose calls; publish the frame and restart it.
            lastTiming = timing;
            timing.writeUs = 0.0;
            return pushes;
        }
        CollisionStepTiming lastTiming;

        // ---- ContactListener (worker threads) --------------------------------------------------------------
        void Accumulate(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
        {
            // Speculative contacts come in with a negative depth; only real penetration counts.
            if (manifold.mPenetrationDepth <= 0.0f)
            {
                return;
            }
            const uint32_t slot1 = static_cast<uint32_t>(body1.GetUserData());
            const uint32_t slot2 = static_cast<uint32_t>(body2.GetUserData());
            if (slot1 >= accum.size() || slot2 >= accum.size())
            {
                return;
            }
            JPH::SubShapeID remainder;
            const uint32_t shape1 = static_cast<const JPH::CompoundShape*>(body1.GetShape())->GetSubShapeIndexFromID(manifold.mSubShapeID1, remainder);
            const uint32_t shape2 = static_cast<const JPH::CompoundShape*>(body2.GetShape())->GetSubShapeIndexFromID(manifold.mSubShapeID2, remainder);

            // mWorldSpaceNormal points from body 1 to body 2: moving body 2 by normal * depth separates them.
            const glm::vec3 n = ToGlm(manifold.mWorldSpaceNormal);
            const glm::vec3 half = n * (0.5f * manifold.mPenetrationDepth);
            accum[slot1].Add(-half, shape1 < 32 ? (1u << shape1) : 0u);
            accum[slot2].Add(half, shape2 < 32 ? (1u << shape2) : 0u);
            manifoldCount.fetch_add(1, std::memory_order_relaxed);

            CollisionContactDebug dbg;
            dbg.slotA = slot1;
            dbg.slotB = slot2;
            dbg.shapeA = static_cast<uint8_t>(std::min<uint32_t>(shape1, 255));
            dbg.shapeB = static_cast<uint8_t>(std::min<uint32_t>(shape2, 255));
            dbg.point = manifold.mRelativeContactPointsOn1.empty()
                                ? ToGlm(manifold.mBaseOffset)
                                : ToGlm(manifold.GetWorldSpaceContactPointOn1(0));
            dbg.normal = n;
            dbg.depth = manifold.mPenetrationDepth;
            std::lock_guard<std::mutex> lock(contactMutex);
            if (contacts.size() < kMaxDebugContacts)
            {
                contacts.push_back(dbg);
            }
        }

        void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
        {
            Accumulate(body1, body2, manifold);
        }
        void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
        {
            Accumulate(body1, body2, manifold);
        }
    };

    // ---- public facade ----------------------------------------------------------------------------------

    CollisionWorld::CollisionWorld() : mImpl(std::make_unique<Impl>()) {}
    CollisionWorld::~CollisionWorld() = default;

    bool CollisionWorld::Init(uint32_t maxBodies, const CollisionThreadingPolicy& threading, std::string& outError)
    {
        return mImpl->InitImpl(maxBodies, threading, outError);
    }
    void CollisionWorld::Shutdown() { mImpl->ShutdownImpl(); }
    bool CollisionWorld::IsInitialized() const { return mImpl->initialized; }

    bool CollisionWorld::AddBody(uint32_t slot, const CollisionProfile& profile, const glm::vec3& position, std::string& outError)
    {
        return mImpl->AddBodyImpl(slot, profile, position, outError);
    }
    void CollisionWorld::RemoveBody(uint32_t slot) { mImpl->RemoveBodyImpl(slot); }
    bool CollisionWorld::HasBody(uint32_t slot) const
    {
        return mImpl->initialized && slot < mImpl->slots.size() && mImpl->slots[slot].used;
    }
    uint32_t CollisionWorld::BodyCount() const { return mImpl->bodyCount; }
    uint32_t CollisionWorld::ShapeCount(uint32_t slot) const { return HasBody(slot) ? mImpl->slots[slot].shapeCount : 0; }
    void CollisionWorld::OptimizeBroadPhase()
    {
        if (mImpl->initialized)
        {
            mImpl->physics->OptimizeBroadPhase();
        }
    }

    void CollisionWorld::SetBodyPose(uint32_t slot, const glm::vec3& bodyPosition, std::span<const CollisionShapePose> worldShapePoses)
    {
        mImpl->SetBodyPoseImpl(slot, bodyPosition, worldShapePoses);
    }
    void CollisionWorld::Step(float deltaTime) { mImpl->StepImpl(deltaTime); }
    std::span<const CollisionPush> CollisionWorld::Resolve() { return mImpl->ResolveImpl(); }

    bool CollisionWorld::GetBodyBounds(uint32_t slot, glm::vec3& outMin, glm::vec3& outMax) const
    {
        if (!HasBody(slot))
        {
            return false;
        }
        JPH::BodyLockRead lock(mImpl->physics->GetBodyLockInterface(), mImpl->slots[slot].body);
        if (!lock.Succeeded())
        {
            return false;
        }
        const JPH::AABox& box = lock.GetBody().GetWorldSpaceBounds();
        outMin = ToGlm(box.mMin);
        outMax = ToGlm(box.mMax);
        return true;
    }

    bool CollisionWorld::BroadPhaseOverlapsOthers(uint32_t slot) const
    {
        if (!HasBody(slot))
        {
            return false;
        }
        JPH::AABox box;
        {
            JPH::BodyLockRead lock(mImpl->physics->GetBodyLockInterface(), mImpl->slots[slot].body);
            if (!lock.Succeeded())
            {
                return false;
            }
            box = lock.GetBody().GetWorldSpaceBounds();
        }
        struct Collector final : public JPH::CollideShapeBodyCollector
        {
            JPH::BodyID self;
            bool hit = false;
            void AddHit(const JPH::BodyID& id) override
            {
                if (id != self)
                {
                    hit = true;
                    ForceEarlyOut();
                }
            }
        } collector;
        collector.self = mImpl->slots[slot].body;
        mImpl->physics->GetBroadPhaseQuery().CollideAABox(box, collector);
        return collector.hit;
    }

    uint32_t CollisionWorld::ContactShapeMask(uint32_t slot) const
    {
        return HasBody(slot) ? mImpl->accum[slot].shapeMask.load(std::memory_order_relaxed) : 0u;
    }
    std::span<const CollisionContactDebug> CollisionWorld::Contacts() const { return mImpl->contacts; }
    const CollisionStepTiming& CollisionWorld::LastTiming() const { return mImpl->lastTiming; }

    CollisionThreadingPolicy& CollisionWorld::Threading() { return mImpl->threading; }
    const CollisionThreadingPolicy& CollisionWorld::Threading() const { return mImpl->threading; }
    int CollisionWorld::ThreadPoolSize() const { return mImpl->poolThreads; }
} // namespace RAnimation
