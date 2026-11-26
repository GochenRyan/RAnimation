#pragma once
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Model/EntityId.h>

namespace RAnimation
{
    enum class MarkerKind : uint8_t
    {
        Waypoint,
        Zone, // zone centre
        Spawn
    };

    // Read-only view of the world for decision makers (§5.5): entity positions, level markers, the player.
    // Implemented by CharacterSystem; leaf nodes use it instead of reaching into the scene.
    class WorldQuery
    {
    public:
        virtual ~WorldQuery() = default;
        virtual std::optional<glm::vec3> Position(EntityId id) const = 0;
        virtual std::optional<float> YawDeg(EntityId id) const = 0;
        virtual EntityId PlayerEntity() const = 0;
        // Waypoint: name = route, index wraps around the route length. Zone / Spawn: name = zone / prim name.
        virtual std::optional<glm::vec3> FindMarker(MarkerKind kind, const std::string& name, int index) const = 0;
        virtual int RouteLength(const std::string& route) const = 0;
    };
} // namespace RAnimation
