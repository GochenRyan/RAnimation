#pragma once

#include <cstdint>
#include <string>

namespace RAnimation
{
    // Stable instance identity. Survives scene USD round trips (customData ranim:entityId), so blackboard
    // entity references, contact events and checkpoint saves can name an instance across sessions.
    // 0 = not assigned yet; SceneEditor hands out ids when an instance enters the scene.
    using EntityId = uint64_t;
    inline constexpr EntityId kInvalidEntityId = 0;

    // Per-instance agent configuration authored in the level (customData ranim:agent). An instance without
    // one is a plain animated crowd member. Values are stored and round-tripped here; the behaviour layer
    // interprets them.
    struct AgentConfig
    {
        std::string tree;              // behaviour tree asset, relative to Assets/SkeletalAnimBehavior
        float tickHz = 0.0f;           // 0 = the tree's own default
        float proximityRadius = 8.0f;  // perception.player.inRange
        float fovHalfDeg = 60.0f;      // perception.player.inFov
        float sightRange = 20.0f;      // perception.player.inFov / visible
        bool possessed = false;        // the player controls this agent when the level starts (at most one)

        bool operator==(const AgentConfig&) const = default;
    };
} // namespace RAnimation
