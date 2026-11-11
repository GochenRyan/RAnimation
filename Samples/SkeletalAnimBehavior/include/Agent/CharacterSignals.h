#pragma once
#include <cmath>

#include <glm/glm.hpp>

// The two plain signals shared by the execution layer (Controls/CharacterController) and the layers above
// it (Agent/, Behavior/). They live here so a decision maker can read the motion state and the intent layer
// can produce input without including Controls/.

namespace RAnimation
{
    struct CharacterInput
    {
        float moveX = 0.0f;    // -1..1 strafe (right positive)
        float moveY = 0.0f;    // -1..1 forward
        float turn = 0.0f;     // -1..1 yaw (left/right turn in place or while moving); positive decreases yaw
        bool run = false;      // jog instead of walk
        bool sprint = false;   // sprint sub machine
        bool crouch = false;
        bool aim = false;      // raises the upper body layer when armed
        float aimPitch = 0.0f; // -1..1, drives the aim offset blend space

        float MoveMagnitude() const
        {
            const float m = moveX * moveX + moveY * moveY;
            return m > 1.0f ? 1.0f : (m > 0.0f ? std::sqrt(m) : 0.0f);
        }
    };

    // Gameplay truth about where the character is, owned by the controller (Move), read by the guards.
    struct MotionState
    {
        bool grounded = true;
        bool airborne = false;
        float speed = 0.0f;           // horizontal m/s actually moved this frame
        float verticalVelocity = 0.0f;
        float yawDeg = 0.0f;          // forward = (-sin yaw, 0, -cos yaw)
        glm::vec3 position = glm::vec3(0.0f);
        bool movementLocked = false;  // a montage with lockMove is playing
    };

    // Character local basis used by Move, Steering and Perception: forward -Z, right +X, up +Y.
    inline glm::vec3 ForwardFromYaw(float yawDeg)
    {
        const float yaw = glm::radians(yawDeg);
        return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
    }
    inline glm::vec3 RightFromYaw(float yawDeg)
    {
        const float yaw = glm::radians(yawDeg);
        return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
    }
    // Yaw whose forward points along the horizontal direction d (inverse of ForwardFromYaw).
    inline float YawFromDirection(const glm::vec3& d)
    {
        return glm::degrees(std::atan2(-d.x, -d.z));
    }
    // a - b wrapped to (-180, 180].
    inline float WrapDeltaDeg(float a, float b)
    {
        float d = std::fmod(a - b, 360.0f);
        if (d <= -180.0f)
            d += 360.0f;
        else if (d > 180.0f)
            d -= 360.0f;
        return d;
    }
} // namespace RAnimation
