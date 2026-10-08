#pragma once
#include <memory>
#include <string>

#include <Platform/SDL/SDLPlatform.h>
#include <Renderer/Renderer.h>
#include <Renderer/UserInterface.h>
#include <Editor/SceneEditor.h>
#include <AnimGraph/AnimationSystem.h>

namespace RAnimation
{
    // Command-line smoke test (see Program.cpp). Empty / negative values mean "not set".
    struct SmokeTestOptions
    {
        std::string scenePath;
        std::string evalMode;  // cpu | a | b | c
        std::string graphPath;
        int crowd = 0;
        int frames = -1;
        bool verify = false; // compare GPU skinning matrices with the CPU pose at the final frame
    };

    class Application final
    {
    public:
        bool init(unsigned int width, unsigned int height, std::string title);
        // Applies the smoke-test options after init (scene import, tier, graph, crowd). Returns false on error.
        bool ApplySmokeTestOptions(const SmokeTestOptions& options);
        void MainLoop();
        void Cleanup();
    private:
        void printFrameStats(const char* tag) const;
        void runVerification();

        int mFrameLimit = -1;
        int mFramesRendered = 0;
        bool mVerifyAtEnd = false;
        bool mVerifyFailed = false;

    public:
        bool VerificationFailed() const { return mVerifyFailed; }

    private:

        std::unique_ptr<Renderer> mRenderer = nullptr;
        std::unique_ptr<SDLPlatform> mPlatform = nullptr;

        // Editor control layer (owns scene data + undo/redo + mode) and the UI that drives it. The
        // Renderer knows about neither; the Application is the only place they meet.
        SceneEditor mSceneEditor{};
        UserInterface mUserInterface{};

        // Explicit per-frame animation stage (graph update, CPU seams, Work records). GPU-free; the
        // renderer only packs what this produced.
        AnimationSystem mAnimationSystem{};

        // Window title without the mode suffix; a "  [edit]" tag is appended while in Edit mode.
        std::string mBaseTitle;
    };
} // namespace RAnimation