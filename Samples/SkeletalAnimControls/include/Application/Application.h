#pragma once
#include <memory>
#include <string>

#include <Platform/SDL/SDLPlatform.h>
#include <Renderer/Renderer.h>
#include <Renderer/UserInterface.h>
#include <Editor/SceneEditor.h>

namespace RAnimation
{
    class Application final
    {
    public:
        bool init(unsigned int width, unsigned int height, std::string title);
        void MainLoop();
        void Cleanup();
    private:
        std::unique_ptr<Renderer> mRenderer = nullptr;
        std::unique_ptr<SDLPlatform> mPlatform = nullptr;

        // Editor control layer (owns scene data + undo/redo + mode) and the UI that drives it. The
        // Renderer knows about neither; the Application is the only place they meet.
        SceneEditor mSceneEditor{};
        UserInterface mUserInterface{};

        // Window title without the mode suffix; a "  [edit]" tag is appended while in Edit mode.
        std::string mBaseTitle;
    };
} // namespace RAnimation