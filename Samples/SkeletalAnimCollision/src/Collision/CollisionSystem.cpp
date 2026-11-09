#include <Collision/CollisionSystem.h>

#include <chrono>
#include <cmath>

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>

namespace RAnimation
{
    namespace
    {
        inline double MicrosSince(std::chrono::steady_clock::time_point t0)
        {
            return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        }

        // Rotation of a world matrix that may carry (non-uniform) scale: normalise the basis columns first.
        glm::quat RotationOf(const glm::mat4& m)
        {
            glm::mat3 r(m);
            for (int c = 0; c < 3; ++c)
            {
                const float len = glm::length(r[c]);
                if (len > 1e-8f)
                {
                    r[c] /= len;
                }
            }
            return glm::normalize(glm::quat_cast(r));
        }

        const glm::vec3 kWhite(0.92f, 0.92f, 0.92f);
        const glm::vec3 kRed(1.0f, 0.2f, 0.2f);
        const glm::vec3 kContact(1.0f, 0.55f, 0.1f);
        constexpr int kRingSegments = 16;
        constexpr int kArcSegments = 8;
    } // namespace

    bool CollisionSystem::Init(uint32_t maxBodies, const CollisionThreadingPolicy& threading, std::string& outError)
    {
        if (!mWorld.Init(maxBodies, threading, outError))
        {
            return false;
        }
        mSlotToInstance.assign(maxBodies, nullptr);
        mFreeSlots.clear();
        mFreeSlots.reserve(maxBodies);
        for (uint32_t i = maxBodies; i > 0; --i)
        {
            mFreeSlots.push_back(i - 1); // pop_back hands out slot 0 first
        }
        return true;
    }

    void CollisionSystem::Shutdown()
    {
        mInstances.clear();
        mModels.clear();
        mModelStatus.clear();
        mSlotToInstance.clear();
        mFreeSlots.clear();
        mWorld.Shutdown();
    }

    const CollisionSystem::ModelEntry& CollisionSystem::EnsureModel(const std::shared_ptr<Model>& model)
    {
        auto it = mModels.find(model.get());
        if (it != mModels.end())
        {
            return it->second;
        }
        ModelEntry entry;
        const std::string path = CollisionProfile::PathForModel(*model);
        auto profile = std::make_shared<CollisionProfile>();
        std::string error;
        if (!CollisionProfile::Load(path, *profile, error))
        {
            entry.status = fmt::format("no collision profile ({})", error);
        }
        else if (!profile->Resolve(*model, error))
        {
            // Partial profiles still work: shapes whose bone is missing are skipped by AddBody.
            entry.profile = profile;
            entry.status = fmt::format("{} shapes, some bones missing: {}", profile->shapes.size(), error);
        }
        else
        {
            entry.profile = profile;
            entry.status = fmt::format("{} shapes from {}", profile->shapes.size(), path);
        }
        mModelStatus[model.get()] = entry.status;
        return mModels.emplace(model.get(), std::move(entry)).first->second;
    }

    void CollisionSystem::Sync(ModelAndInstanceData& scene)
    {
        if (!mWorld.IsInitialized())
        {
            return;
        }
        ++mFrame;
        for (const std::shared_ptr<ModelInstance>& instance : scene.miModelInstances)
        {
            if (instance == nullptr)
            {
                continue;
            }
            auto it = mInstances.find(instance.get());
            if (it == mInstances.end())
            {
                std::shared_ptr<Model> model = instance->GetModel();
                if (model == nullptr)
                {
                    continue;
                }
                const ModelEntry& modelEntry = EnsureModel(model);
                if (modelEntry.profile == nullptr || mFreeSlots.empty())
                {
                    continue;
                }
                InstanceEntry entry;
                entry.slot = mFreeSlots.back();
                entry.model = model;
                entry.modelEntry = &modelEntry;
                for (const CollisionShapeDesc& d : modelEntry.profile->shapes)
                {
                    if (d.nodeIndex >= 0)
                    {
                        entry.shapes.push_back(&d);
                    }
                }
                std::string error;
                if (!mWorld.AddBody(entry.slot, *modelEntry.profile, instance->GetWorldPosition(), error))
                {
                    fmt::print(stderr, "CollisionSystem: {}\n", error);
                    continue;
                }
                mFreeSlots.pop_back();
                mSlotToInstance[entry.slot] = instance.get();
                entry.worldPoses.resize(entry.shapes.size());
                it = mInstances.emplace(instance.get(), std::move(entry)).first;
            }
            it->second.frameSeen = mFrame;
        }

        // Instances that disappeared from the scene (deleted / undo) release their body.
        for (auto it = mInstances.begin(); it != mInstances.end();)
        {
            if (it->second.frameSeen != mFrame)
            {
                mWorld.RemoveBody(it->second.slot);
                mSlotToInstance[it->second.slot] = nullptr;
                mFreeSlots.push_back(it->second.slot);
                it = mInstances.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void CollisionSystem::UpdateBodies(ModelAndInstanceData&)
    {
        if (!mWorld.IsInitialized())
        {
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        double poseUs = 0.0;
        for (auto& [instancePtr, entry] : mInstances)
        {
            ModelInstance* instance = instancePtr;
            const auto tp = std::chrono::steady_clock::now();
            // World joint matrices of what is being drawn (all tiers go through the CPU pose here).
            entry.model->ComputeNodeGlobals(instance->GetCpuPose(), instance->GetLocalTransformMatrix(), mScratchGlobals);
            mScratchPoses.resize(entry.shapes.size());
            for (size_t i = 0; i < entry.shapes.size(); ++i)
            {
                const CollisionShapeDesc& d = *entry.shapes[i];
                const glm::mat4& joint = mScratchGlobals[static_cast<size_t>(d.nodeIndex)];
                CollisionShapePose& p = mScratchPoses[i];
                // The joint matrices carry the asset's bind scale (UAL: 100 in the 3x3, translation already in
                // metres), so the shape offset must go through the normalised rotation only, never the raw matrix.
                const glm::quat jointRotation = RotationOf(joint);
                p.position = glm::vec3(joint[3]) + jointRotation * d.center;
                p.rotation = jointRotation * d.rotation;
            }
            entry.worldPoses = mScratchPoses;
            poseUs += MicrosSince(tp);
            mWorld.SetBodyPose(entry.slot, instance->GetWorldPosition(), mScratchPoses);
        }
        mTiming.poseUs = poseUs;
        (void)t0;
    }

    void CollisionSystem::Step(float deltaTime)
    {
        if (!mWorld.IsInitialized())
        {
            return;
        }
        mWorld.Step(deltaTime);
    }

    std::span<const CollisionPush> CollisionSystem::Resolve()
    {
        if (!mWorld.IsInitialized())
        {
            return {};
        }
        std::span<const CollisionPush> pushes = mWorld.Resolve();
        const CollisionStepTiming& t = mWorld.LastTiming();
        mTiming.writeUs = t.writeUs;
        mTiming.updateUs = t.updateUs;
        mTiming.resolveUs = t.resolveUs;
        mTiming.usedThreadPool = t.usedThreadPool;
        mTiming.bodies = t.bodies;
        mTiming.manifolds = t.manifolds;
        mTiming.pushes = static_cast<uint32_t>(pushes.size());
        return pushes;
    }

    ModelInstance* CollisionSystem::InstanceForSlot(uint32_t slot) const
    {
        return slot < mSlotToInstance.size() ? mSlotToInstance[slot] : nullptr;
    }

    bool CollisionSystem::GetInstanceBounds(ModelInstance* instance, glm::vec3& outMin, glm::vec3& outMax) const
    {
        const auto it = mInstances.find(instance);
        return it != mInstances.end() && mWorld.GetBodyBounds(it->second.slot, outMin, outMax);
    }

    void CollisionSystem::AppendShapeWireframe(DebugLineList& out, const CollisionShapeDesc& shape, const CollisionShapePose& pose, const glm::vec3& color)
    {
        const glm::mat3 R = glm::mat3_cast(pose.rotation);
        const glm::vec3 X = R[0];
        const glm::vec3 Y = R[1];
        const glm::vec3 Z = R[2];
        const glm::vec3 C = pose.position;
        const float twoPi = glm::two_pi<float>();

        auto ring = [&](const glm::vec3& centre, const glm::vec3& u, const glm::vec3& v, float r)
        {
            glm::vec3 prev = centre + u * r;
            for (int i = 1; i <= kRingSegments; ++i)
            {
                const float a = twoPi * static_cast<float>(i) / kRingSegments;
                const glm::vec3 p = centre + (u * std::cos(a) + v * std::sin(a)) * r;
                out.Line(prev, p, color);
                prev = p;
            }
        };
        // Half circle from +u through +axis to -u, in the (u, axis) plane.
        auto arc = [&](const glm::vec3& centre, const glm::vec3& u, const glm::vec3& axis, float r)
        {
            glm::vec3 prev = centre + u * r;
            for (int i = 1; i <= kArcSegments; ++i)
            {
                const float a = glm::pi<float>() * static_cast<float>(i) / kArcSegments;
                const glm::vec3 p = centre + (u * std::cos(a) + axis * std::sin(a)) * r;
                out.Line(prev, p, color);
                prev = p;
            }
        };

        switch (shape.type)
        {
            case CollisionShapeDesc::Type::Sphere:
                ring(C, X, Z, shape.radius);
                ring(C, X, Y, shape.radius);
                ring(C, Y, Z, shape.radius);
                break;
            case CollisionShapeDesc::Type::Capsule:
            {
                const float h = shape.halfHeight;
                const float r = shape.radius;
                const glm::vec3 top = C + Y * h;
                const glm::vec3 bottom = C - Y * h;
                ring(top, X, Z, r);
                ring(bottom, X, Z, r);
                out.Line(top + X * r, bottom + X * r, color);
                out.Line(top - X * r, bottom - X * r, color);
                out.Line(top + Z * r, bottom + Z * r, color);
                out.Line(top - Z * r, bottom - Z * r, color);
                arc(top, X, Y, r);
                arc(top, Z, Y, r);
                arc(bottom, X, -Y, r);
                arc(bottom, Z, -Y, r);
                break;
            }
            case CollisionShapeDesc::Type::Box:
            {
                const glm::vec3 e = shape.halfExtents;
                glm::vec3 c[8];
                for (int i = 0; i < 8; ++i)
                {
                    const float sx = (i & 1) ? 1.0f : -1.0f;
                    const float sy = (i & 2) ? 1.0f : -1.0f;
                    const float sz = (i & 4) ? 1.0f : -1.0f;
                    c[i] = C + X * (sx * e.x) + Y * (sy * e.y) + Z * (sz * e.z);
                }
                static constexpr int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (const auto& edge : edges)
                {
                    out.Line(c[edge[0]], c[edge[1]], color);
                }
                break;
            }
        }
    }

    void CollisionSystem::BuildDebugLines(DebugLineList& out)
    {
        out.Clear();
        mLinesDropped = 0;
        mTiming.drawUs = 0.0;
        if (!mWorld.IsInitialized() || (!mSettings.showAABB && !mSettings.showShapes))
        {
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();

        for (const auto& [instancePtr, entry] : mInstances)
        {
            if (out.vertices.size() + 4096 > kMaxLineVertices)
            {
                ++mLinesDropped;
                continue;
            }
            if (mSettings.showAABB)
            {
                glm::vec3 mn, mx;
                if (mWorld.GetBodyBounds(entry.slot, mn, mx))
                {
                    const bool paired = mWorld.BroadPhaseOverlapsOthers(entry.slot);
                    out.Box(mn, mx, paired ? kRed : kWhite);
                }
            }
            if (mSettings.showShapes)
            {
                const uint32_t mask = mWorld.ContactShapeMask(entry.slot);
                for (size_t i = 0; i < entry.shapes.size(); ++i)
                {
                    const bool hit = i < 32 && (mask & (1u << i)) != 0;
                    AppendShapeWireframe(out, *entry.shapes[i], entry.worldPoses[i], hit ? kRed : kWhite);
                }
            }
        }
        if (mSettings.showShapes)
        {
            // Penetration vectors: contact point on A, along the normal by the depth, with a small cross at the tip.
            for (const CollisionContactDebug& c : mWorld.Contacts())
            {
                if (out.vertices.size() + 8 > kMaxLineVertices)
                {
                    ++mLinesDropped;
                    break;
                }
                const glm::vec3 tip = c.point + c.normal * c.depth;
                out.Line(c.point, tip, kContact);
                const glm::vec3 side = glm::length(glm::cross(c.normal, glm::vec3(0, 1, 0))) > 1e-3f
                                               ? glm::normalize(glm::cross(c.normal, glm::vec3(0, 1, 0)))
                                               : glm::vec3(1, 0, 0);
                const glm::vec3 up = glm::normalize(glm::cross(side, c.normal));
                out.Line(tip - side * 0.02f, tip + side * 0.02f, kContact);
                out.Line(tip - up * 0.02f, tip + up * 0.02f, kContact);
            }
        }
        mTiming.drawUs = MicrosSince(t0);
    }
} // namespace RAnimation
