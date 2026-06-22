#include <algorithm>
#include <cstdint>
#include <memory>

#include <fmt/base.h>
#include <fmt/color.h>
#include <NRI.h>

#include <Model/Mesh.h>
#include <Tools/Tools.h>

using namespace RAnimation;

bool Mesh::ProcessMesh(RRenderData& renderData,
                       aiMesh* mesh,
                       const aiScene* scene,
                       std::string assetDirectory,
                       std::unordered_map<std::string, RTextureData>& textures)
{
    mMeshName = mesh->mName.C_Str();
    mTriangleCount = mesh->mNumFaces;
    mVertexCount = mesh->mNumVertices;

    fmt::print("{}: -- mesh '{}' has {} faces ({} vertices)\n", __FUNCTION__, mMeshName, mTriangleCount, mVertexCount);
    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_COLOR_SETS; ++i)
    {
        if (mesh->HasVertexColors(i))
        {
            fmt::print("{}: --- mesh has vertex colors in set {}\n", __FUNCTION__, i);
        }
    }

    if (mesh->HasNormals())
    {
        fmt::print("{}: --- mesh has normals\n", __FUNCTION__);
    }

    for (unsigned int i = 0; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i)
    {
        if (mesh->HasTextureCoords(i))
        {
            fmt::print("{}: --- mesh has texture cooords in set {}\n", __FUNCTION__, i);
        }
    }

    aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
    if (material)
    {
        aiString materialName = material->GetName();
        fmt::print("{}: - material found, name '{}'\n", __FUNCTION__, materialName.C_Str());

        if (mesh->mMaterialIndex >= 0)
        {
            // scan only for diffuse and specular textures for a start
            std::vector<aiTextureType> supportedTexTypes = {aiTextureType_DIFFUSE, aiTextureType_SPECULAR};
            for (const auto& texType : supportedTexTypes)
            {
                unsigned int textureCount = material->GetTextureCount(texType);
                if (textureCount > 0)
                {
                    fmt::print("{}: -- material '{}' has %i images of type {}\n",
                               __FUNCTION__,
                               materialName.C_Str(),
                               textureCount,
                               static_cast<int>(texType));
                    for (unsigned int i = 0; i < textureCount; ++i)
                    {
                        aiString textureName;
                        material->GetTexture(texType, i, &textureName);

                        std::string texName = textureName.C_Str();

                        // Windows does understand forward slashes but Linux no backslashes
                        std::replace(texName.begin(), texName.end(), '\\', '/');

                        fmt::print("{}: --- image {} has name '{}'\n", __FUNCTION__, i, texName);

                        mMesh.textures.insert({texType, texName});

                        /* skip already loaded textures */
                        if (textures.count(texName) > 0)
                        {
                            fmt::print("{}: texture '{}' already loaded, skipping\n", __FUNCTION__, texName);
                            continue;
                        }

                        // do not try to load internal textures
                        if (!texName.empty() && texName.find("*") != 0)
                        {
                            std::string texNameWithPath = assetDirectory + '/' + texName;

                            auto [it, inserted] = textures.try_emplace(texName);
                            if (!inserted)
                            {
                                continue;
                            }

                            RTextureData& texData = it->second;

                            if (!utils::LoadTexture(texNameWithPath, texData.texture))
                            {
                                fmt::print(stderr,
                                           fg(fmt::color::red),
                                           "{} error: could not load texture file '{}', skipping\n",
                                           __FUNCTION__,
                                           texNameWithPath);

                                textures.erase(it);
                                continue;
                            }
                        }
                    }
                }
            }
        }

        aiColor4D baseColor(0.0f, 0.0f, 0.0f, 1.0f);
        if (material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == aiReturn_SUCCESS && mMesh.textures.empty())
        {
            mBaseColor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
            mMesh.usesPBRColors = true;
        }
    }

    for (unsigned int i = 0; i < mVertexCount; ++i)
    {
        RVertex vertex;
        vertex.position.x = mesh->mVertices[i].x;
        vertex.position.y = mesh->mVertices[i].y;
        vertex.position.z = mesh->mVertices[i].z;

        if (mesh->HasVertexColors(0))
        {
            vertex.color.r = mesh->mColors[0][i].r;
            vertex.color.g = mesh->mColors[0][i].g;
            vertex.color.b = mesh->mColors[0][i].b;
            vertex.color.a = mesh->mColors[0][i].a;
        }
        else
        {
            if (mMesh.usesPBRColors)
            {
                vertex.color = mBaseColor;
            }
            else
            {
                vertex.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        if (mesh->HasNormals())
        {
            vertex.normal.x = mesh->mNormals[i].x;
            vertex.normal.y = mesh->mNormals[i].y;
            vertex.normal.z = mesh->mNormals[i].z;
        }
        else
        {
            vertex.normal = glm::vec3(0.0f);
        }

        if (mesh->HasTextureCoords(0))
        {
            vertex.uv.x = mesh->mTextureCoords[0][i].x;
            vertex.uv.y = mesh->mTextureCoords[0][i].y;
        }
        else
        {
            vertex.uv = glm::vec2(0.0f);
        }

        mMesh.vertices.emplace_back(vertex);
    }

    for (unsigned int i = 0; i < mTriangleCount; ++i)
    {
        aiFace face = mesh->mFaces[i];
        mMesh.indices.push_back(face.mIndices[0]);
        mMesh.indices.push_back(face.mIndices[1]);
        mMesh.indices.push_back(face.mIndices[2]);
    }

    if (mesh->HasBones())
    {
        unsigned int numBones = mesh->mNumBones;
        fmt::print("{}: -- mesh has information about {} bones\n", __FUNCTION__, numBones);
        for (unsigned int boneId = 0; boneId < numBones; ++boneId)
        {
            std::string boneName = mesh->mBones[boneId]->mName.C_Str();
            unsigned int numWeights = mesh->mBones[boneId]->mNumWeights;
            fmt::print("{}: --- bone nr. {} has name {}, contains {} weights\n",
                       __FUNCTION__,
                       boneId,
                       boneName,
                       numWeights);

            std::shared_ptr<Bone> newBone = std::make_shared<Bone>(
                    boneId, boneName, Tools::convertAiToGLM(mesh->mBones[boneId]->mOffsetMatrix));
            mBoneList.push_back(newBone);

            for (unsigned int weight = 0; weight < numWeights; ++weight)
            {
                unsigned int vertexId = mesh->mBones[boneId]->mWeights[weight].mVertexId;
                float vertexWeight = mesh->mBones[boneId]->mWeights[weight].mWeight;

                glm::uvec4 currentIds = mMesh.vertices.at(vertexId).boneNumber;
                glm::vec4 currentWeights = mMesh.vertices.at(vertexId).boneWeight;

                int targetSlot = -1;
                for (unsigned int i = 0; i < 4; ++i)
                {
                    if (currentWeights[i] == 0.0f)
                    {
                        targetSlot = static_cast<int>(i);
                        break;
                    }
                }

                if (targetSlot < 0)
                {
                    targetSlot = 0;
                    for (int slot = 1; slot < 4; ++slot)
                    {
                        if (currentWeights[slot] < currentWeights[targetSlot])
                        {
                            targetSlot = slot;
                        }
                    }

                    if (vertexWeight <= currentWeights[targetSlot])
                    {
                        continue;
                    }
                }

                currentIds[targetSlot] = boneId;
                currentWeights[targetSlot] = vertexWeight;

                mMesh.vertices.at(vertexId).boneNumber = currentIds;
                mMesh.vertices.at(vertexId).boneWeight = currentWeights;
            }
        }

        for (RVertex& vertex : mMesh.vertices)
        {
            const float weightSum =
                    vertex.boneWeight.x + vertex.boneWeight.y + vertex.boneWeight.z + vertex.boneWeight.w;
            if (weightSum > 0.0f)
            {
                vertex.boneWeight /= weightSum;
            }
        }
    }

    return true;
}

std::string Mesh::GetMeshName()
{
    return mMeshName;
}

unsigned int Mesh::GetTriangleCount()
{
    return mTriangleCount;
}

unsigned int Mesh::GetVertexCount()
{
    return mVertexCount;
}

RMesh Mesh::GetMesh()
{
    return mMesh;
}

std::vector<uint32_t> Mesh::GetIndices()
{
    return mMesh.indices;
}

std::vector<std::shared_ptr<Bone>> Mesh::GetBoneList()
{
    return mBoneList;
}
