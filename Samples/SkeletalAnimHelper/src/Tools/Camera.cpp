#include <Tools/Camera.h>

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace RAnimation
{
    namespace
    {
        // Forward direction from the (azimuth, elevation) spherical convention shared with the legacy
        // inline camera: azimuth measured from +X toward +Z, elevation is pitch.
        glm::vec3 ForwardFromAngles(float azimuthDeg, float elevationDeg)
        {
            const float az = glm::radians(azimuthDeg);
            const float el = glm::radians(elevationDeg);
            return glm::normalize(glm::vec3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)));
        }

        float WrapDegrees(float deg)
        {
            deg = std::fmod(deg, 360.0f);
            if (deg < 0.0f)
            {
                deg += 360.0f;
            }
            return deg;
        }

        // Exponential (framerate-independent) smoothing toward `target`. rate>0; larger = faster catch-up.
        glm::vec3 ExpDamp(const glm::vec3& current, const glm::vec3& target, float rate, float dt)
        {
            return glm::mix(target, current, std::exp(-rate * dt));
        }

        constexpr glm::vec3 kWorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        constexpr float kFirstPersonRollSpeed = 90.0f; // degrees/second for Q/E head roll
    } // namespace

    glm::mat4 CameraCommon::MakeProjection(float aspect) const
    {
        glm::mat4 p;
        if (projection == ProjectionType::Orthographic)
        {
            const float h = orthoHalfHeight;
            const float w = h * aspect;
            p = glm::orthoRH_ZO(-w, w, -h, h, nearZ, farZ);
        }
        else
        {
            p = glm::perspectiveRH_ZO(glm::radians(fovDeg), aspect, nearZ, farZ);
        }

        // Reversed-Z: remap depth so near->1, far->0 (z' = w - z). This is why the scene pipelines clear
        // depth to 0 and compare with GREATER_EQUAL. glm is column-major: the z output is row 2, the w
        // output is row 3, so subtract the z-row from the w-row into the z-row per column.
        for (int c = 0; c < 4; ++c)
        {
            p[c][2] = p[c][3] - p[c][2];
        }
        return p;
    }

    void FreeCamera::Update(float deltaTime, const CameraInput& input, float aspect)
    {
        common.projMatrix = common.MakeProjection(aspect);

        if (input.lookActive)
        {
            yawDeg = WrapDegrees(yawDeg + input.mouseDelta.x * mouseSensitivity);
            pitchDeg = glm::clamp(pitchDeg - input.mouseDelta.y * mouseSensitivity, -89.0f, 89.0f);
        }

        const glm::vec3 forward = ForwardFromAngles(yawDeg, pitchDeg);
        const glm::vec3 right = glm::normalize(glm::cross(forward, kWorldUp));

        glm::vec3 move = forward * input.moveForward + right * input.moveRight + kWorldUp * input.moveUp;
        if (glm::dot(move, move) > 0.0f)
        {
            position += glm::normalize(move) * moveSpeed * deltaTime;
        }

        common.viewMatrix = glm::lookAtRH(position, position + forward, kWorldUp);
    }

    void FirstPersonCamera::Update(float deltaTime, const CameraInput& input, const CameraTarget& target, float aspect)
    {
        common.projMatrix = common.MakeProjection(aspect);

        // Mouse and Q/E always turn the head in first person (no RMB gate). WASD is reserved as "character
        // input" but this sample has no locomotion system, so it is intentionally unused.
        yawDeg = WrapDegrees(yawDeg + input.mouseDelta.x * mouseSensitivity);
        pitchDeg = glm::clamp(pitchDeg - input.mouseDelta.y * mouseSensitivity, -89.0f, 89.0f);
        rollDeg = WrapDegrees(rollDeg + input.roll * kFirstPersonRollSpeed * deltaTime);

        if (!target.hasHead)
        {
            // Nothing to ride (no selection or head joint not found): retain the previous view.
            return;
        }

        // Combine the head joint's world orientation with the user's yaw/pitch/roll offset. The view looks
        // down the eye's local -Z (glm camera convention); which world direction that is depends on the
        // rig's head-joint axes, so eyeOffset/angles let the user tune it.
        const glm::quat headRot = glm::quat_cast(glm::mat3(target.headWorld));
        const glm::quat finalRot = headRot * glm::quat(glm::radians(glm::vec3(pitchDeg, yawDeg, rollDeg)));

        // Anchor at the head joint (+ a head-local fine offset), then push forward along the look direction
        // so the eye clears the character's own head mesh. forwardPush follows the actual view axis, so it
        // is rig-independent.
        const glm::vec3 lookForward = finalRot * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 eyePos =
                glm::vec3(target.headWorld[3]) + glm::mat3(target.headWorld) * eyeOffset + lookForward * forwardPush;

        const glm::mat4 eyeXform = glm::translate(glm::mat4(1.0f), eyePos) * glm::mat4_cast(finalRot);
        common.viewMatrix = glm::inverse(eyeXform);
    }

    void ThirdPersonCamera::Update(float deltaTime, const CameraInput& input, const CameraTarget& target, float aspect)
    {
        common.projMatrix = common.MakeProjection(aspect);

        const glm::vec3 tgtPos = target.hasTarget ? target.targetWorldPos : smoothedTarget;
        if (!smoothInit)
        {
            smoothedTarget = tgtPos;
            smoothInit = true;
        }
        else
        {
            smoothedTarget = ExpDamp(smoothedTarget, tgtPos, damping, deltaTime);
        }

        if (input.lookActive)
        {
            yawDeg = WrapDegrees(yawDeg + input.mouseDelta.x * mouseSensitivity);
            pitchDeg = glm::clamp(pitchDeg - input.mouseDelta.y * mouseSensitivity, -89.0f, 89.0f);
        }
        distance = glm::clamp(distance - input.scroll * scrollSpeed, 0.5f, 50.0f);

        // ForwardFromAngles is the view direction (camera -> target); place the camera behind it.
        const glm::vec3 forward = ForwardFromAngles(yawDeg, pitchDeg);
        const glm::vec3 position = smoothedTarget - forward * distance;
        common.viewMatrix = glm::lookAtRH(position, smoothedTarget, kWorldUp);
    }

    void StationaryCamera::Update(float deltaTime, const CameraTarget& target, float aspect)
    {
        common.projMatrix = common.MakeProjection(aspect);

        const glm::vec3 tgtPos = target.hasTarget ? target.targetWorldPos : smoothedTarget;
        if (!smoothInit)
        {
            smoothedTarget = tgtPos;
            smoothInit = true;
        }
        else
        {
            smoothedTarget = ExpDamp(smoothedTarget, tgtPos, damping, deltaTime);
        }

        glm::vec3 lookAt = smoothedTarget;
        if (glm::distance(lookAt, position) < 1e-4f)
        {
            lookAt = position + glm::vec3(0.0f, 0.0f, -1.0f); // avoid a degenerate lookAt
        }
        common.viewMatrix = glm::lookAtRH(position, lookAt, kWorldUp);
    }

    CameraCommon& CameraRig::ActiveCommon()
    {
        switch (ActiveType())
        {
            case CameraType::FirstPerson:
                return firstPerson.common;
            case CameraType::ThirdPerson:
                return thirdPerson.common;
            case CameraType::Stationary:
                return stationary.common;
            case CameraType::Free:
            default:
                return free.common;
        }
    }

    const CameraCommon& CameraRig::ActiveCommon() const
    {
        return const_cast<CameraRig*>(this)->ActiveCommon();
    }

    void CameraRig::Update(float deltaTime, const CameraInput& input, const CameraTarget& target, float aspect)
    {
        switch (ActiveType())
        {
            case CameraType::Free:
                free.Update(deltaTime, input, aspect);
                break;
            case CameraType::FirstPerson:
                firstPerson.Update(deltaTime, input, target, aspect);
                break;
            case CameraType::ThirdPerson:
                thirdPerson.Update(deltaTime, input, target, aspect);
                break;
            case CameraType::Stationary:
                stationary.Update(deltaTime, target, aspect);
                break;
        }
    }

    void CameraRig::ResetActive()
    {
        switch (ActiveType())
        {
            case CameraType::Free:
                free.Reset();
                break;
            case CameraType::FirstPerson:
                firstPerson.Reset();
                break;
            case CameraType::ThirdPerson:
                thirdPerson.Reset();
                break;
            case CameraType::Stationary:
                stationary.Reset();
                break;
        }
    }
} // namespace RAnimation
