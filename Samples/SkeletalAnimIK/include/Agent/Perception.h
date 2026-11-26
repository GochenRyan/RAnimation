#pragma once
#include <functional>
#include <set>
#include <span>
#include <string>

#include <glm/glm.hpp>

#include <Collision/ContactEvent.h>
#include <Model/EntityId.h>
#include <Model/LevelStatics.h>

namespace RAnimation
{
    struct Agent;
    // ContactEvent: Collision/ContactEvent.h (produced by CollisionSystem, consumed here).

    // What the world provides to perception this frame.
    struct PerceptionInputs
    {
        std::span<const ContactEvent> contacts;
        // true when a static body blocks the segment (line of sight); null = nothing ever blocks
        std::function<bool(const glm::vec3& from, const glm::vec3& to)> lineBlocked;
        const std::vector<StaticLayer>* staticLayers = nullptr;
    };

    struct PerceptionSettings
    {
        float eyeHeight = 1.6f;   // fallback when the model has no head joint
        float chestHeight = 1.2f; // where the line of sight aims at the player
        int raysThisFrame = 0;    // statistics
    };

    // Per-agent perception memory (not in the blackboard: bookkeeping behind the keys).
    struct PerceptionMemory
    {
        std::set<EntityId> touching; // characters in contact
        int boundaryContacts = 0;
        std::string zone;
        bool zoneValid = false;
        bool visible = false;
    };
} // namespace RAnimation
