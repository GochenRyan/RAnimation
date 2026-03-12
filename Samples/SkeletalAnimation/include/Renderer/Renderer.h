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
        bool createPipelines();

    private:
        RRenderData mRenderData{};
        ModelAndInstanceData mModelInstData{};

        Timer mFrameTimer{};
        Timer mMatrixGenerateTimer{};
        Timer mUploadToUBOTimer{};
        Timer mUIGenerateTimer{};
        Timer mUIDrawTimer{};

        UserInterface mUserInterface{};
        Camera mCamera;
    };
} // namespace RAnimation