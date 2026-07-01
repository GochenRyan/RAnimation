#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <utility>

#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/Model.h>
#include <Model/UsdModelLoader.h>
#include <Renderer/NRITexture.h>
#include <RHIWrap/Helper.h>

using namespace RAnimation;

bool Model::LoadModel(RRenderData& renderData, std::string modelFilename, unsigned int extraImportFlags)
{
    (void)extraImportFlags; // retained for API compatibility; USD loading takes no assimp post-process flags
    fmt::print("{}: loading USD model from file '{}'\n", __FUNCTION__, modelFilename);

    UsdLoadedModel loaded;
    if (!LoadUsdModel(modelFilename, loaded))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: could not load USD asset '{}'\n",
                   __FUNCTION__,
                   modelFilename);
        return false;
    }

    /* add a white texture in case there is no diffuse tex but colors */
    std::string whiteTexName = ASSETS_SRC_DIR "/SkeletalAnimation/textures/white.png";
    if (!utils::LoadTexture(whiteTexName, mWhiteTexture.texture))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: could not load white default texture '{}'\n",
                   __FUNCTION__,
                   whiteTexName);
        return false;
    }

    if (!NRITexture::LoadTexture(renderData, mWhiteTexture))
    {
        return false;
    }

    /* add a placeholder texture in case there is no diffuse tex */
    std::string placeholderTexName = ASSETS_SRC_DIR "/SkeletalAnimation/textures/missing_tex.png";
    if (!utils::LoadTexture(placeholderTexName, mPlaceholderTexture.texture))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: could not load placeholder texture '{}'\n",
                   __FUNCTION__,
                   placeholderTexName);
        return false;
    }

    if (!NRITexture::LoadTexture(renderData, mPlaceholderTexture))
    {
        return false;
    }

    /* material textures referenced by the asset */
    for (const auto& texRef : loaded.textures)
    {
        auto [it, inserted] = mTextures.try_emplace(texRef.name);
        if (!inserted)
        {
            continue;
        }

        RTextureData& texData = it->second;
        if (!utils::LoadTexture(texRef.absolutePath, texData.texture))
        {
            fmt::print(stderr,
                       fg(fmt::color::red),
                       "{} error: could not load texture file '{}', skipping\n",
                       __FUNCTION__,
                       texRef.absolutePath);
            mTextures.erase(it);
            continue;
        }

        if (!NRITexture::LoadTexture(renderData, texData))
        {
            return false;
        }
    }

    /* nodes: the loader emits joints in parent-before-child order, so a single walk rebuilds the tree */
    std::vector<std::shared_ptr<Node>> nodeByIndex(loaded.nodes.size());
    for (size_t i = 0; i < loaded.nodes.size(); ++i)
    {
        const UsdNodeData& nodeData = loaded.nodes.at(i);

        std::shared_ptr<Node> node;
        if (nodeData.parentIndex < 0)
        {
            node = Node::CreateNode(nodeData.name);
            mRootNode = node;
        }
        else
        {
            node = nodeByIndex.at(static_cast<size_t>(nodeData.parentIndex))->AddChild(nodeData.name);
        }

        node->SetLocalTransform(nodeData.localTransform);
        nodeByIndex.at(i) = node;
        mNodeMap.insert({nodeData.name, node});
        mNodeList.emplace_back(node);
    }

    /* bones: bone id == index, offset matrix == inverse bind matrix */
    for (size_t i = 0; i < loaded.bones.size(); ++i)
    {
        const UsdBoneData& boneData = loaded.bones.at(i);
        mBoneList.emplace_back(
                std::make_shared<Bone>(static_cast<unsigned int>(i), boneData.name, boneData.inverseBindMatrix));
        mInverseBindMatrices.insert({boneData.name, boneData.inverseBindMatrix});
    }

    /* meshes + triangle/vertex stats */
    mModelMeshes = std::move(loaded.meshes);
    for (const auto& mesh : mModelMeshes)
    {
        mVertexCount += static_cast<unsigned int>(mesh.vertices.size());
        mTriangleCount += static_cast<unsigned int>(mesh.indices.size() / 3);
    }

    /* animation clips */
    for (const auto& clipData : loaded.animClips)
    {
        std::shared_ptr<AnimClip> animClip = std::make_shared<AnimClip>();
        animClip->SetClipName(clipData.name);
        animClip->SetClipDuration(clipData.duration);
        animClip->SetClipTicksPerSecond(clipData.ticksPerSecond);

        for (const auto& channelData : clipData.channels)
        {
            std::shared_ptr<AnimChannel> channel = std::make_shared<AnimChannel>();
            channel->SetChannelData(channelData.nodeName,
                                    channelData.translationTimings,
                                    channelData.translations,
                                    channelData.rotationTimings,
                                    channelData.rotations,
                                    channelData.scaleTimings,
                                    channelData.scalings,
                                    channelData.preState,
                                    channelData.postState);
            animClip->AddChannel(channel);
        }

        mAnimClips.emplace_back(animClip);
    }

    mRootTransformMatrix = loaded.rootTransform;

    /* create vertex buffers for the meshes */
    std::vector<nri::Buffer*> uploadBuffers;
    std::vector<nri::BufferUploadDesc> uploadDescs;
    for (const auto& mesh : mModelMeshes)
    {
        nri::Buffer* vertexBuffer = nullptr;
        nri::BufferDesc vertexBufferDesc = {};
        vertexBufferDesc.size = mesh.vertices.size() * sizeof(RVertex);
        vertexBufferDesc.usage = nri::BufferUsageBits::VERTEX_BUFFER;
        NRI_ABORT_ON_FAILURE(renderData.NRI.CreateBuffer(*renderData.rdDevice, vertexBufferDesc, vertexBuffer));
        mVertexBuffers.emplace_back(vertexBuffer);
        uploadBuffers.emplace_back(vertexBuffer);
        uploadDescs.push_back({mesh.vertices.data(), vertexBuffer, {nri::AccessBits::VERTEX_BUFFER, nri::StageBits::ALL}});

        nri::Buffer* indexBuffer = nullptr;
        nri::BufferDesc indexBufferDesc = {};
        indexBufferDesc.size = mesh.indices.size() * sizeof(uint32_t);
        indexBufferDesc.usage = nri::BufferUsageBits::INDEX_BUFFER;
        NRI_ABORT_ON_FAILURE(renderData.NRI.CreateBuffer(*renderData.rdDevice, indexBufferDesc, indexBuffer));
        mIndexBuffers.emplace_back(indexBuffer);
        uploadBuffers.emplace_back(indexBuffer);
        uploadDescs.push_back({mesh.indices.data(), indexBuffer, {nri::AccessBits::INDEX_BUFFER, nri::StageBits::ALL}});
    }

    if (!uploadBuffers.empty())
    {
        nri::ResourceGroupDesc resourceGroupDesc = {};
        resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
        resourceGroupDesc.buffers = uploadBuffers.data();
        resourceGroupDesc.bufferNum = static_cast<uint32_t>(uploadBuffers.size());

        const uint32_t allocationNum = renderData.NRI.CalculateAllocationNumber(*renderData.rdDevice, resourceGroupDesc);
        mBufferMemories.resize(allocationNum, nullptr);
        NRI_ABORT_ON_FAILURE(
                renderData.NRI.AllocateAndBindMemory(*renderData.rdDevice, resourceGroupDesc, mBufferMemories.data()));

        NRI_ABORT_ON_FAILURE(renderData.NRI.UploadData(
                *renderData.rdGraphicsQueue, nullptr, 0, uploadDescs.data(), static_cast<uint32_t>(uploadDescs.size())));
    }

    mModelFilenamePath = modelFilename;
    mModelFilename = std::filesystem::path(modelFilename).filename().generic_string();

    fmt::print("{}: - model has a total of {} texture{}\n",
               __FUNCTION__,
               mTextures.size(),
               mTextures.size() == 1 ? "" : "s");
    fmt::print(
            "{}: - model has a total of {} bone{}\n", __FUNCTION__, mBoneList.size(), mBoneList.size() == 1 ? "" : "s");
    fmt::print("{}: - model has a total of {} animation{}\n",
               __FUNCTION__,
               mAnimClips.size(),
               mAnimClips.size() == 1 ? "" : "s");

    fmt::print("{}: successfully loaded model '{}' ({})\n", __FUNCTION__, modelFilename, mModelFilename);
    return true;
}

glm::mat4 Model::GetRootTranformationMatrix()
{
    return mRootTransformMatrix;
}

void Model::Draw(RRenderData& renderData)
{
    nri::CommandBuffer& commandBuffer = *renderData.GetCurrentQueueFrame().commandBuffer;
    for (unsigned int i = 0; i < mModelMeshes.size(); ++i)
    {
        RMesh& mesh = mModelMeshes.at(i);
        // find diffuse texture by name
        RTextureData* diffuseTex = nullptr;
        auto diffuseTexName = mesh.textures.find(TextureType::Diffuse);
        if (diffuseTexName != mesh.textures.end())
        {
            auto diffuseTexture = mTextures.find(diffuseTexName->second);
            if (diffuseTexture != mTextures.end())
            {
                diffuseTex = &diffuseTexture->second;
            }
        }

        nri::DescriptorSet* materialDescriptorSet = diffuseTex != nullptr ? diffuseTex->descriptorSet : nullptr;
        if (materialDescriptorSet == nullptr)
        {
            materialDescriptorSet = mesh.usesPBRColors ? mWhiteTexture.descriptorSet : mPlaceholderTexture.descriptorSet;
        }

        if (materialDescriptorSet != nullptr)
        {
            nri::SetDescriptorSetDesc materialSet = {0, materialDescriptorSet, nri::BindPoint::GRAPHICS};
            renderData.NRI.CmdSetDescriptorSet(commandBuffer, materialSet);
        }

        nri::VertexBufferDesc vertexBufferDesc = {};
        vertexBufferDesc.buffer = mVertexBuffers.at(i);
        vertexBufferDesc.offset = 0;
        vertexBufferDesc.stride = sizeof(RVertex);
        renderData.NRI.CmdSetVertexBuffers(commandBuffer, 0, &vertexBufferDesc, 1);
        renderData.NRI.CmdSetIndexBuffer(commandBuffer, *mIndexBuffers.at(i), 0, nri::IndexType::UINT32);
        renderData.NRI.CmdDrawIndexed(commandBuffer, {static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0});
    }
}

void Model::DrawInstanced(RRenderData& renderData, uint32_t instanceCount)
{
    nri::CommandBuffer& commandBuffer = *renderData.GetCurrentQueueFrame().commandBuffer;
    for (unsigned int i = 0; i < mModelMeshes.size(); ++i)
    {
        RMesh& mesh = mModelMeshes.at(i);
        // find diffuse texture by name
        RTextureData* diffuseTex = nullptr;
        auto diffuseTexName = mesh.textures.find(TextureType::Diffuse);
        if (diffuseTexName != mesh.textures.end())
        {
            auto diffuseTexture = mTextures.find(diffuseTexName->second);
            if (diffuseTexture != mTextures.end())
            {
                diffuseTex = &diffuseTexture->second;
            }
        }

        nri::DescriptorSet* materialDescriptorSet = diffuseTex != nullptr ? diffuseTex->descriptorSet : nullptr;
        if (materialDescriptorSet == nullptr)
        {
            materialDescriptorSet = mesh.usesPBRColors ? mWhiteTexture.descriptorSet : mPlaceholderTexture.descriptorSet;
        }

        if (materialDescriptorSet != nullptr)
        {
            nri::SetDescriptorSetDesc materialSet = {0, materialDescriptorSet, nri::BindPoint::GRAPHICS};
            renderData.NRI.CmdSetDescriptorSet(commandBuffer, materialSet);
        }

        if (HasAnimations())
        {
            uint32_t maxBoneIndex = 0;
            for (const RVertex& vertex : mesh.vertices)
            {
                maxBoneIndex = std::max(maxBoneIndex, vertex.boneNumber.x);
                maxBoneIndex = std::max(maxBoneIndex, vertex.boneNumber.y);
                maxBoneIndex = std::max(maxBoneIndex, vertex.boneNumber.z);
                maxBoneIndex = std::max(maxBoneIndex, vertex.boneNumber.w);
            }

            if (maxBoneIndex >= mBoneList.size())
            {
                fmt::print(stderr,
                           fg(fmt::color::red),
                           "{} error: mesh {} in model '{}' references bone index {}, but model only has {} bones\n",
                           __FUNCTION__,
                           i,
                           mModelFilename,
                           maxBoneIndex,
                           mBoneList.size());
            }
        }

        nri::VertexBufferDesc vertexBufferDesc = {};
        vertexBufferDesc.buffer = mVertexBuffers.at(i);
        vertexBufferDesc.offset = 0;
        vertexBufferDesc.stride = sizeof(RVertex);
        renderData.NRI.CmdSetVertexBuffers(commandBuffer, 0, &vertexBufferDesc, 1);
        renderData.NRI.CmdSetIndexBuffer(commandBuffer, *mIndexBuffers.at(i), 0, nri::IndexType::UINT32);
        renderData.NRI.CmdDrawIndexed(commandBuffer,
                                      {static_cast<uint32_t>(mesh.indices.size()), instanceCount, 0, 0, 0});
    }
}

unsigned int Model::GetTriangleCount()
{
    return mTriangleCount;
}

std::string Model::GetModelFileName()
{
    return mModelFilename;
}

std::string Model::GetModelFileNamePath()
{
    return mModelFilenamePath;
}

bool Model::HasAnimations()
{
    return !mAnimClips.empty();
}

const std::vector<std::shared_ptr<AnimClip>>& Model::GetAnimClips()
{
    return mAnimClips;
}

const std::vector<std::shared_ptr<Node>>& Model::GetNodeList()
{
    return mNodeList;
}

const std::unordered_map<std::string, std::shared_ptr<Node>>& Model::GetNodeMap()
{
    return mNodeMap;
}

const std::vector<std::shared_ptr<Bone>>& Model::GetBoneList()
{
    return mBoneList;
}

const std::unordered_map<std::string, glm::mat4>& Model::GetInverseBindMatrices()
{
    return mInverseBindMatrices;
}

const std::shared_ptr<Node> Model::GetRootNode()
{
    return mRootNode;
}

void Model::Cleanup(RRenderData& renderData)
{
    for (auto& [name, textureData] : mTextures)
    {
        if (textureData.nriTexture != nullptr)
        {
            NRITexture::Cleanup(renderData, textureData);
        }
    }

    if (mWhiteTexture.nriTexture != nullptr)
    {
        NRITexture::Cleanup(renderData, mWhiteTexture);
    }

    if (mPlaceholderTexture.nriTexture != nullptr)
    {
        NRITexture::Cleanup(renderData, mPlaceholderTexture);
    }

    for (nri::Buffer* buffer : mVertexBuffers)
    {
        renderData.NRI.DestroyBuffer(buffer);
    }

    for (nri::Buffer* buffer : mIndexBuffers)
    {
        renderData.NRI.DestroyBuffer(buffer);
    }

    for (nri::Memory* memory : mBufferMemories)
    {
        renderData.NRI.FreeMemory(memory);
    }
}
