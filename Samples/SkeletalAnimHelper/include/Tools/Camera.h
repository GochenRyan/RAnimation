#pragma once
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace RAnimation
{
    // The four fixed camera behaviours. The rig has one instance of each; CameraRig::active selects which
    // one is live. Type is a property of the slot, not a runtime-switchable field.
    enum class CameraType : int
    {
        Free = 0,        // free-fly: WASD + Q/E move, RMB-drag looks
        FirstPerson,     // rides the selected instance's head joint; mouse + Q/E turn the head
        ThirdPerson,     // orbits the selected instance's head; exp-lerp damped follow
        Stationary       // fixed position, smoothly tracks the selected instance's centre
    };

    enum class ProjectionType : int
    {
        Perspective = 0,
        Orthographic
    };

    // Per-frame input for the active camera, gathered from ImGui IO by the caller (Renderer). Kept free
    // of any windowing/ImGui type so the cameras stay testable and platform-agnostic.
    struct CameraInput
    {
        glm::vec2 mouseDelta = glm::vec2(0.0f); // pixels this frame
        bool lookActive = false;                // RMB held (Free/ThirdPerson mouse-look gate)
        float moveForward = 0.0f;               // W(+)/S(-)
        float moveRight = 0.0f;                 // D(+)/A(-)
        float moveUp = 0.0f;                    // E(+)/Q(-) for Free
        float roll = 0.0f;                      // E(+)/Q(-) head roll for FirstPerson
        float scroll = 0.0f;                    // mouse wheel (ThirdPerson distance)
    };

    // Follow target for FirstPerson/ThirdPerson/Stationary: the currently selected instance. targetWorldPos
    // is the point to follow/look at; headWorld is the world transform of the head joint (FirstPerson).
    struct CameraTarget
    {
        bool hasTarget = false;
        glm::vec3 targetWorldPos = glm::vec3(0.0f);
        bool hasHead = false;
        glm::mat4 headWorld = glm::mat4(1.0f);
    };

    // Projection parameters + computed matrices shared by every camera type. MakeProjection builds a
    // reversed-Z RH matrix (z in [1,0]); the scene pipelines clear depth to 0 and compare GREATER_EQUAL.
    struct CameraCommon
    {
        ProjectionType projection = ProjectionType::Perspective;
        float fovDeg = 60.0f;         // vertical FOV (perspective)
        float nearZ = 0.1f;
        float farZ = 500.0f;
        float orthoHalfHeight = 5.0f; // half of the vertical extent (orthographic)

        glm::mat4 viewMatrix = glm::mat4(1.0f);
        glm::mat4 projMatrix = glm::mat4(1.0f);

        glm::mat4 MakeProjection(float aspect) const;
    };

    // Free-fly camera: absolute placement, WASD/Q-E move, RMB-drag look.
    struct FreeCamera
    {
        CameraCommon common;
        glm::vec3 position = glm::vec3(2.0f, 5.0f, 7.0f);
        // Unreal-style naming: yaw about world up (0 = +X, toward +Z), pitch = elevation (positive = up).
        // This is a genuine self-rotating yaw/pitch, not an orbit (contrast ThirdPerson).
        float yawDeg = 330.0f;
        float pitchDeg = -20.0f;
        float moveSpeed = 5.0f; // metres/second
        float mouseSensitivity = 0.15f;

        void Update(float deltaTime, const CameraInput& input, float aspect);
        void Reset() { *this = FreeCamera{}; }
    };

    // First-person camera: rides the target's head joint, mouse + Q/E turn the head, pushed forward out
    // of the head mesh along the look direction.
    struct FirstPersonCamera
    {
        CameraCommon common;
        std::string headBoneName = "Head";
        glm::vec3 eyeOffset = glm::vec3(0.0f, 0.05f, 0.0f); // head-local offset to the eye
        float forwardPush = 0.3f;                           // push along the look direction, out of the mesh
        float yawDeg = 0.0f;                                // look offset added to the head orientation
        float pitchDeg = 0.0f;
        float rollDeg = 0.0f;
        float mouseSensitivity = 0.15f;

        void Update(float deltaTime, const CameraInput& input, const CameraTarget& target, float aspect);
        void Reset() { *this = FirstPersonCamera{}; }
    };

    // Third-person camera: orbits the target (its head) at a distance, with exp-lerp damped follow.
    struct ThirdPersonCamera
    {
        CameraCommon common;
        std::string headBoneName = "Head";
        float distance = 5.0f; // orbit radius (Unreal's "arm length")
        // Named yaw/pitch for consistency (Unreal represents everything as FRotator), but this is really
        // an orbit: yaw is the azimuth (about world up, from +X) and pitch is the elevation of the camera
        // on a sphere of radius `distance` around the target. The UI labels annotate that.
        float pitchDeg = 15.0f;
        float yawDeg = 315.0f;
        float damping = 8.0f; // exp-lerp rate (larger = snappier)
        float mouseSensitivity = 0.15f;
        float scrollSpeed = 1.0f;

        glm::vec3 smoothedTarget = glm::vec3(0.0f); // not serialized
        bool smoothInit = false;

        void Update(float deltaTime, const CameraInput& input, const CameraTarget& target, float aspect);
        void Reset() { *this = ThirdPersonCamera{}; }
    };

    // Stationary camera: fixed position, smoothly tracks the target's centre.
    struct StationaryCamera
    {
        CameraCommon common;
        glm::vec3 position = glm::vec3(0.0f, 3.0f, 8.0f);
        float damping = 5.0f;

        glm::vec3 smoothedTarget = glm::vec3(0.0f); // not serialized
        bool smoothInit = false;

        void Update(float deltaTime, const CameraTarget& target, float aspect);
        void Reset() { *this = StationaryCamera{}; }
    };

    // The camera rig: one instance of each type plus the active slot index. Pure value type (no pointers,
    // no polymorphism), so it lives directly in RRenderData, copies for undo/import, and serializes flat.
    struct CameraRig
    {
        FreeCamera free;
        FirstPersonCamera firstPerson;
        ThirdPersonCamera thirdPerson;
        StationaryCamera stationary;
        int active = 0; // CameraType index

        CameraType ActiveType() const { return static_cast<CameraType>(active); }

        // The active slot's shared projection/matrix block (for the matrix upload and projection toggle).
        CameraCommon& ActiveCommon();
        const CameraCommon& ActiveCommon() const;

        // Update the active camera from this frame's input + target.
        void Update(float deltaTime, const CameraInput& input, const CameraTarget& target, float aspect);

        // Reset the active camera to its defaults (type preserved).
        void ResetActive();
    };
} // namespace RAnimation
