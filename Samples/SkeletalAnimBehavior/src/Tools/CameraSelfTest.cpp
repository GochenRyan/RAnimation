#include <Tools/CameraSelfTest.h>
#include <Tools/Camera.h>

#include <cmath>
#include <limits>

#include <fmt/base.h>
#include <glm/gtc/matrix_transform.hpp>

namespace RAnimation
{
    namespace
    {
        bool Near(const glm::mat4& a, const glm::mat4& b, float tolerance = 2e-4f)
        {
            for (int c = 0; c < 4; ++c)
            {
                for (int r = 0; r < 4; ++r)
                {
                    if (!std::isfinite(a[c][r]) || std::abs(a[c][r] - b[c][r]) > tolerance)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    } // namespace

    int RunCameraSelfTests()
    {
        int checks = 0;
        int failures = 0;
        const auto check = [&](bool ok, const char* name)
        {
            ++checks;
            failures += ok ? 0 : 1;
            fmt::print("  [{}] {}\n", ok ? " ok " : "FAIL", name);
        };

        const glm::vec3 position(2.0f, 1.57f, -3.0f);
        const glm::quat rotation = glm::quat(glm::radians(glm::vec3(13.0f, 137.0f, -7.0f)));
        const glm::mat4 rigid = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation);
        const CameraInput input;
        constexpr float aspect = 16.0f / 9.0f;

        for (float instanceScale : {0.5f, 1.0f, 2.0f})
        {
            for (float skeletonScale : {1.0f, 100.0f})
            {
                CameraTarget target;
                target.SetHeadTransform(glm::scale(rigid, glm::vec3(instanceScale * skeletonScale)), instanceScale);
                check(target.headStatus == HeadPoseStatus::Valid, "scaled head accepted");
                check(glm::length(target.headPosition - position) < 1e-6f, "world head position preserved");

                FirstPersonCamera camera;
                camera.yawDeg = 23.0f;
                camera.pitchDeg = -11.0f;
                camera.rollDeg = 9.0f;
                camera.eyeOffset = glm::vec3(0.02f, 0.05f, -0.01f);
                camera.Update(0.0f, input, target, aspect);

                const glm::quat look = rotation * glm::quat(glm::radians(glm::vec3(-11.0f, 23.0f, 9.0f)));
                const glm::vec3 forward = look * glm::vec3(0.0f, 0.0f, -1.0f);
                const glm::vec3 eye = position +
                                      (rotation * camera.eyeOffset + forward * camera.forwardPush) * instanceScale;
                const glm::mat4 expected = glm::lookAtRH(eye, eye + forward, look * glm::vec3(0.0f, 1.0f, 0.0f));
                check(Near(camera.common.viewMatrix, expected),
                      "view uses rigid rotation and scales offsets exactly once");
                const glm::mat3 viewBasis(camera.common.viewMatrix);
                check(Near(glm::mat4(glm::transpose(viewBasis) * viewBasis), glm::mat4(1.0f)) &&
                              std::abs(glm::determinant(viewBasis) - 1.0f) < 1e-5f,
                      "view basis is orthonormal and right-handed");
                check(Near(camera.common.projMatrix, camera.common.MakeProjection(aspect)),
                      "projection independent of scale");
            }
        }

        // UAL rest head: world translation is in metres, basis lengths are approximately 100.
        const glm::mat4 ualHead(glm::vec4(-100.0f, 0.0f, 0.0f, 0.0f),
                                glm::vec4(0.0f, 99.9471f, -3.25173f, 0.0f),
                                glm::vec4(0.0f, -3.25173f, -99.94711f, 0.0f),
                                glm::vec4(0.0f, 1.5687f, -0.0051f, 1.0f));
        CameraTarget ualTarget;
        ualTarget.SetHeadTransform(ualHead, 1.0f);
        FirstPersonCamera ualCamera;
        ualCamera.Update(0.0f, input, ualTarget, aspect);
        const glm::vec3 ualEye(glm::inverse(ualCamera.common.viewMatrix)[3]);
        check(ualTarget.headStatus == HeadPoseStatus::Valid && std::abs(glm::distance(ualEye, glm::vec3(ualHead[3])) -
                                                                        std::sqrt(0.05f * 0.05f + 0.3f * 0.3f)) < 1e-4f,
              "UAL rest head keeps the eye about 0.304m from the joint");

        CameraTarget target;
        target.SetHeadTransform(glm::scale(rigid, glm::vec3(2.0f, 3.0f, 4.0f)), 1.0f);
        check(target.headStatus == HeadPoseStatus::Valid &&
                      Near(glm::mat4_cast(target.headRotation), glm::mat4_cast(rotation)),
              "orthogonal nonuniform basis yields the same rotation");

        const glm::mat4 savedView = ualCamera.common.viewMatrix;
        const auto reject = [&](const glm::mat4& matrix, float scale, const char* name)
        {
            target.SetHeadTransform(matrix, scale);
            ualCamera.Update(0.0f, input, target, aspect);
            check(target.headStatus == HeadPoseStatus::InvalidTransform &&
                          ualCamera.headStatus == HeadPoseStatus::InvalidTransform &&
                          Near(ualCamera.common.viewMatrix, savedView),
                  name);
        };
        reject(glm::scale(rigid, glm::vec3(0.0f, 1.0f, 1.0f)), 1.0f, "zero head axis rejected without corrupting view");
        reject(glm::scale(rigid, glm::vec3(-1.0f, 1.0f, 1.0f)), 1.0f, "mirrored head rejected");
        glm::mat4 shear = rigid;
        shear[1] += shear[0] * 0.2f;
        reject(shear, 1.0f, "sheared head rejected");
        reject(rigid, 0.0f, "zero instance scale rejected");
        reject(rigid, -1.0f, "negative instance scale rejected");
        reject(rigid, std::numeric_limits<float>::infinity(), "infinite instance scale rejected");
        glm::mat4 invalid = rigid;
        invalid[3][0] = std::numeric_limits<float>::quiet_NaN();
        reject(invalid, 1.0f, "nonfinite head translation rejected");
        invalid = rigid;
        invalid[0][3] = 0.1f;
        reject(invalid, 1.0f, "nonaffine head transform rejected");

        target = CameraTarget{};
        ualCamera.Update(0.0f, input, target, aspect);
        check(ualCamera.headStatus == HeadPoseStatus::Missing && Near(ualCamera.common.viewMatrix, savedView),
              "missing head retains last valid view");
        target.SetHeadTransform(rigid, 1.0f);
        ualCamera.Update(0.0f, input, target, aspect);
        check(ualCamera.headStatus == HeadPoseStatus::Valid, "valid attachment recovers after invalid target");

        fmt::print("\nCamera result: {}/{} passed\n", checks - failures, checks);
        return failures;
    }
} // namespace RAnimation
