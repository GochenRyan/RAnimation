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

    void CameraTarget::SetHeadTransform(const glm::mat4& world, float scale)
    {
        headStatus = HeadPoseStatus::InvalidTransform;
        if (!std::isfinite(scale) || scale <= 0.0f)
        {
            return;
        }
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                if (!std::isfinite(world[c][r]))
                {
                    return;
                }
            }
        }
        constexpr float tolerance = 1e-4f;
        if (std::abs(world[0][3]) > tolerance || std::abs(world[1][3]) > tolerance ||
            std::abs(world[2][3]) > tolerance || std::abs(world[3][3] - 1.0f) > tolerance)
        {
            return;
        }

        glm::mat3 basis(world);
        for (int c = 0; c < 3; ++c)
        {
            const float length = glm::length(basis[c]);
            if (!std::isfinite(length) || length <= 1e-8f)
            {
                return;
            }
            basis[c] /= length;
        }
        if (std::abs(glm::dot(basis[0], basis[1])) > tolerance ||
            std::abs(glm::dot(basis[0], basis[2])) > tolerance ||
            std::abs(glm::dot(basis[1], basis[2])) > tolerance || glm::determinant(basis) <= 0.0f)
        {
            return;
        }

        headPosition = glm::vec3(world[3]);
        headRotation = glm::normalize(glm::quat_cast(basis));
        instanceScale = scale;
        headStatus = HeadPoseStatus::Valid;
    }

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

        yawDeg = WrapDegrees(yawDeg + input.mouseDelta.x * mouseSensitivity);
        pitchDeg = glm::clamp(pitchDeg - input.mouseDelta.y * mouseSensitivity, -89.0f, 89.0f);
        rollDeg = WrapDegrees(rollDeg + input.roll * kFirstPersonRollSpeed * deltaTime);

        headStatus = target.headStatus;
        if (headStatus != HeadPoseStatus::Valid)
        {
            return;
        }

        const glm::quat finalRot = glm::normalize(
                target.headRotation * glm::quat(glm::radians(glm::vec3(pitchDeg, yawDeg, rollDeg))));

        const glm::vec3 lookForward = finalRot * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 eyePos = target.headPosition +
                                (target.headRotation * eyeOffset + lookForward * forwardPush) * target.instanceScale;

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
