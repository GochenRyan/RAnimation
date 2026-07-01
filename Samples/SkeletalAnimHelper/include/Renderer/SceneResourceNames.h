#pragma once

// Centralized buffer/view name constants used with RenderResourceRegistry's shared resource pool.
// Each pass calls RegisterSharedBuffer/View with these names; first registration wins for the
// BufferDesc, subsequent registrations of the same name return the existing handle (with desc
// equivalence checked).
namespace RAnimation
{
    namespace SceneResourceNames
    {
        inline constexpr const char* kCameraBuffer = "CameraBuffer";
        inline constexpr const char* kWorldMatrixBuffer = "WorldMatrixBuffer";
        inline constexpr const char* kNodeTransformBuffer = "NodeTransformBuffer";
        inline constexpr const char* kTRSMatrixBuffer = "TRSMatrixBuffer";
        inline constexpr const char* kModelRootMatrixBuffer = "ModelRootMatrixBuffer";
        inline constexpr const char* kNodeParentIndexBuffer = "NodeParentIndexBuffer";
        inline constexpr const char* kBoneNodeIndexBuffer = "BoneNodeIndexBuffer";
        inline constexpr const char* kBoneOffsetMatrixBuffer = "BoneOffsetMatrixBuffer";
        inline constexpr const char* kBoneMatrixBuffer = "BoneMatrixBuffer";

        inline constexpr const char* kCameraBufferView = "CameraBufferView";
        inline constexpr const char* kWorldMatrixBufferView = "WorldMatrixBufferView";
        inline constexpr const char* kNodeTransformBufferView = "NodeTransformBufferView";
        inline constexpr const char* kTRSMatrixBufferView = "TRSMatrixBufferView";
        inline constexpr const char* kTRSMatrixStorageView = "TRSMatrixStorageView";
        inline constexpr const char* kModelRootMatrixBufferView = "ModelRootMatrixBufferView";
        inline constexpr const char* kNodeParentIndexBufferView = "NodeParentIndexBufferView";
        inline constexpr const char* kBoneNodeIndexBufferView = "BoneNodeIndexBufferView";
        inline constexpr const char* kBoneOffsetMatrixBufferView = "BoneOffsetMatrixBufferView";
        inline constexpr const char* kBoneMatrixBufferView = "BoneMatrixBufferView";
        inline constexpr const char* kBoneMatrixStorageView = "BoneMatrixStorageView";
    } // namespace SceneResourceNames
} // namespace RAnimation
