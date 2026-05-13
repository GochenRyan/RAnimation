#include <filesystem>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/Model.h>
#include <Model/Mesh.h>
#include <Renderer/NRITexture.h>
#include <RHIWrap/Helper.h>
#include <Tools/Tools.h>

using namespace RAnimation;

bool Model::LoadModel(RRenderData& renderData, std::string modelFilename, unsigned int extraImportFlags)
{
    fmt::print("{}: loading model from file '{}'\n", __FUNCTION__, modelFilename);

    Assimp::Importer importer;
    /* we need to flip texture coordinates for Vulkan */
    const aiScene* scene = importer.ReadFile(modelFilename,
                                             aiProcess_Triangulate | aiProcess_GenNormals |
                                                     aiProcess_ValidateDataStructure | aiProcess_FlipUVs |
                                                     extraImportFlags);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: assimp error '{}' while loading file '{}'\n",
                   __FUNCTION__,
                   importer.GetErrorString(),
                   modelFilename);
        return false;
    }

    unsigned int numMeshes = scene->mNumMeshes;
    fmt::print("{}: found {} mesh{}\n", __FUNCTION__, numMeshes, numMeshes == 1 ? "" : "es");

    for (unsigned int i = 0; i < numMeshes; ++i)
    {
        unsigned int numVertices = scene->mMeshes[i]->mNumVertices;
        unsigned int numFaces = scene->mMeshes[i]->mNumFaces;

        mVertexCount += numVertices;
        mTriangleCount += numFaces;

        fmt::print("{}: mesh {} contains {} vertices and {} faces\n", __FUNCTION__, i, numVertices, numFaces);
    }
    fmt::print("{}: model contains {} vertices and {} faces\n", __FUNCTION__, mVertexCount, mTriangleCount);

    if (scene->HasTextures())
    {
        unsigned int numTextures = scene->mNumTextures;

        for (unsigned int i = 0; i < scene->mNumTextures; ++i)
        {
            std::string texName = scene->mTextures[i]->mFilename.C_Str();

            unsigned int height = scene->mTextures[i]->mHeight;
            unsigned int width = scene->mTextures[i]->mWidth;
            aiTexel* data = scene->mTextures[i]->pcData;

            auto [it, inserted] = mTextures.try_emplace(texName);
            if (!inserted)
            {
                continue;
            }

            RTextureData& texData = it->second;

            if (!utils::LoadTexture(texName, texData.texture))
            {
                return false;
            }

            if (!NRITexture::LoadTexture(renderData, texData))
            {
                return false;
            }

            std::string internalTexName = "*" + std::to_string(i);
            fmt::print("{}: - added internal texture '{}'\n", __FUNCTION__, internalTexName);
        }

        fmt::print("{}: scene has {} embedded textures\n", __FUNCTION__, numTextures);
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

    /* the textures are stored directly or relative to the model file */
    std::string assetDirectory = modelFilename.substr(0, modelFilename.find_last_of('/'));

    /* nodes */
    fmt::print("{}: ... processing nodes...\n", __FUNCTION__);

    aiNode* rootNode = scene->mRootNode;
    std::string rootNodeName = rootNode->mName.C_Str();
    mRootNode = Node::CreateNode(rootNodeName);
    fmt::print("{}: root node name: '{}'\n", __FUNCTION__, rootNodeName);

    processNode(renderData, mRootNode, rootNode, scene, assetDirectory);

    fmt::print("{}: ... processing nodes finished...\n", __FUNCTION__);

    for (auto& [textureName, textureData] : mTextures)
    {
        if (textureData.nriTexture == nullptr)
        {
            if (!NRITexture::LoadTexture(renderData, textureData))
            {
                return false;
            }
        }
    }

    for (const auto& entry : mNodeList)
    {
        std::vector<std::shared_ptr<Node>> childNodes = entry->GetChilds();

        std::string parentName = entry->GetParentNodeName();
        fmt::print("{}: --- found node {} in node list, it has {} children, parent is {}\n",
                   __FUNCTION__,
                   entry->GetNodeName(),
                   childNodes.size(),
                   parentName);

        for (const auto& node : childNodes)
        {
            fmt::print("{}: ---- child: {}\n", __FUNCTION__, node->GetNodeName());
        }
    }

    for (const auto& node : mNodeList)
    {
        std::string nodeName = node->GetNodeName();
        const auto boneIter = std::find_if(mBoneList.begin(),
                                           mBoneList.end(),
                                           [nodeName](std::shared_ptr<Bone>& bone)
                                           { return bone->GetBoneName() == nodeName; });
        if (boneIter != mBoneList.end())
        {
            mInverseBindMatrices.insert(
                    {nodeName, mBoneList.at(std::distance(mBoneList.begin(), boneIter))->GetOffsetMatrix()});
        }
    }

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

    /* animations */
    unsigned int numAnims = scene->mNumAnimations;
    for (unsigned int i = 0; i < numAnims; ++i)
    {
        aiAnimation* animation = scene->mAnimations[i];

        fmt::print("{}: -- animation clip {} has {} skeletal channels, {} mesh channels, and {} morph mesh "
                   "channels\n",
                   __FUNCTION__,
                   i,
                   animation->mNumChannels,
                   animation->mNumMeshChannels,
                   animation->mNumMorphMeshChannels);

        std::shared_ptr<AnimClip> animClip = std::make_shared<AnimClip>();
        animClip->AddChannels(animation);
        if (animClip->GetClipName().empty())
        {
            animClip->SetClipName(std::to_string(i));
        }
        mAnimClips.emplace_back(animClip);
    }

    mModelFilenamePath = modelFilename;
    mModelFilename = std::filesystem::path(modelFilename).filename().generic_string();

    /* get root transformation matrix from model's root node */
    mRootTransformMatrix = Tools::convertAiToGLM(rootNode->mTransformation);

    fmt::print("{}: - model has a total of {} texture{}\n",
               __FUNCTION__,
               mTextures.size(),
               mTextures.size() == 1 ? "" : "s");
    fmt::print(
            "{}: - model has a total of {} bone{}\n", __FUNCTION__, mBoneList.size(), mBoneList.size() == 1 ? "" : "s");
    fmt::print("{}: - model has a total of {} animation{}\n", __FUNCTION__, numAnims, numAnims == 1 ? "" : "s");

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
        auto diffuseTexName = mesh.textures.find(aiTextureType_DIFFUSE);
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
        auto diffuseTexName = mesh.textures.find(aiTextureType_DIFFUSE);
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

void Model::processNode(RRenderData& renderData,
                        std::shared_ptr<Node> node,
                        aiNode* aNode,
                        const aiScene* scene,
                        std::string assetDirectory)
{
    std::string nodeName = aNode->mName.C_Str();
    fmt::print("{}: node name: '{}'\n", __FUNCTION__, nodeName);

    node->SetLocalTransform(Tools::convertAiToGLM(aNode->mTransformation));

    unsigned int numMeshes = aNode->mNumMeshes;
    if (numMeshes > 0)
    {
        fmt::print("{}: - node has {} meshes\n", __FUNCTION__, numMeshes);
        for (unsigned int i = 0; i < numMeshes; ++i)
        {
            aiMesh* modelMesh = scene->mMeshes[aNode->mMeshes[i]];

            Mesh mesh;
            mesh.ProcessMesh(renderData, modelMesh, scene, assetDirectory, mTextures);

            RMesh processedMesh = mesh.GetMesh();

            /* Convert mesh-local bone IDs into model-global IDs keyed by bone name. */
            std::vector<std::shared_ptr<Bone>> flatBones = mesh.GetBoneList();
            std::vector<uint32_t> localToGlobalBoneIds(flatBones.size(), 0);
            for (const auto& bone : flatBones)
            {
                const auto iter = std::find_if(mBoneList.begin(),
                                               mBoneList.end(),
                                               [bone](std::shared_ptr<Bone>& otherBone)
                                               { return bone->GetBoneName() == otherBone->GetBoneName(); });
                if (iter == mBoneList.end())
                {
                    const uint32_t globalBoneId = static_cast<uint32_t>(mBoneList.size());
                    mBoneList.emplace_back(
                            std::make_shared<Bone>(globalBoneId, bone->GetBoneName(), bone->GetOffsetMatrix()));
                    localToGlobalBoneIds.at(bone->GetBoneId()) = globalBoneId;
                }
                else
                {
                    localToGlobalBoneIds.at(bone->GetBoneId()) = (*iter)->GetBoneId();
                }
            }

            for (RVertex& vertex : processedMesh.vertices)
            {
                for (uint32_t boneWeightIndex = 0; boneWeightIndex < 4; ++boneWeightIndex)
                {
                    if (vertex.boneWeight[boneWeightIndex] <= 0.0f)
                    {
                        continue;
                    }

                    const uint32_t localBoneId = vertex.boneNumber[boneWeightIndex];
                    if (localBoneId >= localToGlobalBoneIds.size())
                    {
                        fmt::print(stderr,
                                   fg(fmt::color::red),
                                   "{} error: mesh '{}' references local bone id {}, but only {} local bones were registered\n",
                                   __FUNCTION__,
                                   mesh.GetMeshName(),
                                   localBoneId,
                                   localToGlobalBoneIds.size());
                        continue;
                    }

                    vertex.boneNumber[boneWeightIndex] = localToGlobalBoneIds[localBoneId];
                }
            }

            mModelMeshes.emplace_back(std::move(processedMesh));
        }
    }

    mNodeMap.insert({nodeName, node});
    mNodeList.emplace_back(node);

    unsigned int numChildren = aNode->mNumChildren;
    fmt::print("{}: - node has {} children \n", __FUNCTION__, numChildren);

    for (unsigned int i = 0; i < numChildren; ++i)
    {
        std::string childName = aNode->mChildren[i]->mName.C_Str();
        fmt::print("{}: --- found child node '{}'\n", __FUNCTION__, childName);

        std::shared_ptr<Node> childNode = node->AddChild(childName);
        processNode(renderData, childNode, aNode->mChildren[i], scene, assetDirectory);
    }
}
