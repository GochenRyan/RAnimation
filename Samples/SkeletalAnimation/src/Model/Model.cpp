#include <filesystem>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <fmt/base.h>
#include <fmt/color.h>

#include <Model/Model.h>
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

        for (int i = 0; i < scene->mNumTextures; ++i)
        {
            std::string texName = scene->mTextures[i]->mFilename.C_Str();

            int height = scene->mTextures[i]->mHeight;
            int width = scene->mTextures[i]->mWidth;
            aiTexel* data = scene->mTextures[i]->pcData;

            RTextureData newTex{};
            if (!utils::LoadTexture(texName, newTex.texture))
            {
                return false;
            }

            std::string internalTexName = "*" + std::to_string(i);
            fmt::print("{}: - added internal texture '{}'\n", __FUNCTION__, internalTexName);
            mTextures.insert({internalTexName, newTex});
        }

        fmt::print("{}: scene has {} embedded textures\n", __FUNCTION__, numTextures);
    }

    /* add a white texture in case there is no diffuse tex but colors */
    std::string whiteTexName = "textures/white.png";
    if (!utils::LoadTexture(whiteTexName, mWhiteTexture.texture))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: could not load white default texture '{}'\n",
                   __FUNCTION__,
                   whiteTexName);
        return false;
    }

    /* add a placeholder texture in case there is no diffuse tex */
    std::string placeholderTexName = "textures/missing_tex.png";
    if (!utils::LoadTexture(placeholderTexName, mPlaceholderTexture.texture))
    {
        fmt::print(stderr,
                   fg(fmt::color::red),
                   "{} error: could not load placeholder texture '{}'\n",
                   __FUNCTION__,
                   placeholderTexName);
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
            mBoneOffsetMatrices.insert(
                    {nodeName, mBoneList.at(std::distance(mBoneList.begin(), boneIter))->GetOffsetMatrix()});
        }
    }

    /* create vertex buffers for the meshes */
    for (const auto& mesh : mModelMeshes)
    {
        nri::Buffer* vertexBuffer = nullptr;
        nri::BufferDesc vertexBufferDesc = {};
        vertexBufferDesc.size = mesh.vertices.size() * sizeof(RVertex);
        vertexBufferDesc.usage = nri::BufferUsageBits::VERTEX_BUFFER;
        NRI_ABORT_ON_FAILURE(renderData.NRI.CreateBuffer(*renderData.mDevice, vertexBufferDesc, vertexBuffer));
        mVertexBuffers.emplace_back(vertexBuffer);

        nri::Buffer* indexBuffer = nullptr;
        nri::BufferDesc indexBufferDesc = {};
        indexBufferDesc.size = mesh.indices.size() * sizeof(uint32_t);
        indexBufferDesc.usage = nri::BufferUsageBits::INDEX_BUFFER;
        NRI_ABORT_ON_FAILURE(renderData.NRI.CreateBuffer(*renderData.mDevice, indexBufferDesc, indexBuffer));
        mIndexBuffers.emplace_back(indexBuffer);
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
    fmt::print("{}: - model has a total of {} bone{}\n",
               __FUNCTION__,
               mBoneList.size(),
               mBoneList.size() == 1 ? "" : "s");
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
}

void Model::DrawInstanced(RRenderData& renderData, uint32_t instanceCount)
{
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

const std::unordered_map<std::string, glm::mat4>& Model::GetBoneOffsetMatrices()
{
    return mBoneOffsetMatrices;
}

const std::shared_ptr<Node> Model::GetRootNode()
{
    return mRootNode;
}

void Model::Cleanup(RRenderData& renderData)
{
}

void Model::processNode(RRenderData& renderData,
                        std::shared_ptr<Node> node,
                        aiNode* aNode,
                        const aiScene* scene,
                        std::string assetDirectory)
{
}

void Model::createNodeList(std::shared_ptr<Node> node,
                           std::shared_ptr<Node> newNode,
                           std::vector<std::shared_ptr<Node>>& list)
{
}
