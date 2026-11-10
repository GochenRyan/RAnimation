#include <Collision/CollisionSelfTest.h>

#include <cmath>
#include <string>
#include <vector>

#include <fmt/base.h>
#include <fmt/format.h>

#include <Collision/CollisionProfile.h>
#include <Collision/CollisionWorld.h>

namespace RAnimation
{
    namespace
    {
        int gChecks = 0;
        int gFails = 0;

        void Check(bool condition, const std::string& what)
        {
            ++gChecks;
            if (!condition)
            {
                ++gFails;
            }
            fmt::print("  [{}] {}\n", condition ? " ok " : "FAIL", what);
        }

        bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

        // A profile with one upright capsule on a fake bone (nodeIndex set by hand; no Model involved).
        CollisionProfile CapsuleProfile(float halfHeight, float radius)
        {
            CollisionProfile p;
            p.path = "<selftest>";
            CollisionShapeDesc d;
            d.bone = "root";
            d.type = CollisionShapeDesc::Type::Capsule;
            d.halfHeight = halfHeight;
            d.radius = radius;
            d.nodeIndex = 0;
            p.shapes.push_back(d);
            return p;
        }

        // Body at `position` with its single shape centred on the body (capsule axis = world Y).
        void PoseAt(CollisionWorld& world, uint32_t slot, const glm::vec3& position)
        {
            CollisionShapePose pose;
            pose.position = position;
            world.SetBodyPose(slot, position, std::span<const CollisionShapePose>(&pose, 1));
        }

        void TestProfileParsing()
        {
            fmt::print("\n-- collision.json parsing --\n");
            const std::string json = R"({
                "skeleton": "Test", "units": "m", "margin": 0.05,
                "wholeBody": {"type": "capsule", "halfHeight": 0.6, "radius": 0.35},
                "shapes": [
                    {"bone": "pelvis", "type": "box", "center": [0, 0.05, 0.03], "rotation": [0,0,0,1], "halfExtents": [0.14, 0.11, 0.12]},
                    {"bone": "Head", "type": "sphere", "center": [0, 0.08, 0.02], "radius": 0.15},
                    {"bone": "upperarm_l", "type": "capsule", "center": [0.1, 0, 0], "rotation": [0, 0, 0.7071068, 0.7071068],
                     "halfHeight": 0.1, "radius": 0.07, "merged": ["lowerarm_twist_l"]}
                ]})";
            CollisionProfile p;
            std::string error;
            const bool ok = CollisionProfile::Parse(json, "<inline>", p, error);
            Check(ok, "inline profile parses: " + error);
            Check(p.shapes.size() == 3, fmt::format("3 shapes ({})", p.shapes.size()));
            Check(p.skeleton == "Test" && Near(p.margin, 0.05f, 1e-6f), "skeleton + margin read");
            Check(p.hasWholeBody && Near(p.wholeBodyRadius, 0.35f, 1e-6f), "wholeBody read");
            if (p.shapes.size() == 3)
            {
                Check(p.shapes[0].type == CollisionShapeDesc::Type::Box && Near(p.shapes[0].halfExtents.y, 0.11f, 1e-6f), "box halfExtents");
                Check(p.shapes[1].type == CollisionShapeDesc::Type::Sphere && Near(p.shapes[1].radius, 0.15f, 1e-6f), "sphere radius");
                Check(p.shapes[2].type == CollisionShapeDesc::Type::Capsule && p.shapes[2].merged.size() == 1, "capsule + merged list");
                Check(Near(glm::length(p.shapes[2].rotation), 1.0f, 1e-5f) && Near(p.shapes[2].rotation.z, 0.7071068f, 1e-5f),
                      "rotation stored as glm::quat(w, x, y, z) from [x, y, z, w]");
                Check(p.shapes[0].nodeIndex == -1, "nodeIndex unresolved before Resolve()");
            }

            CollisionProfile bad;
            Check(!CollisionProfile::Parse(R"({"shapes": [{"bone": "a", "type": "cone", "radius": 1}]})", "<bad>", bad, error), "unknown type rejected: " + error);
            Check(!CollisionProfile::Parse(R"({"shapes": [{"bone": "a", "type": "capsule", "halfHeight": 0.1}]})", "<bad>", bad, error), "capsule without radius rejected");
            Check(!CollisionProfile::Parse(R"({"shapes": [{"bone": "a", "type": "box", "halfExtents": [1, 2]}]})", "<bad>", bad, error), "box with 2 extents rejected");
            Check(!CollisionProfile::Parse("not json", "<bad>", bad, error), "garbage rejected");

            // The shipped UAL profile: 18 shapes = 12 capsules + 5 boxes + 1 sphere.
            CollisionProfile ual;
            const std::string path = CollisionProfile::PathForStem("UAL_Mannequin");
            const bool loaded = CollisionProfile::Load(path, ual, error);
            Check(loaded, "UAL_Mannequin.collision.json loads from " + path + (loaded ? "" : ": " + error));
            if (loaded)
            {
                int capsules = 0, boxes = 0, spheres = 0;
                for (const CollisionShapeDesc& d : ual.shapes)
                {
                    capsules += d.type == CollisionShapeDesc::Type::Capsule;
                    boxes += d.type == CollisionShapeDesc::Type::Box;
                    spheres += d.type == CollisionShapeDesc::Type::Sphere;
                }
                Check(ual.shapes.size() == 18, fmt::format("18 shapes ({})", ual.shapes.size()));
                Check(capsules == 12 && boxes == 5 && spheres == 1, fmt::format("12 capsules / 5 boxes / 1 sphere ({}/{}/{})", capsules, boxes, spheres));
                Check(ual.skeleton == "UAL_Mannequin", "skeleton name");
            }
        }

        void TestWorldPushes(CollisionThreadingPolicy::Mode mode, const char* modeName)
        {
            fmt::print("\n-- CollisionWorld ({}) --\n", modeName);
            CollisionWorld world;
            CollisionThreadingPolicy policy;
            policy.mode = mode;
            std::string error;
            Check(world.Init(32, policy, error), std::string("Init: ") + error);

            const CollisionProfile capsule = CapsuleProfile(0.5f, 0.3f);
            Check(world.AddBody(0, capsule, glm::vec3(0.0f), error), "AddBody slot 0: " + error);
            Check(world.AddBody(1, capsule, glm::vec3(0.1f, 0.0f, 0.0f), error), "AddBody slot 1: " + error);
            Check(!world.AddBody(1, capsule, glm::vec3(0.0f), error), "AddBody on an occupied slot fails");
            Check(world.BodyCount() == 2 && world.ShapeCount(0) == 1, "2 bodies, 1 shape each");

            // Two capsules 0.1 m apart along X: penetration = 2r - 0.1 = 0.5 m, so each side moves 0.25 m.
            PoseAt(world, 0, glm::vec3(0.0f));
            PoseAt(world, 1, glm::vec3(0.1f, 0.0f, 0.0f));
            world.Step(1.0f / 60.0f);
            std::span<const CollisionPush> pushes = world.Resolve();
            Check(pushes.size() == 2, fmt::format("overlapping pair -> 2 pushes ({})", pushes.size()));
            if (pushes.size() == 2)
            {
                const CollisionPush& a = pushes[0].slot == 0 ? pushes[0] : pushes[1];
                const CollisionPush& b = pushes[0].slot == 0 ? pushes[1] : pushes[0];
                Check(a.slot == 0 && b.slot == 1, "pushes address slots 0 and 1");
                Check(a.delta.x < 0.0f && b.delta.x > 0.0f, fmt::format("pushed apart along X (a.x {:.3f}, b.x {:.3f})", a.delta.x, b.delta.x));
                Check(Near(glm::length(a.delta), 0.25f, 0.03f) && Near(glm::length(b.delta), 0.25f, 0.03f),
                      fmt::format("each side moves half the penetration (|a| {:.3f}, |b| {:.3f}, expected 0.25)", glm::length(a.delta), glm::length(b.delta)));
                Check(a.delta.y == 0.0f && b.delta.y == 0.0f, "vertical component removed");
                Check(Near(glm::length(a.delta + b.delta), 0.0f, 1e-4f), "pushes are equal and opposite");
            }
            Check(world.ContactShapeMask(0) == 1u && world.ContactShapeMask(1) == 1u, "shape 0 flagged on both bodies");
            Check(world.LastTiming().manifolds >= 1, fmt::format("manifolds reported ({})", world.LastTiming().manifolds));
            Check(world.LastTiming().usedThreadPool == (mode == CollisionThreadingPolicy::Mode::Pool), "job system matches the requested mode");
            Check(!world.Contacts().empty() && world.Contacts()[0].depth > 0.0f, "debug contact list carries a positive depth");
            Check(world.BroadPhaseOverlapsOthers(0) && world.BroadPhaseOverlapsOthers(1), "broad phase pairs the overlapping bodies");

            // Far apart: nothing.
            PoseAt(world, 1, glm::vec3(3.0f, 0.0f, 0.0f));
            world.Step(1.0f / 60.0f);
            pushes = world.Resolve();
            Check(pushes.empty(), fmt::format("separated pair -> no pushes ({})", pushes.size()));
            Check(!world.BroadPhaseOverlapsOthers(0), "broad phase: no pair when 3 m apart");
            glm::vec3 mn, mx;
            Check(world.GetBodyBounds(1, mn, mx) && Near(mn.x, 2.7f, 0.06f) && Near(mx.y, 0.8f, 0.06f),
                  fmt::format("body bounds follow the pose (min.x {:.3f}, max.y {:.3f})", mn.x, mx.y));

            // Diagonal offset: AABBs overlap (0.5 < 0.6 on each axis) but the capsules do not (0.707 > 0.6).
            PoseAt(world, 1, glm::vec3(0.5f, 0.0f, 0.5f));
            world.Step(1.0f / 60.0f);
            pushes = world.Resolve();
            Check(world.BroadPhaseOverlapsOthers(0) && pushes.empty(), "AABB pair without shape contact -> broad phase red, narrow phase empty");

            // Remove a body: the slot is free again and nothing refers to it.
            world.RemoveBody(1);
            Check(!world.HasBody(1) && world.BodyCount() == 1, "RemoveBody frees the slot");
            PoseAt(world, 0, glm::vec3(0.0f));
            world.Step(1.0f / 60.0f);
            Check(world.Resolve().empty(), "lone body -> no pushes");
            Check(world.AddBody(1, capsule, glm::vec3(0.0f), error), "slot reusable after RemoveBody");
            world.Shutdown();
            Check(!world.IsInitialized(), "Shutdown");
        }

        void TestPackedGroupSymmetry()
        {
            fmt::print("\n-- packed group, thread pool: push sum is zero --\n");
            CollisionWorld world;
            CollisionThreadingPolicy policy;
            policy.mode = CollisionThreadingPolicy::Mode::Pool;
            std::string error;
            Check(world.Init(64, policy, error), std::string("Init: ") + error);
            const CollisionProfile capsule = CapsuleProfile(0.4f, 0.25f);
            constexpr int n = 24;
            for (int i = 0; i < n; ++i)
            {
                // 6 x 4 grid at 0.3 m: every body overlaps several neighbours.
                const glm::vec3 pos(static_cast<float>(i % 6) * 0.3f, 0.0f, static_cast<float>(i / 6) * 0.3f);
                world.AddBody(static_cast<uint32_t>(i), capsule, pos, error);
            }
            Check(world.BodyCount() == n, fmt::format("{} bodies", n));
            glm::vec3 sum(0.0f);
            size_t pushCount = 0;
            for (int frame = 0; frame < 5; ++frame)
            {
                for (int i = 0; i < n; ++i)
                {
                    PoseAt(world, static_cast<uint32_t>(i), glm::vec3(static_cast<float>(i % 6) * 0.3f, 0.0f, static_cast<float>(i / 6) * 0.3f));
                }
                world.Step(1.0f / 60.0f);
                for (const CollisionPush& p : world.Resolve())
                {
                    sum += p.delta;
                    ++pushCount;
                }
            }
            Check(pushCount >= 5 * n - 10, fmt::format("almost every body is pushed every frame ({} pushes over 5 frames)", pushCount));
            Check(world.LastTiming().manifolds > n, fmt::format("many manifolds per frame ({})", world.LastTiming().manifolds));
            Check(glm::length(sum) < 1e-3f, fmt::format("sum of all pushes ~ 0 (|sum| {:.6f}): every manifold split half/half, accumulated from worker threads", glm::length(sum)));
            world.Shutdown();
        }
    } // namespace

    int RunCollisionSelfTests()
    {
        gChecks = 0;
        gFails = 0;
        fmt::print("\nCollision self test\n");
        TestProfileParsing();
        TestWorldPushes(CollisionThreadingPolicy::Mode::Single, "single-threaded");
        TestWorldPushes(CollisionThreadingPolicy::Mode::Pool, "thread pool");
        TestPackedGroupSymmetry();
        fmt::print("\nCollision result: {}/{} passed\n", gChecks - gFails, gChecks);
        return gFails;
    }
} // namespace RAnimation
