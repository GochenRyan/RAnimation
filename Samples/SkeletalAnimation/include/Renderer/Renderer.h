#pragma once
#include <string>
#include <memory>

#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>
#include <Platform/NativeWindowHandle.h>
#include <Renderer/UserInterface.h>
#include <Tools/Timer.h>
#include <Tools/Camera.h>

namespace RAnimation
{
    // Settings
    constexpr nri::VKBindingOffsets VK_BINDING_OFFSETS = {0, 128, 32, 64};
    constexpr bool D3D11_ENABLE_COMMAND_BUFFER_EMULATION = false;
    constexpr bool D3D12_DISABLE_ENHANCED_BARRIERS = false;

    constexpr uint32_t VP_MATRIX_BUFFER = 0;
    constexpr uint32_t WORLD_POS_BUFFER = 1;
    constexpr uint32_t MODEL_BONE_BUFFER = 2;
    constexpr uint32_t VERTEX_BUFFER = 3;

    constexpr uint32_t TEXTURES_PER_MATERIAL = 1;

    class Renderer final
    {
    public:
        Renderer(NativeWindowHandle* window);
        bool Init(unsigned int width, unsigned int height);
        void SetSize(unsigned int width, unsigned int height);

        bool Draw(float deltaTime);

        void HandleKeyEvents(int key, int scancode, int action, int mods);
        void HandleMouseButtonEvents(int button, int action, int mods);
        void HandleMousePositionEvents(double xPos, double yPos);

        bool HasModel(std::string modelFileName);
        std::shared_ptr<RAnimation::Model> GetModel(std::string modelFileName);
        bool AddModel(std::string modelFileName);
        void DeleteModel(std::string modelFileName);

        std::shared_ptr<RAnimation::ModelInstance> AddInstance(std::shared_ptr<RAnimation::Model> model);
        void AddInstances(std::shared_ptr<RAnimation::Model> model, int numInstances);
        void DeleteInstance(std::shared_ptr<RAnimation::ModelInstance> instance);
        void CloneInstance(std::shared_ptr<RAnimation::ModelInstance> instance);

        void Cleanup();

    private:
        bool initDevice();
        bool initNRI();
        bool createStreamer();
        bool getQueue();
        bool createSyncObjects();
        bool createSwapchain();
        bool createQueuedFrames();
        bool createPipelineLayout();
        bool createPipelines();
        bool createMatrixUBO();
        bool createSSBOs();
        bool allocateAndBindMemory();
        bool createDescriptors();
        bool createDescriptorPool();
        bool createDescriptorLayouts();
        bool createDescriptorSets();
        bool createSwapchainTextures();

        void updateTriangleCount();

        void latencySleep(uint32_t frameIndex);

    private:
        RRenderData mRenderData{};
        ModelAndInstanceData mModelInstData{};

        Timer mFrameTimer{};
        Timer mMatrixGenerateTimer{};
        Timer mUploadToUBOTimer{};
        Timer mUIGenerateTimer{};
        Timer mUIDrawTimer{};

        uint32_t mFrameIndex = 0;

        UserInterface mUserInterface{};
        Camera mCamera;

        /* for non-animated models */
        std::vector<glm::mat4> mWorldPosMatrices;

        /* for animated models */
        std::vector<glm::mat4> mModelBoneMatrices;
    };
} // namespace RAnimation