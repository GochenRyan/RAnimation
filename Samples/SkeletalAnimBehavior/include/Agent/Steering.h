#pragma once
#include <optional>
#include <vector>

#include <Agent/AgentIntent.h>
#include <Agent/CharacterSignals.h>
#include <Agent/ExecutionFeedback.h>

// Per-frame translation of the goal-level intent into CharacterInput + one-shot commands (§3.2). Stateless
// apart from SteeringMemory (the ToggleWeapon debounce), so it never needs saving. D21: straight lines only;
// after a push the next frame simply aims again from the new position.

namespace RAnimation
{
    struct SteeringSettings
    {
        float slowdownFactor = 3.0f;   // linear slow-down inside arriveRadius * slowdownFactor
        float turnGainPerDeg = 1.0f / 30.0f; // turn = clamp(-gain * dYaw, -1, 1)
        float turnDeadzoneDeg = 3.0f;  // |dYaw| below this: no turn (no left/right jitter)
        float sprintFacingDeg = 30.0f; // sprint only when (almost) facing the target
    };

    struct SteeringMemory
    {
        bool toggleSent = false; // a ToggleWeapon is in flight; cleared when feedback.armed changes
        bool armedAtSend = false;
    };

    struct SteeringResult
    {
        CharacterInput input;
        std::vector<IntentCommand> commands;
        bool hasTarget = false;
        float distance = 0.0f;       // horizontal distance to the MoveTo / FaceTo target
        float facingErrorDeg = 0.0f; // desired yaw - current yaw
        bool arrived = false;
    };

    // targetPosition: the resolved intent target (entity position when locomotion.targetEntity != 0), or
    // nullopt when the target entity no longer exists (then the character stands still).
    void Steer(const AgentIntent& intent,
               const MotionState& motion,
               const ExecutionFeedback& feedback,
               const std::optional<glm::vec3>& targetPosition,
               const SteeringSettings& settings,
               SteeringMemory& memory,
               SteeringResult& out);
} // namespace RAnimation
