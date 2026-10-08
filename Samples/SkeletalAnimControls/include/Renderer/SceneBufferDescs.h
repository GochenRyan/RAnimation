#pragma once

// Centralized BufferDesc factory functions used by multiple passes via DeclareResources.
// First pass in PassRegistry order to call RegisterSharedBuffer(name, desc) registers the
// buffer; subsequent passes that share the same name pass the same desc to satisfy the
// equivalence check.

#include <cstdint>

#include <glm/glm.hpp>

#include <AnimGraph/HybridWork.h>
#include <Model/RenderData.h>
#include <Renderer/RenderResourceBudget.h>
#include <Renderer/SceneResourceNames.h>

namespace RAnimation
{
    namespace SceneBufferDescs
    {
        inline BufferDesc Camera()
        {
            return {SceneResourceNames::kCameraBuffer,
                    sizeof(RUploadMatrices),
                    1,
                    0,
                    nri::BufferUsageBits::CONSTANT_BUFFER,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc WorldMatrix(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kWorldMatrixBuffer,
                    sizeof(glm::mat4),
                    budget.maxWorldMatrices,
                    sizeof(glm::mat4),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc BoneMatrix(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kBoneMatrixBuffer,
                    sizeof(glm::mat4),
                    budget.GetMaxBoneMatrices(),
                    sizeof(glm::mat4),
                    nri::BufferUsageBits::SHADER_RESOURCE | nri::BufferUsageBits::SHADER_RESOURCE_STORAGE,
                    nri::MemoryLocation::DEVICE,
                    true};
        }

        inline BufferDesc NodeTransform(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kNodeTransformBuffer,
                    sizeof(RNodeTransformData),
                    budget.GetMaxNodeTransforms(),
                    sizeof(RNodeTransformData),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc TRSMatrix(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kTRSMatrixBuffer,
                    sizeof(glm::mat4),
                    budget.GetMaxNodeTransforms(),
                    sizeof(glm::mat4),
                    nri::BufferUsageBits::SHADER_RESOURCE | nri::BufferUsageBits::SHADER_RESOURCE_STORAGE,
                    nri::MemoryLocation::DEVICE,
                    true};
        }

        inline BufferDesc ModelRootMatrix(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kModelRootMatrixBuffer,
                    sizeof(glm::mat4),
                    budget.maxWorldMatrices,
                    sizeof(glm::mat4),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc NodeParentIndex(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kNodeParentIndexBuffer,
                    sizeof(int32_t),
                    budget.GetMaxNodeTransforms(),
                    sizeof(int32_t),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc BoneNodeIndex(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kBoneNodeIndexBuffer,
                    sizeof(uint32_t),
                    budget.GetMaxBoneMatrices(),
                    sizeof(uint32_t),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc BoneOffsetMatrix(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kBoneOffsetMatrixBuffer,
                    sizeof(glm::mat4),
                    budget.GetMaxBoneMatrices(),
                    sizeof(glm::mat4),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        // ---- hybrid_eval inputs (HOST_UPLOAD, per queued frame) ----
        inline BufferDesc HybridWork(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kHybridWorkBuffer,
                    sizeof(RAnimation::HybridWork),
                    budget.maxWorldMatrices,
                    sizeof(RAnimation::HybridWork),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc HybridTerm(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kHybridTermBuffer,
                    sizeof(RAnimation::HybridTerm),
                    budget.maxWorldMatrices * kMaxTermsPerWork,
                    sizeof(RAnimation::HybridTerm),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }

        inline BufferDesc CrowdInstance(const RenderResourceBudget& budget)
        {
            return {SceneResourceNames::kCrowdInstanceBuffer,
                    sizeof(CrowdInstanceData),
                    budget.maxWorldMatrices,
                    sizeof(CrowdInstanceData),
                    nri::BufferUsageBits::SHADER_RESOURCE,
                    nri::MemoryLocation::HOST_UPLOAD,
                    true};
        }
    } // namespace SceneBufferDescs
} // namespace RAnimation
