#include <Platform/SDL/SDLPlatform.h>
#include <Platform/SDL/SDLWindow.h>
#include <Application/Application.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

#include <fmt/base.h>
#include <fmt/color.h>
#include <imgui.h>

#include <AnimGraph/AnimGraphAsset.h>
#include <Model/UsdSceneExporter.h>

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

    // The character state machine is optional: a missing definition / graph only disables the panel.
    std::string controllerError;
    if (!mCharacterController.Init(ASSETS_SRC_DIR "/SkeletalAnimControls", controllerError))
    {
        fmt::print(stderr, fg(fmt::color::yellow), "CharacterController disabled: {}\n", controllerError);
    }
    mUserInterface.SetCharacterController(&mCharacterController);

    return true;
}

void Application::updateCharacterInput()
{
    if (!mCharacterController.IsAttached() || mControllerScript)
    {
        return;
    }
    // Free camera owns WASD; every other camera follows the selected instance, so the keys drive the
    // character. Text fields / hovered panels keep the keyboard (same guard as the cameras).
    const ImGuiIO& io = ImGui::GetIO();
    const bool keysFree = !io.WantCaptureKeyboard && !io.WantTextInput;
    const bool characterKeys = keysFree && mRenderer->GetRenderData().rdCameraRig.ActiveType() != CameraType::Free;
    CharacterInput input;
    if (characterKeys)
    {
        // W/S forward, A/D strafe, Q/E turn, Shift jog, Alt sprint, Ctrl crouch, V aim (+ Up/Down pitch),
        // Space jump, F punch, H hit, G toggle weapon, K die.
        input.moveY = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
        input.moveX = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
        input.turn = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
        input.run = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        input.sprint = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
        input.crouch = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        input.aim = ImGui::IsKeyDown(ImGuiKey_V);
        input.aimPitch = (ImGui::IsKeyDown(ImGuiKey_UpArrow) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_DownArrow) ? 1.0f : 0.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
        {
            mCharacterController.Jump();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            mCharacterController.RequestAction("punch");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
        {
            mCharacterController.RequestAction("hit");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G, false))
        {
            mCharacterController.ToggleWeapon();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_K, false))
        {
            mCharacterController.Die();
        }
    }
    mCharacterController.SetInput(input);
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
        if (mFrameLimit >= 0)
        {
            if (mFramesRendered > 0 && mFramesRendered % 30 == 0)
            {
                printFrameStats("frame");
            }
            if (mFramesRendered >= mFrameLimit)
            {
                printFrameStats("final");
                if (mVerifyAtEnd)
                {
                    runVerification();
                }
                if (mControllerScript)
                {
                    verifyControllerLog();
                }
                break;
            }
        }
        if (mFixedDeltaTime > 0.0f)
        {
            deltaTime = mFixedDeltaTime;
        }

        mRenderer->SetSize(mPlatform->GetMainWindow()->GetWidth(), mPlatform->GetMainWindow()->GetHeight());

        // Build the UI for this frame (reads renderer telemetry, reads/edits the SceneEditor) before
        // rendering, so this frame's ImguiPass draws this frame's UI. ImGui draw data is global state.
        mUserInterface.CreateFrame(mRenderer->GetRenderData(), mSceneEditor);
        mUserInterface.Render(mRenderer->GetRenderData());

        // Character state machine first: it decides which graph / blend input this frame evaluates.
        if (mControllerScript)
        {
            runControllerScript(mFramesRendered);
        }
        updateCharacterInput();
        mCharacterController.Update(deltaTime, mSceneEditor.ModelData());

        // Advance every instance's animation graph (time, CPU seams, Work records) before the camera reads
        // poses and before the renderer packs the frame.
        mAnimationSystem.Update(deltaTime, mSceneEditor.ModelData());

        // Drive the active camera from this frame's ImGui input + the selected instance, after the UI
        // frame is built (so IO is populated) and before Draw uploads the camera matrices.
        mRenderer->UpdateActiveCamera(deltaTime, mSceneEditor.ModelData());

        if (!mRenderer->Draw(deltaTime, mSceneEditor.ModelData()))
        {
            break;
        }
        ++mFramesRendered;

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
        mCharacterController.Detach();
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
