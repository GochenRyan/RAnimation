#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <assimp/scene.h>

#include <Model/Bone.h>
#include <Model/RenderData.h>

namespace RAnimation
{
    class Mesh
    {
    public:
        bool ProcessMesh(RRenderData& renderData,
                         aiMesh* mesh,
                         const aiScene* scene,
                         std::string assetDirectory,
                         std::unordered_map<std::string, RTextureData>& textures);

        std::string GetMeshName();
        unsigned int GetTriangleCount();
        unsigned int GetVertexCount();

        RMesh GetMesh();
        std::vector<uint32_t> GetIndices();
        std::vector<std::shared_ptr<Bone>> GetBoneList();

    private:
        std::string mMeshName;
        unsigned int mTriangleCount = 0;
        unsigned int mVertexCount = 0;

        glm::vec4 mBaseColor = glm::vec4(1.0f);

        RMesh mMesh{};
        std::vector<std::shared_ptr<Bone>> mBoneList{};
    };
} // namespace RAnimation
