#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <AnimGraph/Pose.h>
#include <Model/AnimClip.h>
#include <Model/ClipSampling.h>
#include <Model/Bone.h>
#include <Model/Node.h>
#include <Model/RenderData.h>
#include <RHIWrap/NRIInterface.h>
#include <Renderer/NRITrsTexture.h>
#include <TrsTexture/TrsTexture.h>

namespace RAnimation
{
    struct UsdLoadedModel;

    class Model
    {
    public:
        bool LoadModel(RRenderData& renderData, std::string modelFilename, unsigned int extraImportFlags = 0);
        glm::mat4 GetRootTranformationMatrix();

        void Draw(RRenderData& renderData);
        void DrawInstanced(RRenderData& renderData, uint32_t instanceCount);
        unsigned int GetTriangleCount() const;

        std::string GetModelFileName() const;
        std::string GetModelFileNamePath() const;

        bool HasAnimations() const;
        const std::vector<std::shared_ptr<AnimClip>>& GetAnimClips() const;

        const std::vector<std::shared_ptr<Node>>& GetNodeList() const;
        const std::unordered_map<std::string, std::shared_ptr<Node>>& GetNodeMap() const;

        const std::vector<std::shared_ptr<Bone>>& GetBoneList() const;
        const std::unordered_map<std::string, glm::mat4>& GetInverseBindMatrices() const;

        const std::shared_ptr<Node> GetRootNode();

        // Node-order tables shared by the AnimGraph runtime, the camera pose and the TRS texture path.
        const std::vector<Transform>& GetBindPose() const { return mBindPose; }
        const std::unordered_map<std::string, int32_t>& GetNodeIndexByName() const { return mNodeIndexByName; }
        const std::vector<int32_t>& GetNodeParentIndices() const { return mNodeParentIndex; }
        const ChannelToNodeTable& GetChannelToNodeTable(size_t clipIndex) const { return mChannelToNode.at(clipIndex); }
        // Global matrices (instanceLocal * node0 * ... * node) for a pose, in node order. Replaces reading
        // the shared Node tree after UpdateAnimation(0).
        void ComputeNodeGlobals(const Pose& pose, const glm::mat4& instanceLocal, std::vector<glm::mat4>& out) const;
        int32_t FindNodeIndex(const std::string& name) const;

        // Baked TRS texture, CPU side (header + clip table + texels). The GPU objects are created by the
        // renderer (Phase 3). Not ready => the GPU evaluation tiers fall back to the CPU tier for this model.
        bool IsTrsTexReady() const { return mTrsTexReady; }
        const BoneTrsTexture& GetTrsTexCpu() const { return mTrsTexCpu; }
        const std::string& GetTrsTexPath() const { return mTrsTexPath; }
        const std::string& GetTrsTexStatus() const { return mTrsTexStatus; }
        void SetTrsTexStatus(std::string status) { mTrsTexStatus = std::move(status); }
        TrsTextureGpu& TrsTexGpu() { return mTrsTexGpu; }
        const TrsTextureGpu& TrsTexGpu() const { return mTrsTexGpu; }

        void Cleanup(RRenderData& renderData);

    private:
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
        std::vector<nri::Memory*> mBufferMemories{};

        // map textures to external or internal texture names
        std::unordered_map<std::string, RTextureData> mTextures{};
        RTextureData mPlaceholderTexture{};
        RTextureData mWhiteTexture{};

        glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);

        std::vector<Transform> mBindPose{};
        std::unordered_map<std::string, int32_t> mNodeIndexByName{};
        std::vector<int32_t> mNodeParentIndex{};
        std::vector<ChannelToNodeTable> mChannelToNode{};

        BoneTrsTexture mTrsTexCpu{};
        bool mTrsTexReady = false;
        std::string mTrsTexPath;
        std::string mTrsTexStatus = "not loaded";
        TrsTextureGpu mTrsTexGpu{};
        void loadTrsTextureCpu(const UsdLoadedModel& loaded, const std::string& assetPath);

        std::string mModelFilenamePath;
        std::string mModelFilename;
    };
} // namespace RAnimation
