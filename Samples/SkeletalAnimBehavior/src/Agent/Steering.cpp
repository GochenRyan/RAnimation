#include <Agent/Steering.h>

#include <algorithm>
#include <cmath>

namespace RAnimation
{
    namespace
    {
        // P control on the yaw error with a dead zone. turn > 0 decreases yaw (see CharacterController::Move).
        float TurnFor(float errorDeg, const SteeringSettings& s)
        {
            if (std::abs(errorDeg) < s.turnDeadzoneDeg)
            {
                return 0.0f;
            }
            return std::clamp(-s.turnGainPerDeg * errorDeg, -1.0f, 1.0f);
        }
    } // namespace

    void Steer(const AgentIntent& intent,
               const MotionState& motion,
               const ExecutionFeedback& feedback,
               const std::optional<glm::vec3>& targetPosition,
               const SteeringSettings& settings,
               SteeringMemory& memory,
               SteeringResult& out)
    {
        out = SteeringResult{};
        out.commands = intent.commands;

        // Player / script input bypasses the goal-level channels.
        if (intent.direct)
        {
            out.input = *intent.direct;
            return;
        }

        const LocomotionIntent& loco = intent.locomotion;
        CharacterInput& in = out.input;
        in.crouch = loco.crouch;
        in.aim = intent.stance.aim;
        in.aimPitch = intent.stance.aimPitch;

        const glm::vec3 forward = ForwardFromYaw(motion.yawDeg);
        const glm::vec3 right = RightFromYaw(motion.yawDeg);
        auto moveAlong = [&](const glm::vec3& worldDir, float magnitude)
        {
            in.moveY = glm::dot(worldDir, forward) * magnitude;
            in.moveX = glm::dot(worldDir, right) * magnitude;
            in.run = loco.gait != Gait::Walk;
            in.sprint = loco.gait == Gait::Sprint && std::abs(out.facingErrorDeg) <= settings.sprintFacingDeg;
        };

        switch (loco.mode)
        {
            case LocomotionMode::None:
                break;
            case LocomotionMode::MoveTo:
            case LocomotionMode::FaceTo:
            {
                if (!targetPosition)
                {
                    break; // the target entity is gone: stand still, the node fails on its own check
                }
                glm::vec3 delta = *targetPosition - motion.position;
                delta.y = 0.0f;
                const float distance = glm::length(delta);
                out.hasTarget = true;
                out.distance = distance;
                if (distance > 1e-4f)
                {
                    out.facingErrorDeg = WrapDeltaDeg(YawFromDirection(delta), motion.yawDeg);
                }
                if (loco.mode == LocomotionMode::FaceTo)
                {
                    in.turn = TurnFor(out.facingErrorDeg, settings);
                    break;
                }
                out.arrived = distance <= loco.arriveRadius;
                if (out.arrived)
                {
                    break;
                }
                // linear slow-down between arriveRadius * slowdownFactor and arriveRadius
                const float slowStart = loco.arriveRadius * std::max(1.0f, settings.slowdownFactor);
                float magnitude = 1.0f;
                if (distance < slowStart && slowStart > loco.arriveRadius)
                {
                    magnitude = std::clamp((distance - loco.arriveRadius) / (slowStart - loco.arriveRadius), 0.15f, 1.0f);
                }
                moveAlong(delta / distance, magnitude);
                in.turn = TurnFor(out.facingErrorDeg, settings);
                break;
            }
            case LocomotionMode::MoveDirection:
            {
                glm::vec3 d = loco.direction;
                d.y = 0.0f;
                const float len = glm::length(d);
                if (len < 1e-4f)
                {
                    break;
                }
                d /= len;
                out.facingErrorDeg = WrapDeltaDeg(YawFromDirection(d), motion.yawDeg);
                moveAlong(d, 1.0f);
                in.turn = TurnFor(out.facingErrorDeg, settings);
                break;
            }
        }

        // Declarative weapon: send one ToggleWeapon and wait until the feedback flips.
        if (memory.toggleSent && feedback.armed != memory.armedAtSend)
        {
            memory.toggleSent = false;
        }
        if (!feedback.dead && intent.stance.wantArmed != feedback.armed && !memory.toggleSent)
        {
            IntentCommand toggle;
            toggle.type = CommandType::ToggleWeapon;
            out.commands.push_back(toggle);
            memory.toggleSent = true;
            memory.armedAtSend = feedback.armed;
        }
    }
} // namespace RAnimation
