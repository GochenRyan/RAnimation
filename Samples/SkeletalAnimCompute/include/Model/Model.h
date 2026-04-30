#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <assimp/scene.h>

#include <Model/AnimClip.h>
#include <Model/Bone.h>
#include <Model/Node.h>
#include <Model/RenderData.h>
#include <RHIWrap/NRIInterface.h>

namespace RAnimation
{
    class Model
    {
    public:
        bool LoadModel(RRenderData& renderData, std::string modelFilename, unsigned int extraImportFlags = 0);
        glm::mat4 GetRootTranformationMatrix();

        void Draw(RRenderData& renderData);
        void DrawInstanced(RRenderData& renderData, uint32_t instanceCount);
        unsigned int GetTriangleCount();

        std::string GetModelFileName();
        std::string GetModelFileNamePath();

        bool HasAnimations();
        const std::vector<std::shared_ptr<AnimClip>>& GetAnimClips();

        const std::vector<std::shared_ptr<Node>>& GetNodeList();
        const std::unordered_map<std::string, std::shared_ptr<Node>>& GetNodeMap();

        const std::vector<std::shared_ptr<Bone>>& GetBoneList();
        const std::unordered_map<std::string, glm::mat4>& GetInverseBindMatrices();

    const std::shared_ptr<Node> GetRootNode();

        void Cleanup(RRenderData& renderData);

    private:
        void processNode(RRenderData& renderData,
                         std::shared_ptr<Node> node,
                         aiNode* aNode,
                         const aiScene* scene,
                         std::string assetDirectory);

        unsigned int mTriangleCount = 0;
        unsigned int mVertexCount = 0;

        /* store the root node for direct access */
        std::shared_ptr<Node> mRootNode = nullptr;
        /* a map to find the node by name */
        std::unordered_map<std::string, std::shared_ptr<Node>> mNodeMap{};
        /* and a 'flat' map to keep the order of insertation  */
        std::vector<std::shared_ptr<Node>> mNodeList{};

        std::vector<std::shared_ptr<Bone>> mBoneList;
        std::unordered_map<std::string, glm::mat4> mInverseBindMatrices{};

        std::vector<std::shared_ptr<AnimClip>> mAnimClips{};

        std::vector<RMesh> mModelMeshes{};
        std::vector<nri::Buffer*> mVertexBuffers{};
        std::vector<nri::Buffer*> mIndexBuffers{};
        nri::Buffer* mShaderBoneParentBuffer = nullptr;
        nri::Buffer* mShaderBoneMatrixOffsetBuffer = nullptr;
        std::vector<nri::Memory*> mBufferMemories{};

        // map textures to external or internal texture names
        std::unordered_map<std::string, RTextureData> mTextures{};
        RTextureData mPlaceholderTexture{};
        RTextureData mWhiteTexture{};

        glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);

        std::string mModelFilenamePath;
        std::string mModelFilename;
    };
} // namespace RAnimation
