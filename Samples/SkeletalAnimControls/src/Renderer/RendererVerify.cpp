// Renderer: GPU -> CPU parity check used by the command-line smoke test (--verify). Reads one instance's
// skinning matrices back from the BoneMatrix buffer of the last rendered frame and compares them with the
// CPU evaluation of the same pose (Model::ComputeNodeGlobals * inverse bind).

#include <algorithm>
#include <cmath>
#include <cstring>

#include <fmt/format.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/AnimationSystem.h>
#include <AnimGraph/HybridWork.h>
#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>
#include <Renderer/Renderer.h>
#include <Renderer/SceneResourceNames.h>
#include <RHIWrap/Helper.h>

using namespace RAnimation;

bool Renderer::VerifyBoneMatrices(ModelAndInstanceData& scene, int instanceIndex, BoneVerifyResult& out)
{
    out = BoneVerifyResult{};
    if (instanceIndex < 0 || instanceIndex >= static_cast<int>(scene.miModelInstances.size()))
    {
        out.info = "no such instance";
        return false;
    }
    if (mSceneFrame.animatedGroups == nullptr)
    {
        out.info = "no animated groups were published";
        return false;
    }
    const std::shared_ptr<ModelInstance> instance = scene.miModelInstances[static_cast<size_t>(instanceIndex)];
    const std::shared_ptr<Model> model = instance->GetModel();

    // Locate the instance's group and its evaluation unit (instance slot, or track in tier C).
    const AnimatedGroup* group = nullptr;
    for (const AnimatedGroup& g : *mSceneFrame.animatedGroups)
    {
        if (g.model == model.get())
        {
            group = &g;
            break;
        }
    }
    if (group == nullptr)
    {
        out.info = "instance's model has no animated group this frame";
        return false;
    }
    uint32_t unit = 0;
    if (group->isTracks)
    {
        unit = static_cast<uint32_t>(AnimationSystem::TrackIndexFor(instance->GetInstanceSettings(), scene.miTrackPhases));
    }
    else
    {
        const auto& list = scene.miModelInstancesPerModel.at(model->GetModelFileName());
        const auto it = std::find(list.begin(), list.end(), instance);
        if (it == list.end())
        {
            out.info = "instance not found in its model group";
            return false;
        }
        unit = static_cast<uint32_t>(std::distance(list.begin(), it));
    }
    if (unit >= group->gpu.instanceCount)
    {
        out.info = "evaluation unit out of range";
        return false;
    }

    const uint32_t boneCount = group->gpu.numberOfBones;
    const uint32_t firstMatrix = group->gpu.boneMatrixOffset + unit * boneCount;
    const uint64_t bytes = static_cast<uint64_t>(boneCount) * sizeof(glm::mat4);

    // ---- read the matrices back ----------------------------------------------------------------------
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));

    const BufferHandle boneHandle = mRenderData.rdResourceRegistry.FindBuffer(SceneResourceNames::kBoneMatrixBuffer);
    if (!boneHandle.IsValid())
    {
        out.info = "BoneMatrix buffer not registered";
        return false;
    }
    nri::Buffer* boneBuffer = mRenderData.rdResourceRegistry.GetBuffer(boneHandle);
    const uint64_t srcOffset = mRenderData.rdResourceRegistry.GetOffsetForFrame(boneHandle, mRenderData.queuedFrameIndex) +
                               static_cast<uint64_t>(firstMatrix) * sizeof(glm::mat4);

    nri::Buffer* readback = nullptr;
    nri::Memory* readbackMemory = nullptr;
    nri::BufferDesc readbackDesc = {};
    readbackDesc.size = bytes;
    readbackDesc.usage = nri::BufferUsageBits::NONE;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateBuffer(*mRenderData.rdDevice, readbackDesc, readback));
    nri::ResourceGroupDesc group_ = {};
    group_.memoryLocation = nri::MemoryLocation::HOST_READBACK;
    group_.bufferNum = 1;
    group_.buffers = &readback;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.AllocateAndBindMemory(*mRenderData.rdDevice, group_, &readbackMemory));

    nri::CommandAllocator* allocator = nullptr;
    nri::CommandBuffer* commandBuffer = nullptr;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateCommandAllocator(*mRenderData.rdGraphicsQueue, allocator));
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.CreateCommandBuffer(*allocator, commandBuffer));
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.BeginCommandBuffer(*commandBuffer, nullptr));

    // The last pass that touched the buffer declared SHADER_RESOURCE @ VERTEX_SHADER (SkinnedMeshDrawPass);
    // move it to COPY_SOURCE for the copy and back so the registry's barrier tracking stays consistent.
    nri::BufferBarrierDesc toCopy = {};
    toCopy.buffer = boneBuffer;
    toCopy.before = {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::VERTEX_SHADER};
    toCopy.after = {nri::AccessBits::COPY_SOURCE, nri::StageBits::COPY};
    nri::BarrierDesc toCopyDesc = {};
    toCopyDesc.buffers = &toCopy;
    toCopyDesc.bufferNum = 1;
    mRenderData.NRI.CmdBarrier(*commandBuffer, toCopyDesc);

    mRenderData.NRI.CmdCopyBuffer(*commandBuffer, *readback, 0, *boneBuffer, srcOffset, bytes);

    nri::BufferBarrierDesc back = {};
    back.buffer = boneBuffer;
    back.before = {nri::AccessBits::COPY_SOURCE, nri::StageBits::COPY};
    back.after = {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::VERTEX_SHADER};
    nri::BarrierDesc backDesc = {};
    backDesc.buffers = &back;
    backDesc.bufferNum = 1;
    mRenderData.NRI.CmdBarrier(*commandBuffer, backDesc);

    NRI_ABORT_ON_FAILURE(mRenderData.NRI.EndCommandBuffer(*commandBuffer));

    nri::QueueSubmitDesc submit = {};
    submit.commandBuffers = &commandBuffer;
    submit.commandBufferNum = 1;
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueSubmit(*mRenderData.rdGraphicsQueue, submit));
    NRI_ABORT_ON_FAILURE(mRenderData.NRI.QueueWaitIdle(mRenderData.rdGraphicsQueue));

    std::vector<glm::mat4> gpu(boneCount);
    const void* mapped = mRenderData.NRI.MapBuffer(*readback, 0, bytes);
    std::memcpy(gpu.data(), mapped, static_cast<size_t>(bytes));
    mRenderData.NRI.UnmapBuffer(*readback);

    mRenderData.NRI.DestroyCommandBuffer(commandBuffer);
    mRenderData.NRI.DestroyCommandAllocator(allocator);
    mRenderData.NRI.DestroyBuffer(readback);
    mRenderData.NRI.FreeMemory(readbackMemory);

    // ---- CPU reference --------------------------------------------------------------------------------
    const glm::mat4 instanceLocal = group->isTracks ? glm::mat4(1.0f) : instance->GetLocalTransformMatrix();
    const auto& bones = model->GetBoneList();
    const uint32_t nodeCount = static_cast<uint32_t>(model->GetNodeList().size());

    auto compare = [&](const std::vector<Transform>& referencePose, float& maxPosition, float& maxElement)
    {
        Pose view;
        view.bones = const_cast<Transform*>(referencePose.data());
        view.count = static_cast<uint32_t>(referencePose.size());
        std::vector<glm::mat4> globals;
        model->ComputeNodeGlobals(view, instanceLocal, globals);
        for (const auto& bone : bones)
        {
            const uint32_t b = bone->GetBoneId();
            const int32_t nodeIndex = model->FindNodeIndex(bone->GetBoneName());
            if (b >= boneCount || nodeIndex < 0)
            {
                continue;
            }
            const glm::mat4 expected = globals[static_cast<size_t>(nodeIndex)] * bone->GetOffsetMatrix();
            const glm::mat4& actual = gpu[b];
            maxPosition = std::max(maxPosition, glm::length(glm::vec3(expected[3]) - glm::vec3(actual[3])));
            for (int c = 0; c < 4; ++c)
            {
                for (int r = 0; r < 4; ++r)
                {
                    maxElement = std::max(maxElement, std::fabs(expected[c][r] - actual[c][r]));
                }
            }
        }
    };

    // 1) Reference A: the CPU pose sampled straight from the keyframes (what the CPU tier renders).
    compare(instance->EnsureCpuPose(), out.maxPositionError, out.maxElementError);

    // 2) Reference B: a CPU mirror of exactly what the kernel computed - the same Work terms, texture rows
    //    sampled with BoneTrsTexture::Sample (the HLSL SampleTrs twin) and the same weighted nlerp.
    {
        struct MirrorTerm
        {
            bool slot = false;
            uint32_t index = 0;
            float timeSec = 0.0f;
            float weight = 0.0f;
        };
        std::vector<MirrorTerm> terms;
        const HybridWorkCpu* work = nullptr;
        if (group->isTracks)
        {
            const int phases = std::max(1, scene.miTrackPhases);
            const uint32_t clip = unit / static_cast<uint32_t>(phases);
            const int bucket = static_cast<int>(unit % static_cast<uint32_t>(phases));
            const float t = AnimationSystem::TrackClipTimeSec(*model->GetAnimClips()[clip], bucket, phases, scene.miTrackGlobalTimeSec);
            terms.push_back({false, clip, t, 1.0f});
        }
        else if (AnimGraphInstance* graph = instance->GetGraph(); graph != nullptr && graph->Usable() && !graph->Work().terms.empty())
        {
            work = &graph->Work();
            for (const HybridTermCpu& t : work->terms)
            {
                terms.push_back({t.bSlot, t.index, t.timeSec, t.weight});
            }
        }

        const bool textureOk = model->IsTrsTexReady();
        bool mirrorPossible = !terms.empty();
        for (const MirrorTerm& t : terms)
        {
            mirrorPossible = mirrorPossible && (t.slot || textureOk);
        }
        if (mirrorPossible)
        {
            std::vector<Transform> mirror(nodeCount);
            std::vector<glm::quat> reference(nodeCount, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            std::vector<bool> first(nodeCount, true);
            for (Transform& t : mirror)
            {
                t.T = glm::vec3(0.0f);
                t.S = glm::vec3(0.0f);
                t.R = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
            }
            std::vector<TrsNodeTransform> sampled(nodeCount);
            for (const MirrorTerm& term : terms)
            {
                for (uint32_t n = 0; n < nodeCount; ++n)
                {
                    Transform v;
                    if (term.slot)
                    {
                        v = work->slotData[static_cast<size_t>(term.index) * nodeCount + n];
                    }
                    else
                    {
                        if (n == 0)
                        {
                            model->GetTrsTexCpu().Sample(term.index, term.timeSec, scene.miTrsInterpolate, sampled.data());
                        }
                        v.T = sampled[n].T;
                        v.R = sampled[n].R;
                        v.S = sampled[n].S;
                    }
                    if (first[n])
                    {
                        reference[n] = v.R;
                        first[n] = false;
                    }
                    const glm::quat aligned = glm::dot(reference[n], v.R) < 0.0f ? -v.R : v.R;
                    mirror[n].T += v.T * term.weight;
                    mirror[n].S += v.S * term.weight;
                    mirror[n].R += aligned * term.weight;
                }
            }
            for (Transform& t : mirror)
            {
                t.R = glm::normalize(t.R);
            }
            compare(mirror, out.mirrorPositionError, out.mirrorElementError);
            out.hasMirror = true;
        }
    }
    out.comparedBones = boneCount;
    out.ok = true;
    out.info = fmt::format("instance {} unit {} ({}), {} bones", instanceIndex, unit, group->isTracks ? "track" : "instance", boneCount);
    return true;
}
