#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>

#include <assimp/scene.h>

#include <Model/Bone.h>
#include <Model/RenderData.h>

class Mesh {
  public:
    bool ProcessMesh(RRenderData &renderData, aiMesh* mesh, const aiScene* scene, std::string assetDirectory,
      std::unordered_map<std::string, nri::Texture> &textures);

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
