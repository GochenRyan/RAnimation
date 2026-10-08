#include <Renderer/Passes/HybridEvalComputePass.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include <fmt/base.h>
#include <fmt/color.h>

#include <AnimGraph/AnimGraphInstance.h>
#include <AnimGraph/AnimationSystem.h>
#include <Model/ClipSampling.h>
#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>
#include <Model/RenderData.h>
#include <Renderer/NRITrsTexture.h>
#include <Renderer/RenderResourceRegistry.h>
#include <Renderer/SceneBufferDescs.h>
#include <Renderer/SceneFrameData.h>
#include <Renderer/SceneResourceNames.h>
#include <RHIWrap/Helper.h>
#include <TrsTexture/TrsTextureFile.h>

namespace RAnimation
{
    namespace
    {
        constexpr uint32_t kFlagInterpolate = 1u;

        RNodeTransformData PackTransform(const Transform& t)
        {
            RNodeTransformData d = {};
            d.translation = glm::vec4(t.T, 0.0f);
            d.scale = glm::vec4(t.S, 0.0f);
            d.rotation = glm::vec4(t.R.x, t.R.y, t.R.z, t.R.w); // explicit xyzw, never memcpy a glm::quat
            return d;
        }
    } // namespace

    bool HybridEvalComputePass::DeclareResources(ResourceContext& context)
    {
        mWorkBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kHybridWorkBuffer,
                                                            SceneBufferDescs::HybridWork(context.budget));
        mTermBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kHybridTermBuffer,
                                                            SceneBufferDescs::HybridTerm(context.budget));
        mCrowdInstanceBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kCrowdInstanceBuffer,
                                                                     SceneBufferDescs::CrowdInstance(context.budget));
        mSlotBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kNodeTransformBuffer,
                                                            SceneBufferDescs::NodeTransform(context.budget));
        mTRSMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kTRSMatrixBuffer,
                                                                 SceneBufferDescs::TRSMatrix(context.budget));

        // Skeleton tables (consumed by BoneMatrixComputePass, uploaded here).
        mNodeParentIndexBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kNodeParentIndexBuffer,
                                                                       SceneBufferDescs::NodeParentIndex(context.budget));
        mBoneNodeIndexBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneNodeIndexBuffer,
                                                                     SceneBufferDescs::BoneNodeIndex(context.budget));
        mBoneOffsetMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kBoneOffsetMatrixBuffer,
                                                                        SceneBufferDescs::BoneOffsetMatrix(context.budget));
        mModelRootMatrixBuffer = context.registry.RegisterSharedBuffer(SceneResourceNames::kModelRootMatrixBuffer,
                                                                       SceneBufferDescs::ModelRootMatrix(context.budget));

        mWorkView = context.registry.RegisterSharedView(SceneResourceNames::kHybridWorkBufferView,
                                                        mWorkBuffer,
                                                        nri::BufferViewType::SHADER_RESOURCE);
        mTermView = context.registry.RegisterSharedView(SceneResourceNames::kHybridTermBufferView,
                                                        mTermBuffer,
                                                        nri::BufferViewType::SHADER_RESOURCE);
        mSlotView = context.registry.RegisterSharedView(SceneResourceNames::kNodeTransformBufferView,
                                                        mSlotBuffer,
                                                        nri::BufferViewType::SHADER_RESOURCE);
        mTRSMatrixStorageView = context.registry.RegisterSharedView(SceneResourceNames::kTRSMatrixStorageView,
                                                                    mTRSMatrixBuffer,
                                                                    nri::BufferViewType::SHADER_RESOURCE_STORAGE);

        return mWorkBuffer.IsValid() && mTermBuffer.IsValid() && mCrowdInstanceBuffer.IsValid() &&
               mSlotBuffer.IsValid() && mTRSMatrixBuffer.IsValid() && mNodeParentIndexBuffer.IsValid() &&
               mBoneNodeIndexBuffer.IsValid() && mBoneOffsetMatrixBuffer.IsValid() &&
               mModelRootMatrixBuffer.IsValid() && mWorkView.IsValid() && mTermView.IsValid() &&
               mSlotView.IsValid() && mTRSMatrixStorageView.IsValid();
    }

    bool HybridEvalComputePass::CreatePipeline(RenderContext& context)
    {
        mDescriptorRanges0 = {
                {0, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},         // Works
                {1, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},         // Terms
                {2, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER},         // Slots
                {3, 1, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER}, // TRS out
        };
        mDescriptorRanges1 = {
                {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},           // TRS texture
                {1, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER}, // clip table
        };

        nri::DescriptorSetDesc setDescs[] = {
                {0, mDescriptorRanges0.data(), static_cast<uint32_t>(mDescriptorRanges0.size())},
                {1, mDescriptorRanges1.data(), static_cast<uint32_t>(mDescriptorRanges1.size())},
        };

        nri::RootConstantDesc rootConstantDesc = {};
        rootConstantDesc.registerIndex = 0;
        rootConstantDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        rootConstantDesc.size = sizeof(PushConstants);

        nri::PipelineLayoutDesc layoutDesc = {};
        layoutDesc.rootConstantNum = 1;
        layoutDesc.rootConstants = &rootConstantDesc;
        layoutDesc.rootRegisterSpace = 2;
        layoutDesc.descriptorSetNum = helper::GetCountOf(setDescs);
        layoutDesc.descriptorSets = setDescs;
        layoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        NRI_ABORT_ON_FAILURE(context.NRI.CreatePipelineLayout(context.device, layoutDesc, mPipelineLayout));

        // Published so NRITrsTexture::Load can allocate/fill per-model set 1 at model-load time.
        context.renderData.rdHybridEvalPipelineLayout = mPipelineLayout;

        const nri::DeviceDesc& deviceDesc = context.NRI.GetDeviceDesc(context.device);
        utils::ShaderCodeStorage shaderCodeStorage;
        nri::ShaderDesc shader = utils::LoadShader(deviceDesc.graphicsAPI,
                                                   SHADER_SRC_DIR "/SkeletalAnimControls/hybrid_eval.cs",
                                                   shaderCodeStorage);

        nri::ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.pipelineLayout = mPipelineLayout;
        pipelineDesc.shader = shader;
        NRI_ABORT_ON_FAILURE(context.NRI.CreateComputePipeline(context.device, pipelineDesc, mPipeline));
        return true;
    }

    DescriptorPoolRequirements HybridEvalComputePass::GetDescriptorPoolRequirements(uint32_t queuedFrameNum) const
    {
        DescriptorPoolRequirements req = {};
        // set 0 per queued frame + fallback set 1 + kMaxTrsTextureModels pre-allocated set 1s
        req.descriptorSetMaxNum = queuedFrameNum + 1 + kMaxTrsTextureModels;
        req.structuredBufferMaxNum = queuedFrameNum * 3 + 1 + kMaxTrsTextureModels;
        req.storageStructuredBufferMaxNum = queuedFrameNum;
        req.textureMaxNum = 1 + kMaxTrsTextureModels;
        return req;
    }

    bool HybridEvalComputePass::createFallbackSet1(FrameContext& context)
    {
        RRenderData& rd = context.renderData;

        nri::TextureDesc textureDesc = {};
        textureDesc.type = nri::TextureType::TEXTURE_2D;
        textureDesc.usage = nri::TextureUsageBits::SHADER_RESOURCE;
        textureDesc.format = nri::Format::RGBA16_SFLOAT;
        textureDesc.width = 3;
        textureDesc.height = 1;
        textureDesc.mipNum = 1;
        textureDesc.layerNum = 1;
        NRI_ABORT_ON_FAILURE(context.NRI.CreateTexture(*rd.rdDevice, textureDesc, mFallbackTexture));

        nri::ResourceGroupDesc textureGroup = {};
        textureGroup.memoryLocation = nri::MemoryLocation::DEVICE;
        textureGroup.textureNum = 1;
        textureGroup.textures = &mFallbackTexture;
        NRI_ABORT_ON_FAILURE(context.NRI.AllocateAndBindMemory(*rd.rdDevice, textureGroup, &mFallbackTextureMemory));

        // Neutral node: T = 0, R = identity (0,0,0,1), S = 1 as half floats
        const uint16_t h0 = 0x0000, h1 = 0x3C00;
        const uint16_t texels[12] = {h0, h0, h0, h0, h0, h0, h0, h1, h1, h1, h1, h0};
        nri::TextureSubresourceUploadDesc subresource = {};
        subresource.slices = texels;
        subresource.sliceNum = 1;
        subresource.rowPitch = 3 * 8;
        subresource.slicePitch = 3 * 8;
        nri::TextureUploadDesc textureUpload = {};
        textureUpload.texture = mFallbackTexture;
        textureUpload.subresources = &subresource;
        textureUpload.after = {nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE};

        nri::BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(TrsTexClipRecord);
        bufferDesc.structureStride = sizeof(TrsTexClipRecord);
        bufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;
        NRI_ABORT_ON_FAILURE(context.NRI.CreateBuffer(*rd.rdDevice, bufferDesc, mFallbackClipTable));
        nri::ResourceGroupDesc bufferGroup = {};
        bufferGroup.memoryLocation = nri::MemoryLocation::DEVICE;
        bufferGroup.bufferNum = 1;
        bufferGroup.buffers = &mFallbackClipTable;
        NRI_ABORT_ON_FAILURE(context.NRI.AllocateAndBindMemory(*rd.rdDevice, bufferGroup, &mFallbackClipTableMemory));

        const TrsTexClipRecord record = {0, 1, 1.0f, 0.0f};
        nri::BufferUploadDesc bufferUpload = {&record, mFallbackClipTable, {nri::AccessBits::SHADER_RESOURCE, nri::StageBits::COMPUTE_SHADER}};
        NRI_ABORT_ON_FAILURE(context.NRI.UploadData(*rd.rdGraphicsQueue, &textureUpload, 1, &bufferUpload, 1));

        nri::Texture2DViewDesc textureViewDesc = {mFallbackTexture, nri::Texture2DViewType::SHADER_RESOURCE, nri::Format::RGBA16_SFLOAT};
        NRI_ABORT_ON_FAILURE(context.NRI.CreateTexture2DView(textureViewDesc, mFallbackTextureView));
        nri::BufferViewDesc clipViewDesc = {};
        clipViewDesc.buffer = mFallbackClipTable;
        clipViewDesc.viewType = nri::BufferViewType::SHADER_RESOURCE;
        clipViewDesc.size = bufferDesc.size;
        clipViewDesc.structureStride = sizeof(TrsTexClipRecord);
        NRI_ABORT_ON_FAILURE(context.NRI.CreateBufferView(clipViewDesc, mFallbackClipTableView));

        NRI_ABORT_ON_FAILURE(context.NRI.AllocateDescriptorSets(context.descriptorPool, *mPipelineLayout, 1, &mFallbackSet1, 1, 0));
        nri::Descriptor* descriptors[] = {mFallbackTextureView, mFallbackClipTableView};
        nri::UpdateDescriptorRangeDesc ranges[] = {
                {mFallbackSet1, 0, 0, &descriptors[0], 1},
                {mFallbackSet1, 1, 0, &descriptors[1], 1},
        };
        context.NRI.UpdateDescriptorRanges(ranges, helper::GetCountOf(ranges));
        return true;
    }

    bool HybridEvalComputePass::CreateDescriptors(FrameContext& context)
    {
        mDescriptorSets.assign(context.queuedFrameNum, nullptr);
        for (uint32_t frameIndex = 0; frameIndex < context.queuedFrameNum; ++frameIndex)
        {
            NRI_ABORT_ON_FAILURE(context.NRI.AllocateDescriptorSets(context.descriptorPool, *mPipelineLayout, 0, &mDescriptorSets[frameIndex], 1, 0));

            nri::Descriptor* descriptors[] = {
                    context.registry.GetView(mWorkView, frameIndex),
                    context.registry.GetView(mTermView, frameIndex),
                    context.registry.GetView(mSlotView, frameIndex),
                    context.registry.GetView(mTRSMatrixStorageView, frameIndex),
            };
            nri::UpdateDescriptorRangeDesc ranges[] = {
                    {mDescriptorSets[frameIndex], 0, 0, &descriptors[0], 1},
                    {mDescriptorSets[frameIndex], 1, 0, &descriptors[1], 1},
                    {mDescriptorSets[frameIndex], 2, 0, &descriptors[2], 1},
                    {mDescriptorSets[frameIndex], 3, 0, &descriptors[3], 1},
            };
            context.NRI.UpdateDescriptorRanges(ranges, helper::GetCountOf(ranges));
        }

        createFallbackSet1(context);

        // NRI cannot free individual descriptor sets: hand out per-model set 1s from a pre-allocated pool.
        RRenderData& rd = context.renderData;
        rd.rdTrsTextureSetFreeList.clear();
        for (uint32_t i = 0; i < kMaxTrsTextureModels; ++i)
        {
            nri::DescriptorSet* set = nullptr;
            NRI_ABORT_ON_FAILURE(context.NRI.AllocateDescriptorSets(context.descriptorPool, *mPipelineLayout, 1, &set, 1, 0));
            rd.rdTrsTextureSetFreeList.push_back(set);
        }
        return true;
    }

    void HybridEvalComputePass::DeclareAccess(RegistryAccessBuilder& builder) const
    {
        builder.Use(mTRSMatrixBuffer, nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::COMPUTE_SHADER);
    }

    void HybridEvalComputePass::publishEmpty(FrameContext& context)
    {
        mAnimatedDispatches.clear();
        mAnimatedGroups.clear();
        context.sceneFrame->animatedDispatches = &mAnimatedDispatches;
        context.sceneFrame->animatedGroups = &mAnimatedGroups;
        context.sceneFrame->uploadedBoneOffsetMatrixCount = 0;
    }

    void HybridEvalComputePass::Upload(FrameContext& context)
    {
        mWorks.clear();
        mTerms.clear();
        mSlotData.clear();
        mCrowdInstances.clear();
        mNodeParentIndices.clear();
        mBoneNodeIndices.clear();
        mBoneOffsetMatrices.clear();
        mModelRootMatrices.clear();
        mAnimatedDispatches.clear();
        mAnimatedGroups.clear();
        mTrsCursor = 0;

        RRenderData& rd = context.renderData;
        HybridEvalStats& stats = rd.rdHybridStats;
        stats = HybridEvalStats{};

        if (context.sceneFrame == nullptr || context.sceneFrame->modelInstData == nullptr)
        {
            return;
        }
        ModelAndInstanceData& scene = *context.sceneFrame->modelInstData;
        const RenderResourceBudget& budget = rd.rdResourceBudget;
        const int phases = std::max(1, scene.miTrackPhases);

        mUploadTimer.Start();

        for (const auto& modelType : scene.miModelInstancesPerModel)
        {
            if (modelType.second.empty())
            {
                continue;
            }
            std::shared_ptr<Model> model = modelType.second.front()->GetModel();
            if (!model->HasAnimations() || model->GetBoneList().empty())
            {
                continue;
            }

            const auto& nodes = model->GetNodeList();
            const auto& bones = model->GetBoneList();
            const uint32_t nodeCount = static_cast<uint32_t>(nodes.size());
            const uint32_t boneCount = static_cast<uint32_t>(bones.size());
            const uint32_t instanceCount = static_cast<uint32_t>(modelType.second.size());
            const uint32_t clipCount = static_cast<uint32_t>(model->GetAnimClips().size());
            if (nodeCount == 0 || clipCount == 0)
            {
                continue;
            }

            // Local skeleton tables (node order == Model::GetNodeList()).
            const std::vector<int32_t>& localParent = model->GetNodeParentIndices();
            std::vector<uint32_t> localBoneNode(boneCount, 0);
            std::vector<glm::mat4> localBoneOffset(boneCount, glm::mat4(1.0f));
            for (const auto& bone : bones)
            {
                const uint32_t boneId = bone->GetBoneId();
                const int32_t nodeIndex = model->FindNodeIndex(bone->GetBoneName());
                if (boneId >= boneCount || nodeIndex < 0)
                {
                    fmt::print(stderr, fg(fmt::color::red), "HybridEvalComputePass: bone '{}' of '{}' has no node\n", bone->GetBoneName(), model->GetModelFileName());
                    publishEmpty(context);
                    return;
                }
                localBoneNode[boneId] = static_cast<uint32_t>(nodeIndex);
                localBoneOffset[boneId] = bone->GetOffsetMatrix();
            }

            const EvalMode mode = AnimationSystem::EffectiveMode(*model, scene.miEvalMode);
            const bool isTracks = mode == EvalMode::Tracks;
            const uint32_t evalUnits = isTracks ? clipCount * static_cast<uint32_t>(phases) : instanceCount;

            AnimatedGroup group = {};
            group.gpu.nodeTransformOffset = mTrsCursor;
            group.gpu.boneMatrixOffset = static_cast<uint32_t>(mBoneOffsetMatrices.size());
            group.gpu.modelRootOffset = static_cast<uint32_t>(mModelRootMatrices.size());
            group.gpu.numberOfNodes = nodeCount;
            group.gpu.numberOfBones = boneCount;
            group.gpu.instanceCount = evalUnits;
            group.workBase = static_cast<uint32_t>(mWorks.size());
            group.workCount = evalUnits;
            group.drawInstanceCount = instanceCount;
            group.crowdInstanceBase = static_cast<uint32_t>(mCrowdInstances.size());
            group.mode = mode;
            group.isTracks = isTracks;
            group.model = model.get();

            for (uint32_t u = 0; u < evalUnits; ++u)
            {
                const uint32_t outputBase = group.gpu.nodeTransformOffset + u * nodeCount;

                // Skeleton tables live in the TRS matrix index space (outputBase + local).
                for (uint32_t n = 0; n < nodeCount; ++n)
                {
                    const int32_t p = n < localParent.size() ? localParent[n] : -1;
                    mNodeParentIndices.emplace_back(p >= 0 ? static_cast<int32_t>(outputBase + static_cast<uint32_t>(p)) : -1);
                }
                for (uint32_t b = 0; b < boneCount; ++b)
                {
                    mBoneNodeIndices.emplace_back(outputBase + localBoneNode[b]);
                    mBoneOffsetMatrices.emplace_back(localBoneOffset[b]);
                }

                HybridWork work = {};
                work.firstTerm = static_cast<uint32_t>(mTerms.size());
                work.outputBase = outputBase;

                if (isTracks)
                {
                    const uint32_t clip = u / static_cast<uint32_t>(phases);
                    const int bucket = static_cast<int>(u % static_cast<uint32_t>(phases));
                    const float timeSec = AnimationSystem::TrackClipTimeSec(*model->GetAnimClips()[clip], bucket, phases, scene.miTrackGlobalTimeSec);
                    mModelRootMatrices.emplace_back(glm::mat4(1.0f)); // tracks are evaluated in model space
                    mTerms.push_back({clip, timeSec, 1.0f, 0.0f});
                }
                else
                {
                    const std::shared_ptr<ModelInstance>& instance = modelType.second[u];
                    mModelRootMatrices.emplace_back(instance->GetLocalTransformMatrix());

                    AnimGraphInstance* graph = instance->GetGraph();
                    const HybridWorkCpu* cpuWork = (graph != nullptr && graph->Usable()) ? &graph->Work() : nullptr;
                    if (cpuWork != nullptr && !cpuWork->terms.empty() && cpuWork->nodeCount == nodeCount)
                    {
                        // Slots first (absolute node bases), then the terms that reference them.
                        std::vector<uint32_t> slotBase(cpuWork->slotCount, 0);
                        for (uint32_t s = 0; s < cpuWork->slotCount; ++s)
                        {
                            slotBase[s] = static_cast<uint32_t>(mSlotData.size());
                            for (uint32_t n = 0; n < nodeCount; ++n)
                            {
                                mSlotData.emplace_back(PackTransform(cpuWork->slotData[static_cast<size_t>(s) * nodeCount + n]));
                            }
                        }
                        for (const HybridTermCpu& t : cpuWork->terms)
                        {
                            HybridTerm term = {};
                            if (t.bSlot)
                            {
                                const uint32_t local = std::min<uint32_t>(t.index, cpuWork->slotCount > 0 ? cpuWork->slotCount - 1 : 0);
                                term.kindAndIndex = kHybridTermSlotBit | slotBase[local];
                            }
                            else
                            {
                                term.kindAndIndex = std::min(t.index, clipCount - 1) & kHybridTermIndexMask;
                                term.timeSec = t.timeSec;
                            }
                            term.weight = t.weight;
                            mTerms.push_back(term);
                        }
                    }
                    else
                    {
                        // No usable graph: upload whatever CPU pose the instance can provide as one slot.
                        const std::vector<Transform>& pose = instance->EnsureCpuPose();
                        const uint32_t base = static_cast<uint32_t>(mSlotData.size());
                        for (uint32_t n = 0; n < nodeCount; ++n)
                        {
                            mSlotData.emplace_back(PackTransform(n < pose.size() ? pose[n] : model->GetBindPose()[n]));
                        }
                        mTerms.push_back({kHybridTermSlotBit | base, 0.0f, 1.0f, 0.0f});
                    }
                }

                work.termCount = static_cast<uint32_t>(mTerms.size()) - work.firstTerm;
                mWorks.emplace_back(work);
            }

            if (isTracks)
            {
                for (const std::shared_ptr<ModelInstance>& instance : modelType.second)
                {
                    const InstanceSettings settings = instance->GetInstanceSettings();
                    CrowdInstanceData crowd = {};
                    crowd.world = instance->GetLocalTransformMatrix();
                    const uint32_t clip = std::min<uint32_t>(settings.mAnimClipNr, clipCount - 1);
                    crowd.trackIndex = clip * static_cast<uint32_t>(phases) + static_cast<uint32_t>(AnimationSystem::TrackBucket(settings.mAnimPhase, phases));
                    mCrowdInstances.emplace_back(crowd);
                }
            }

            mTrsCursor += evalUnits * nodeCount;
            mAnimatedDispatches.emplace_back(group.gpu);
            mAnimatedGroups.emplace_back(group);
        }

        // ---- capacity (safety net; the UI caps spawning well below this) -----------------------------
        const uint64_t maxNodes = budget.GetMaxNodeTransforms();
        const uint64_t maxBones = budget.GetMaxBoneMatrices();
        const uint64_t maxWorld = budget.maxWorldMatrices;
        const uint64_t maxTerms = maxWorld * kMaxTermsPerWork;
        if (mTrsCursor > maxNodes || mSlotData.size() > maxNodes || mNodeParentIndices.size() > maxNodes ||
            mBoneOffsetMatrices.size() > maxBones || mBoneNodeIndices.size() > maxBones ||
            mModelRootMatrices.size() > maxWorld || mWorks.size() > maxWorld || mTerms.size() > maxTerms ||
            mCrowdInstances.size() > maxWorld)
        {
            fmt::print(stderr,
                       fg(fmt::color::red),
                       "HybridEvalComputePass::Upload error: capacity exceeded (trs={}/{}, slots={}/{}, bones={}/{}, works={}/{}, terms={}/{}, crowd={}/{})\n",
                       mTrsCursor, maxNodes, mSlotData.size(), maxNodes, mBoneOffsetMatrices.size(), maxBones,
                       mWorks.size(), maxWorld, mTerms.size(), maxTerms, mCrowdInstances.size(), maxWorld);
            publishEmpty(context);
            return;
        }

        // ---- upload ----------------------------------------------------------------------------------
        const uint32_t frameIndex = rd.queuedFrameIndex;
        RenderResourceRegistry& registry = context.registry;
        auto upload = [&](BufferHandle handle, const void* src, size_t bytes)
        {
            if (bytes == 0)
            {
                return;
            }
            nri::Buffer* buffer = registry.GetBuffer(handle);
            const uint64_t offset = registry.GetOffsetForFrame(handle, frameIndex);
            void* dst = context.NRI.MapBuffer(*buffer, offset, bytes);
            std::memcpy(dst, src, bytes);
            context.NRI.UnmapBuffer(*buffer);
        };
        upload(mWorkBuffer, mWorks.data(), mWorks.size() * sizeof(HybridWork));
        upload(mTermBuffer, mTerms.data(), mTerms.size() * sizeof(HybridTerm));
        upload(mSlotBuffer, mSlotData.data(), mSlotData.size() * sizeof(RNodeTransformData));
        upload(mCrowdInstanceBuffer, mCrowdInstances.data(), mCrowdInstances.size() * sizeof(CrowdInstanceData));
        upload(mNodeParentIndexBuffer, mNodeParentIndices.data(), mNodeParentIndices.size() * sizeof(int32_t));
        upload(mBoneNodeIndexBuffer, mBoneNodeIndices.data(), mBoneNodeIndices.size() * sizeof(uint32_t));
        upload(mBoneOffsetMatrixBuffer, mBoneOffsetMatrices.data(), mBoneOffsetMatrices.size() * sizeof(glm::mat4));
        upload(mModelRootMatrixBuffer, mModelRootMatrices.data(), mModelRootMatrices.size() * sizeof(glm::mat4));
        rd.rdUploadToUBOTime += mUploadTimer.Stop();

        // ---- publish ---------------------------------------------------------------------------------
        context.sceneFrame->animatedDispatches = &mAnimatedDispatches;
        context.sceneFrame->animatedGroups = &mAnimatedGroups;
        context.sceneFrame->uploadedBoneOffsetMatrixCount = mBoneOffsetMatrices.size();

        const size_t slotBytes = mSlotData.size() * sizeof(RNodeTransformData);
        const size_t tableBytes = mNodeParentIndices.size() * sizeof(int32_t) + mBoneNodeIndices.size() * sizeof(uint32_t) +
                                  mBoneOffsetMatrices.size() * sizeof(glm::mat4) + mModelRootMatrices.size() * sizeof(glm::mat4);
        stats.works = static_cast<uint32_t>(mWorks.size());
        stats.terms = static_cast<uint32_t>(mTerms.size());
        stats.slotTransforms = static_cast<uint32_t>(mSlotData.size());
        stats.slotBytes = slotBytes;
        stats.uploadBytes = slotBytes + tableBytes + mWorks.size() * sizeof(HybridWork) + mTerms.size() * sizeof(HybridTerm) +
                            mCrowdInstances.size() * sizeof(CrowdInstanceData);
        stats.crowdInstances = static_cast<uint32_t>(mCrowdInstances.size());
        for (const AnimatedGroup& g : mAnimatedGroups)
        {
            if (g.model != nullptr && g.model->IsTrsTexReady())
            {
                stats.trsTextureBytes += g.model->GetTrsTexCpu().TexelBytes();
            }
        }
        rd.rdResourceBudgetUsage.animatedInstances = mModelRootMatrices.size();
        rd.rdResourceBudgetUsage.boneMatrices = mBoneOffsetMatrices.size();
        rd.rdResourceBudgetUsage.nodeTransforms = std::max<size_t>(mTrsCursor, mSlotData.size());
        rd.rdResourceBudgetUsage.uploadBytes += stats.uploadBytes;
    }

    void HybridEvalComputePass::Record(CommandContext& context)
    {
        if (context.sceneFrame == nullptr || context.sceneFrame->animatedGroups == nullptr ||
            context.sceneFrame->animatedGroups->empty())
        {
            return;
        }
        HybridEvalStats& stats = context.renderData.rdHybridStats;
        const bool interpolate = context.sceneFrame->modelInstData == nullptr || context.sceneFrame->modelInstData->miTrsInterpolate;

        context.NRI.CmdSetPipelineLayout(context.commandBuffer, nri::BindPoint::COMPUTE, *mPipelineLayout);
        context.NRI.CmdSetPipeline(context.commandBuffer, *mPipeline);
        context.NRI.CmdSetDescriptorSet(context.commandBuffer, {0, mDescriptorSets[context.frameIndex], nri::BindPoint::COMPUTE});

        for (const AnimatedGroup& group : *context.sceneFrame->animatedGroups)
        {
            nri::DescriptorSet* set1 = mFallbackSet1;
            if (group.model != nullptr && group.model->TrsTexGpu().IsReady())
            {
                set1 = group.model->TrsTexGpu().set1;
            }
            context.NRI.CmdSetDescriptorSet(context.commandBuffer, {1, set1, nri::BindPoint::COMPUTE});

            PushConstants push = {};
            push.nodeCount = group.gpu.numberOfNodes;
            push.flags = interpolate ? kFlagInterpolate : 0u;

            if (group.mode == EvalMode::PerCharacter)
            {
                // Tier A: one dispatch per character (the per-draw overhead this tier exists to show).
                for (uint32_t i = 0; i < group.workCount; ++i)
                {
                    push.workBase = group.workBase + i;
                    context.NRI.CmdSetRootConstants(context.commandBuffer, {0, &push, sizeof(push), 0, nri::BindPoint::COMPUTE});
                    context.NRI.CmdDispatch(context.commandBuffer, {1, 1, 1});
                    ++stats.dispatches;
                    ++stats.threadGroups;
                }
            }
            else
            {
                // CPU / B: one dispatch per model, group = instance. C: group = track.
                push.workBase = group.workBase;
                context.NRI.CmdSetRootConstants(context.commandBuffer, {0, &push, sizeof(push), 0, nri::BindPoint::COMPUTE});
                context.NRI.CmdDispatch(context.commandBuffer, {group.workCount, 1, 1});
                ++stats.dispatches;
                stats.threadGroups += group.workCount;
            }
        }
    }

    void HybridEvalComputePass::Cleanup(RRenderData& renderData)
    {
        if (mFallbackTextureView != nullptr)
        {
            renderData.NRI.DestroyDescriptor(mFallbackTextureView);
            mFallbackTextureView = nullptr;
        }
        if (mFallbackClipTableView != nullptr)
        {
            renderData.NRI.DestroyDescriptor(mFallbackClipTableView);
            mFallbackClipTableView = nullptr;
        }
        if (mFallbackTexture != nullptr)
        {
            renderData.NRI.DestroyTexture(mFallbackTexture);
            mFallbackTexture = nullptr;
        }
        if (mFallbackClipTable != nullptr)
        {
            renderData.NRI.DestroyBuffer(mFallbackClipTable);
            mFallbackClipTable = nullptr;
        }
        if (mFallbackTextureMemory != nullptr)
        {
            renderData.NRI.FreeMemory(mFallbackTextureMemory);
            mFallbackTextureMemory = nullptr;
        }
        if (mFallbackClipTableMemory != nullptr)
        {
            renderData.NRI.FreeMemory(mFallbackClipTableMemory);
            mFallbackClipTableMemory = nullptr;
        }
        mFallbackSet1 = nullptr;
        renderData.rdTrsTextureSetFreeList.clear();

        if (mPipeline != nullptr)
        {
            renderData.NRI.DestroyPipeline(mPipeline);
            mPipeline = nullptr;
        }
        if (mPipelineLayout != nullptr)
        {
            renderData.NRI.DestroyPipelineLayout(mPipelineLayout);
            mPipelineLayout = nullptr;
        }
        renderData.rdHybridEvalPipelineLayout = nullptr;
        mDescriptorSets.clear();
    }
} // namespace RAnimation
