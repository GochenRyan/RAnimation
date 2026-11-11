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

    // The character state machine is optional: a missing definition / graph only disables the agents.
    // This sample's definition (with CancelAction) lives under Assets/SkeletalAnimBehavior/statemachine.
    std::string controllerError;
    if (!mCharacterSystem.Init(ASSETS_SRC_DIR "/SkeletalAnimBehavior", controllerError))
    {
        fmt::print(stderr, fg(fmt::color::yellow), "CharacterSystem disabled: {}\n", controllerError);
    }
    mUserInterface.SetCharacterSystem(&mCharacterSystem);
    mUserInterface.SetCollisionSystem(&mCollisionSystem);
    initBehavior();

    return true;
}

void Application::updateCollision(float deltaTime)
{
    ModelAndInstanceData& scene = mSceneEditor.ModelData();
    if (!mCollisionSystem.IsInitialized() || !mCollisionSystem.GetSettings().enabled)
    {
        scene.miDebugLines.Clear();
        return;
    }
    mCollisionSystem.SyncStatics(scene);
    mCollisionSystem.Sync(scene);
    mCollisionSystem.UpdateBodies(scene);
    mCollisionSystem.Step(deltaTime);
    const std::span<const CollisionPush> pushes = mCollisionSystem.Resolve();
    if (mCollisionSystem.GetSettings().pushApart)
    {
        for (const CollisionPush& push : pushes)
        {
            ModelInstance* instance = mCollisionSystem.InstanceForSlot(push.slot);
            if (instance == nullptr)
            {
                continue;
            }
            if (Agent* agent = mCharacterSystem.FindByInstance(instance))
            {
                mCharacterSystem.ApplyPush(*agent, push.delta);
            }
            else
            {
                InstanceSettings s = instance->GetInstanceSettings();
                s.mWorldPosition += push.delta;
                instance->SetInstanceSettings(s);
            }
        }
    }
    mCollisionSystem.BuildDebugLines(scene.miDebugLines);
}

void Application::updateCharacterInput()
{
    PlayerInputBuffer& player = mCharacterSystem.PlayerInput();
    player.input = CharacterInput{};
    if (mCharacterSystem.Possessed() == kInvalidEntityId)
    {
        player.commands.clear();
        return;
    }
    const auto command = [&player](CommandType type, const char* montage = "")
    {
        IntentCommand c;
        c.type = type;
        c.montage = montage;
        player.commands.push_back(c);
    };
    // Free camera owns WASD; every other camera follows the selected instance, so the keys drive the
    // character. Text fields / hovered panels keep the keyboard (same guard as the cameras).
    const ImGuiIO& io = ImGui::GetIO();
    const bool keysFree = !io.WantCaptureKeyboard && !io.WantTextInput;
    const bool characterKeys = keysFree && mRenderer->GetRenderData().rdCameraRig.ActiveType() != CameraType::Free;
    if (characterKeys)
    {
        // W/S forward, A/D strafe, Q/E turn, Shift jog, Alt sprint, Ctrl crouch, V aim (+ Up/Down pitch),
        // Space jump, F punch, H hit, G toggle weapon, K die, U interact (P7).
        CharacterInput& input = player.input;
        input.moveY = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
        input.moveX = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
        input.turn = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
        input.run = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
        input.sprint = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
        input.crouch = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        input.aim = ImGui::IsKeyDown(ImGuiKey_V);
        input.aimPitch = (ImGui::IsKeyDown(ImGuiKey_UpArrow) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_DownArrow) ? 1.0f : 0.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
            command(CommandType::Jump);
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            command(CommandType::Action, "punch");
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
            command(CommandType::Action, "hit");
        if (ImGui::IsKeyPressed(ImGuiKey_G, false))
            command(CommandType::ToggleWeapon);
        if (ImGui::IsKeyPressed(ImGuiKey_K, false))
            command(CommandType::Die);
        if (ImGui::IsKeyPressed(ImGuiKey_U, false))
            command(CommandType::Interact);
    }
}

void Application::MainLoop()
{
    if (mCollisionBench)
    {
        runCollisionBench();
        return;
    }
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
                if (mBehaviorSmoke)
                {
                    verifyBehaviorSmoke();
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

        // Agents first: perception -> decisions -> steering -> state machines decide which graph / blend
        // inputs this frame evaluates. Edit mode pauses perception, decisions and world time (D17).
        if (mControllerScript)
        {
            checkControllerFrame(mFramesRendered);
        }
        updateAgents(deltaTime);
        if (mBehaviorSmoke)
        {
            observeBehaviorFrame(mFramesRendered);
        }

        // Advance every instance's animation graph (time, CPU seams, Work records) before the camera reads
        // poses and before the renderer packs the frame.
        mAnimationSystem.Update(deltaTime, mSceneEditor.ModelData());

        // Collision after the poses are final for this frame: bodies follow the joints, contacts push the
        // instance roots apart, debug lines are rebuilt. The camera and the renderer see the pushed positions.
        updateCollision(deltaTime);
        // Level statics (colliders, zones, markers) on top of the collision lines rebuilt above.
        if (mSceneEditor.ModelData().miShowLevelStatics)
        {
            for (const StaticLayer& layer : mSceneEditor.ModelData().miStaticLayers)
            {
                AppendLevelStaticsLines(layer.statics, mSceneEditor.ModelData().miDebugLines);
            }
        }

        // Drive the active camera from this frame's ImGui input + the selected instance, after the UI
        // frame is built (so IO is populated) and before Draw uploads the camera matrices.
        mRenderer->UpdateActiveCamera(deltaTime, mSceneEditor.ModelData());

        if (!mRenderer->Draw(deltaTime, mSceneEditor.ModelData()))
        {
            break;
        }
        // Checkpoint save / load requests run here, when every system is in a consistent state (§8.4).
        updateBehaviorFrameEnd();
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
        mCollisionSystem.Shutdown();
        mCharacterSystem.Clear();
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
