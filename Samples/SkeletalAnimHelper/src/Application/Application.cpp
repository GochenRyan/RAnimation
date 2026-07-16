#include <Platform/SDL/SDLPlatform.h>
#include <Platform/SDL/SDLWindow.h>
#include <Application/Application.h>

#include <chrono>

using namespace RAnimation;

bool Application::init(unsigned int width, unsigned int height, std::string title)
{
    mPlatform = std::make_unique<SDLPlatform>();
    if (!mPlatform->Initialize())
    {
        return false;
    }

    mBaseTitle = title;

    WindowDesc windowDesc{};
    windowDesc.width = width;
    windowDesc.height = height;
    windowDesc.title = title;
    windowDesc.resizable = true;
    windowDesc.highDPI = true;
    windowDesc.maximized = false;

    IWindow* window = mPlatform->CreateWindow(windowDesc);
    if (!window)
    {
        return false;
    }

    NativeWindowHandle nativeWindowHandle = window->GetNativeHandle();
    SDL_Window* sdlWindow = nullptr;
    if (auto* sdlPlatformWindow = dynamic_cast<SDLWindow*>(window))
    {
        sdlWindow = sdlPlatformWindow->GetSDLHandle();
    }

    mRenderer = std::make_unique<Renderer>(&nativeWindowHandle, sdlWindow);
    if (!mRenderer->Init(width, height))
    {
        return false;
    }

    if (!mUserInterface.Init(mRenderer->GetRenderData()))
    {
        return false;
    }

    // The SceneEditor performs structural edits but owns no GPU code; route its GPU needs to the
    // Renderer's services. This is the only seam where the editor and renderer meet.
    Renderer* renderer = mRenderer.get();
    mSceneEditor.SetGpuHooks(
            [renderer](const std::string& fileName) { return renderer->LoadModel(fileName); },
            [renderer](const std::shared_ptr<Model>& model) { renderer->ReleaseModel(model); },
            [renderer](const glm::vec3& point) { renderer->FocusCameraOnPoint(point); });

    return true;
}

void Application::MainLoop()
{
    std::chrono::time_point<std::chrono::steady_clock> loopStartTime = std::chrono::steady_clock::now();
    std::chrono::time_point<std::chrono::steady_clock> loopEndTime = std::chrono::steady_clock::now();
    float deltaTime = 0.0f;

    // Reflect the editor mode in the window title. Start in View (no suffix); update only on change.
    bool lastEditMode = mSceneEditor.IsEditMode();
    mPlatform->GetMainWindow()->SetTitle((mBaseTitle + (lastEditMode ? "  [edit]" : "")).c_str());

    while (!mPlatform->GetMainWindow()->ShouldClose())
    {
        mRenderer->SetSize(mPlatform->GetMainWindow()->GetWidth(), mPlatform->GetMainWindow()->GetHeight());

        // Build the UI for this frame (reads renderer telemetry, reads/edits the SceneEditor) before
        // rendering, so this frame's ImguiPass draws this frame's UI. ImGui draw data is global state.
        mUserInterface.CreateFrame(mRenderer->GetRenderData(), mSceneEditor);
        mUserInterface.Render(mRenderer->GetRenderData());

        // Drive the active camera from this frame's ImGui input + the selected instance, after the UI
        // frame is built (so IO is populated) and before Draw uploads the camera matrices.
        mRenderer->UpdateActiveCamera(deltaTime, mSceneEditor.ModelData());

        if (!mRenderer->Draw(deltaTime, mSceneEditor.ModelData()))
        {
            break;
        }

        const bool editMode = mSceneEditor.IsEditMode();
        if (editMode != lastEditMode)
        {
            mPlatform->GetMainWindow()->SetTitle((mBaseTitle + (editMode ? "  [edit]" : "")).c_str());
            lastEditMode = editMode;
        }

        mPlatform->PumpEvents();

        /* calculate the time we needed for the current frame, feed it to the next draw() call */
        loopEndTime = std::chrono::steady_clock::now();

        /* delta time in seconds */
        deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(loopEndTime - loopStartTime).count() /
                    1'000'000.0f;

        loopStartTime = loopEndTime;
    }
}

void Application::Cleanup()
{
    if (mRenderer)
    {
        // Order matters: the device must stay alive while the UI and the scene's models release their
        // GPU resources. The Renderer tears down its own device last.
        mRenderer->WaitIdle();
        mUserInterface.Cleanup(mRenderer->GetRenderData());
        mSceneEditor.ReleaseAllGpuResources();
        mRenderer->Cleanup();
        mRenderer.reset();
    }

    if (mPlatform)
    {
        mPlatform->Shutdown();
        mPlatform.reset();
    }
}
