#pragma once
#include <string>
#include <memory>

#include <Model/Model.h>
#include <Model/ModelAndInstanceData.h>
#include <Model/ModelInstance.h>
#include <Platform/NativeWindowHandle.h>
#include <Renderer/RenderResourceBudget.h>
#include <Renderer/RenderResourceRegistry.h>
#include <Renderer/SceneFrameData.h>
#include <Tools/Timer.h>
#include <Tools/Camera.h>

struct SDL_Window;

namespace RAnimation
{
    // Settings
    // Keep runtime Vulkan binding offsets aligned with ShaderMake's SPIR-V defaults.
    constexpr nri::VKBindingOffsets VK_BINDING_OFFSETS = {100, 200, 300, 400};
    constexpr bool D3D11_ENABLE_COMMAND_BUFFER_EMULATION = false;
    constexpr bool D3D12_DISABLE_ENHANCED_BARRIERS = false;

    constexpr uint32_t TEXTURES_PER_MATERIAL = 1;

    class Renderer final
    {
    public:
        Renderer(NativeWindowHandle* window);
        Renderer(NativeWindowHandle* window, SDL_Window* sdlWindow);
        bool Init(unsigned int width, unsigned int height);
        void SetSize(unsigned int width, unsigned int height);

        // Renders one frame of the given scene. The Renderer holds no scene/editor state of its own;
        // the caller (Application) owns the scene data and the UI build.
        bool Draw(float deltaTime, ModelAndInstanceData& modInstData);

        void HandleKeyEvents(int key, int scancode, int action, int mods);
        void HandleMouseButtonEvents(int button, int action, int mods);
        void HandleMousePositionEvents(double xPos, double yPos);

        // GPU services exposed to the editor layer (wired in as hooks) and the host application.
        // The Renderer knows nothing about commands, undo/redo, or modes.
        std::shared_ptr<RAnimation::Model> LoadModel(const std::string& modelFileName);
        void ReleaseModel(const std::shared_ptr<RAnimation::Model>& model);
        void FocusCameraOnPoint(const glm::vec3& focusPoint);

        void WaitIdle();
        RRenderData& GetRenderData() { return mRenderData; }

        void Cleanup();

    private:
        bool initDevice();
        bool initNRI();
        bool createStreamer();
        bool getQueue();
        bool createSyncObjects();
        bool createSwapchain();
        bool createQueuedFrames();
        bool registerPasses();
        bool allocateAndBindMemory();
        bool createSampler();
        bool createDescriptorPool();
        bool createPassPipelinesAndDescriptors();
        bool createSwapchainTextures();
        bool createDepthAttachmentResources();
        bool createPickingResources();
        bool recreateSwapchain();
        void destroySwapchainResources();

        // Picking: issue a 1px readback after the scene pass, then resolve completed readbacks.
        void resolvePendingReadback(ModelAndInstanceData& modInstData);
        void recordPickReadback(nri::CommandBuffer& commandBuffer);

        void updateTriangleCount(const ModelAndInstanceData& modInstData);

        void latencySleep(uint32_t frameIndex);

        bool recordCommandBuffer();

    private:
        RRenderData mRenderData{};
        RenderResourceBudget mResourceBudget{};

        Timer mFrameTimer{};

        uint32_t mFrameIndex = 0;

        Camera mCamera;

        // Per-frame scene state. Filled by Renderer::Draw (modelInstData/hasSceneGeometry) and
        // by AnimationTransformComputePass::Upload (animatedDispatches/uploadedBoneOffsetMatrixCount).
        // Consumed by passes during Record() via CommandContext::sceneFrame.
        SceneFrameData mSceneFrame{};

        // Owned by the PassRegistry; cached so the Renderer can re-point its pick-ID SRV on resize.
        class OutlinePass* mOutlinePass = nullptr;

        bool mDepthAttachmentInitialized = false;
        bool mSwapchainRecreateRequested = false;

        // Tracks the current barrier state of the R32_UINT pick-ID texture across its per-frame
        // cycle (COLOR_ATTACHMENT -> COPY_SOURCE? -> SHADER_RESOURCE). Reset on swapchain recreate.
        nri::AccessBits mPickIdAccess = nri::AccessBits::NONE;
        nri::Layout mPickIdLayout = nri::Layout::UNDEFINED;
        nri::StageBits mPickIdStage = nri::StageBits::NONE;
    };
} // namespace RAnimation
