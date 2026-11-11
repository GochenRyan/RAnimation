#pragma once

#include <Behavior/Blackboard.h>

// Standard perception keys (§7.1): every agent's instance blackboard starts with these, in this order. A tree
// may redeclare one in its own instance schema to change the wake flag (same type required).

namespace RAnimation
{
    namespace PerceptionKeys
    {
        inline constexpr const char* kContactEntity = "perception.contact.entity";
        inline constexpr const char* kContactTime = "perception.contact.time";
        inline constexpr const char* kContactCount = "perception.contact.count";
        inline constexpr const char* kBoundaryTouching = "perception.boundary.touching";
        inline constexpr const char* kBoundaryTime = "perception.boundary.time";
        inline constexpr const char* kBoundaryNormal = "perception.boundary.normal";
        inline constexpr const char* kPlayerEntity = "perception.player.entity";
        inline constexpr const char* kPlayerDistance = "perception.player.distance";
        inline constexpr const char* kPlayerInRange = "perception.player.inRange";
        inline constexpr const char* kPlayerInFov = "perception.player.inFov";
        inline constexpr const char* kNearestEntity = "perception.nearest.entity";
        inline constexpr const char* kPlayerVisible = "perception.player.visible";
        inline constexpr const char* kPlayerLastSeenPos = "perception.player.lastSeenPos";
        inline constexpr const char* kPlayerLastSeenTime = "perception.player.lastSeenTime";
        inline constexpr const char* kZone = "perception.zone";
        inline constexpr const char* kZoneEnterTime = "perception.zone.enterTime";
        inline constexpr const char* kInteractionFrom = "perception.interaction.from";
        inline constexpr const char* kInteractionTime = "perception.interaction.time";
    } // namespace PerceptionKeys

    // The schema above (Instance scope, defaults, wake flags as in §7.1).
    const BlackboardSchema& PerceptionSchema();
} // namespace RAnimation
