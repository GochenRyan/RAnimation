#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include <NRI.h>

namespace RAnimation
{
    enum class RenderResourceTier : uint8_t
    {
        Unsupported,
        Low,
        Medium,
        High,
    };

    enum class RenderResourceRejectReason : uint8_t
    {
        None,
        BelowMinimumAnimatedInstances,
    };

    struct RenderResourceBudgetPolicy
    {
        uint64_t minAnimatedInstances = 128;
        uint64_t recommendedAnimatedInstances = 1024;
        uint64_t preferredAnimatedInstances = 4096;
        uint64_t maxBonesPerInstance = 100;
        uint64_t maxNodesPerInstance = 100;

        uint64_t fallbackAnimationMemoryBudget = 32ull * 1024ull * 1024ull;
        uint64_t maxAnimationMemoryBudget = 256ull * 1024ull * 1024ull;
        uint32_t dedicatedMemoryBudgetPercent = 8;
        uint32_t sharedMemoryBudgetPercent = 4;
    };

    struct RenderResourceBudget
    {
        RenderResourceTier tier = RenderResourceTier::Unsupported;
        RenderResourceRejectReason rejectReason = RenderResourceRejectReason::BelowMinimumAnimatedInstances;

        uint64_t maxWorldMatrices = 0;
        uint64_t maxBonesPerInstance = 0;
        uint64_t maxNodesPerInstance = 0;

        uint64_t deviceLimitedWorldMatrices = 0;
        uint64_t memoryLimitedWorldMatrices = 0;
        uint64_t animationMemoryBudget = 0;
        uint64_t estimatedAnimationBufferBytesPerFrame = 0;
        uint64_t estimatedAnimationBufferBytesTotal = 0;

        bool IsSupported() const { return rejectReason == RenderResourceRejectReason::None; }
        uint64_t GetMaxBoneMatrices() const { return maxWorldMatrices * maxBonesPerInstance; }
        uint64_t GetMaxNodeTransforms() const { return maxWorldMatrices * maxNodesPerInstance; }

        static RenderResourceBudget CreateForDevice(const nri::DeviceDesc& deviceDesc,
                                                    uint32_t queuedFrameNum,
                                                    const RenderResourceBudgetPolicy& policy = RenderResourceBudgetPolicy{})
        {
            RenderResourceBudget budget = {};
            budget.maxBonesPerInstance = policy.maxBonesPerInstance;
            budget.maxNodesPerInstance = policy.maxNodesPerInstance;

            const uint64_t animationMemoryBudget = GetAnimationMemoryBudget(deviceDesc, policy);
            const uint64_t memoryLimitedWorldMatrices =
                    GetWorldMatrixLimitForBytes(animationMemoryBudget,
                                                queuedFrameNum,
                                                policy.maxBonesPerInstance,
                                                policy.maxNodesPerInstance);
            const uint64_t deviceLimitedWorldMatrices =
                    GetDeviceWorldMatrixLimit(deviceDesc,
                                              queuedFrameNum,
                                              policy.maxBonesPerInstance,
                                              policy.maxNodesPerInstance);
            const uint64_t selectedWorldMatrices = std::min({policy.preferredAnimatedInstances,
                                                             memoryLimitedWorldMatrices,
                                                             deviceLimitedWorldMatrices});

            budget.animationMemoryBudget = animationMemoryBudget;
            budget.memoryLimitedWorldMatrices = memoryLimitedWorldMatrices;
            budget.deviceLimitedWorldMatrices = deviceLimitedWorldMatrices;
            budget.maxWorldMatrices = selectedWorldMatrices;
            budget.estimatedAnimationBufferBytesPerFrame =
                    EstimateAnimationBufferBytesPerFrame(selectedWorldMatrices,
                                                         policy.maxBonesPerInstance,
                                                         policy.maxNodesPerInstance);
            budget.estimatedAnimationBufferBytesTotal =
                    SaturatingMultiply(budget.estimatedAnimationBufferBytesPerFrame, std::max<uint32_t>(1, queuedFrameNum));

            if (selectedWorldMatrices < policy.minAnimatedInstances)
            {
                budget.tier = RenderResourceTier::Unsupported;
                budget.rejectReason = RenderResourceRejectReason::BelowMinimumAnimatedInstances;
                return budget;
            }

            budget.rejectReason = RenderResourceRejectReason::None;
            if (selectedWorldMatrices >= policy.preferredAnimatedInstances)
            {
                budget.tier = RenderResourceTier::High;
            }
            else if (selectedWorldMatrices >= policy.recommendedAnimatedInstances)
            {
                budget.tier = RenderResourceTier::Medium;
            }
            else
            {
                budget.tier = RenderResourceTier::Low;
            }

            return budget;
        }

        static uint64_t EstimateAnimationBufferBytesPerFrame(uint64_t animatedInstances,
                                                             uint64_t maxBonesPerInstance,
                                                             uint64_t maxNodesPerInstance)
        {
            constexpr uint64_t matrixBytes = 64;
            constexpr uint64_t nodeTransformBytes = 48;
            constexpr uint64_t indexBytes = 4;

            const uint64_t worldBytes = SaturatingMultiply(animatedInstances, matrixBytes * 2);
            const uint64_t boneBytes = SaturatingMultiply(animatedInstances,
                                                          SaturatingMultiply(maxBonesPerInstance, matrixBytes * 2 + indexBytes));
            const uint64_t nodeBytes = SaturatingMultiply(animatedInstances,
                                                          SaturatingMultiply(maxNodesPerInstance,
                                                                             nodeTransformBytes + matrixBytes + indexBytes));

            return SaturatingAdd(SaturatingAdd(worldBytes, boneBytes), nodeBytes);
        }

    private:
        static uint64_t GetAnimationMemoryBudget(const nri::DeviceDesc& deviceDesc,
                                                 const RenderResourceBudgetPolicy& policy)
        {
            const bool usesSharedMemory =
                    deviceDesc.adapterDesc.architecture == nri::Architecture::INTEGRATED ||
                    deviceDesc.adapterDesc.videoMemorySize == 0;
            uint64_t reportedMemory = usesSharedMemory ? deviceDesc.adapterDesc.sharedSystemMemorySize :
                                                         deviceDesc.adapterDesc.videoMemorySize;
            if (reportedMemory == 0)
            {
                reportedMemory = std::max(deviceDesc.adapterDesc.videoMemorySize,
                                          deviceDesc.adapterDesc.sharedSystemMemorySize);
            }

            if (reportedMemory == 0)
            {
                return policy.fallbackAnimationMemoryBudget;
            }

            const uint32_t percent = usesSharedMemory ? policy.sharedMemoryBudgetPercent :
                                                       policy.dedicatedMemoryBudgetPercent;
            const uint64_t budget = SaturatingMultiply(reportedMemory, percent) / 100;
            return std::min(budget, policy.maxAnimationMemoryBudget);
        }

        static uint64_t GetDeviceWorldMatrixLimit(const nri::DeviceDesc& deviceDesc,
                                                  uint32_t queuedFrameNum,
                                                  uint64_t maxBonesPerInstance,
                                                  uint64_t maxNodesPerInstance)
        {
            constexpr uint64_t matrixBytes = 64;
            const uint64_t frameNum = std::max<uint32_t>(1, queuedFrameNum);
            uint64_t limit = std::numeric_limits<uint64_t>::max();

            if (deviceDesc.memory.storageBufferMaxRange > 0)
            {
                limit = std::min(limit,
                                 static_cast<uint64_t>(deviceDesc.memory.storageBufferMaxRange) /
                                         SaturatingMultiply(matrixBytes, maxBonesPerInstance));
                limit = std::min(limit,
                                 static_cast<uint64_t>(deviceDesc.memory.storageBufferMaxRange) /
                                         SaturatingMultiply(matrixBytes, maxNodesPerInstance));
            }

            if (deviceDesc.memory.bufferMaxSize > 0)
            {
                limit = std::min(limit,
                                 deviceDesc.memory.bufferMaxSize /
                                         SaturatingMultiply(SaturatingMultiply(matrixBytes, maxBonesPerInstance), frameNum));
                limit = std::min(limit,
                                 deviceDesc.memory.bufferMaxSize /
                                         SaturatingMultiply(SaturatingMultiply(matrixBytes, maxNodesPerInstance), frameNum));
            }

            return limit;
        }

        static uint64_t GetWorldMatrixLimitForBytes(uint64_t byteBudget,
                                                    uint32_t queuedFrameNum,
                                                    uint64_t maxBonesPerInstance,
                                                    uint64_t maxNodesPerInstance)
        {
            const uint64_t bytesPerFrame =
                    EstimateAnimationBufferBytesPerFrame(1, maxBonesPerInstance, maxNodesPerInstance);
            const uint64_t bytesPerInstance = SaturatingMultiply(bytesPerFrame, std::max<uint32_t>(1, queuedFrameNum));
            return bytesPerInstance > 0 ? byteBudget / bytesPerInstance : 0;
        }

        static uint64_t SaturatingAdd(uint64_t lhs, uint64_t rhs)
        {
            if (rhs > std::numeric_limits<uint64_t>::max() - lhs)
            {
                return std::numeric_limits<uint64_t>::max();
            }

            return lhs + rhs;
        }

        static uint64_t SaturatingMultiply(uint64_t lhs, uint64_t rhs)
        {
            if (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs)
            {
                return std::numeric_limits<uint64_t>::max();
            }

            return lhs * rhs;
        }
    };

    struct BufferDesc
    {
        const char* name = "";
        uint64_t elementSize = 0;
        uint64_t elementCount = 0;
        uint32_t structureStride = 0;
        nri::BufferUsageBits usage = nri::BufferUsageBits::NONE;
        nri::MemoryLocation memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
        bool perQueuedFrame = true;

        uint64_t GetUnalignedSize() const { return elementSize * elementCount; }
    };

    struct RenderResourceBudgetUsage
    {
        uint64_t staticWorldMatrices = 0;
        uint64_t animatedInstances = 0;
        uint64_t boneMatrices = 0;
        uint64_t nodeTransforms = 0;
        uint64_t uploadBytes = 0;
    };
} // namespace RAnimation
