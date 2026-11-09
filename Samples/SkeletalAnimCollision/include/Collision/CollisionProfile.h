#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace RAnimation
{
    class Model;

    // Per-model collision description: the capsule / box / sphere attached to each whitelisted bone, authored
    // offline by the capsule_fit tool as Assets/SkeletalAnimCollision/collision/<Stem>.collision.json. Shapes
    // are in bone-local space (bind rotation normalised, translation in metres); the runtime only transforms
    // them with the animated joint, it never re-derives sizes.
    struct CollisionShapeDesc
    {
        enum class Type : uint8_t
        {
            Capsule,
            Box,
            Sphere
        };

        std::string bone;                                  // joint name in the model's node list
        Type type = Type::Capsule;
        glm::vec3 center = glm::vec3(0.0f);                // bone_T_shape translation
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // bone_T_shape rotation (capsule axis = local Y)
        glm::vec3 halfExtents = glm::vec3(0.1f);           // box
        float halfHeight = 0.1f;                           // capsule: half height of the cylinder part
        float radius = 0.1f;                               // capsule / sphere
        std::vector<std::string> merged;                   // bones folded into this one (informational)

        // Filled by CollisionProfile::Resolve: index into Model::GetNodeList(), -1 when the bone is missing.
        int32_t nodeIndex = -1;
    };

    struct CollisionProfile
    {
        std::string path;
        std::string skeleton;
        float margin = 0.0f;
        std::vector<CollisionShapeDesc> shapes;

        // Whole-body capsule (reserved for a future character-virtual path; unused by the per-bone world).
        bool hasWholeBody = false;
        float wholeBodyHalfHeight = 0.0f;
        float wholeBodyRadius = 0.0f;

        // Assets/SkeletalAnimCollision/collision/<Stem>.collision.json for the model's "<Stem>.asset.usda".
        static std::string PathForModel(const Model& model);
        static std::string PathForStem(const std::string& stem);

        static bool Load(const std::string& path, CollisionProfile& out, std::string& outError);
        static bool Parse(const std::string& json, const std::string& sourceName, CollisionProfile& out, std::string& outError);

        // Resolves every shape's bone to a node index. Returns false (and lists the offenders) when a bone is
        // missing from the model; shapes with nodeIndex < 0 must then be skipped by the caller.
        bool Resolve(const Model& model, std::string& outError);
    };

    const char* CollisionShapeTypeName(CollisionShapeDesc::Type type);
} // namespace RAnimation
