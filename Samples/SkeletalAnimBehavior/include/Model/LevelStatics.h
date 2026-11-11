#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Tools/DebugLines.h>

namespace RAnimation
{
    class Model;

    // Gameplay content of a level's static layer (a DCC-authored USD sublayer, see
    // Tools/Maya/BehaviorScene). Everything is in canonical engine space (Y-up, metres): positions and box
    // sizes come from the prim's full local-to-world transform folded with the stage metadata, because the
    // DCC exporters put their unit / axis conversion on the root prim (Maya: scale 0.01, Blender: Rx(-90)).

    enum class StaticColliderTag
    {
        Obstacle, // blocks movement and line of sight
        Boundary  // level wall: also blocks, and perception reports touching it
    };

    // Oriented box. halfExtents already include the prim's world scale; rotation has the scale removed.
    struct StaticBox
    {
        glm::vec3 center = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 halfExtents = glm::vec3(0.5f);

        // 8 corners in world space (bit 0 -> +x, bit 1 -> +y, bit 2 -> +z).
        void Corners(glm::vec3 (&out)[8]) const;
        bool Contains(const glm::vec3& point) const;
        float Volume() const { return 8.0f * halfExtents.x * halfExtents.y * halfExtents.z; }
    };

    struct StaticCollider
    {
        std::string name; // prim name (COL_*, BND_*)
        std::string path; // prim path, for messages
        StaticColliderTag tag = StaticColliderTag::Obstacle;
        StaticBox box;
    };

    struct StaticZone
    {
        std::string name; // zone name reported by perception (prim name without the ZONE_ prefix)
        std::string path;
        StaticBox box;
    };

    enum class StaticMarkerKind
    {
        Waypoint,
        Spawn
    };

    struct StaticMarker
    {
        std::string name; // full prim name
        std::string path;
        StaticMarkerKind kind = StaticMarkerKind::Waypoint;
        std::string route; // WP_<route>_<index>
        int index = -1;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f); // marker's local -Z projected on XZ (character forward)
        float radius = 0.0f; // ranim:radius, metres, never scaled by the prim transform
    };

    struct LevelStatics
    {
        std::vector<StaticCollider> colliders;
        std::vector<StaticZone> zones;
        std::vector<StaticMarker> markers;
        std::vector<std::string> warnings; // naming / attribute mismatches found while reading

        bool Empty() const { return colliders.empty() && zones.empty() && markers.empty(); }
        // Smallest zone containing the point (nested zones report the inner one), or nullptr.
        const StaticZone* ZoneAt(const glm::vec3& point) const;
        // Waypoints of one route ordered by index.
        std::vector<const StaticMarker*> Route(const std::string& route) const;
    };

    // One static sublayer of the level: its file, the gameplay data read from it and the display model
    // (null when the layer has no visible mesh). The display model is shown through an ordinary instance
    // at the origin; the scene exporter writes the layer back as a subLayer, never as an instance.
    struct StaticLayer
    {
        std::string path; // absolute
        LevelStatics statics;
        std::shared_ptr<Model> model;
    };

    // Debug wireframes: obstacles white, boundaries orange, zones cyan, waypoints yellow crosses joined per
    // route, spawns green crosses with a heading tick.
    void AppendLevelStaticsLines(const LevelStatics& statics, DebugLineList& out);
} // namespace RAnimation
