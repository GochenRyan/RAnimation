#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <Platform/SDL/SDLPlatform.h>
#include <Platform/SDL/SDLWindow.h>
#include <Platform/SDL/SDLInputConverter.h>

bool SDLPlatform::Initialize()
{
    if (m_initialized)
        return true;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    m_initialized = true;
    return true;
}

void SDLPlatform::Shutdown()
{
    if (!m_initialized)
        return;

    m_mainWindow = nullptr;
    SDL_Quit();
    m_initialized = false;
}

IWindow* SDLPlatform::CreateWindow(const WindowDesc& desc)
{
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    SDL_WindowFlags flags = 0;

    if (desc.resizable)
        flags |= SDL_WINDOW_RESIZABLE;

    if (desc.highDPI)
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (desc.maximized)
        flags |= SDL_WINDOW_MAXIMIZED;

    SDL_Window* window = SDL_CreateWindow(
        desc.title.c_str(),
        desc.width,
        desc.height,
        flags
    );

    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return nullptr;
    }

    m_mainWindow = std::make_unique<SDLWindow>(window);
    return m_mainWindow.get();
}

void SDLPlatform::PumpEvents()
{
    m_inputState.BeginFrame();

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui_ImplSDL3_ProcessEvent(&e);
        }

        switch (e.type)
        {
        case SDL_EVENT_QUIT:
        {
            if (m_mainWindow)
                m_mainWindow->SetShouldClose(true);
            break;
        }

        case SDL_EVENT_KEY_DOWN:
        {
            KeyCode key = SDLToKeyCode(e.key.scancode);
            if (key != KeyCode::Unknown)
                m_inputState.SetKey(key, true);
            break;
        }

        case SDL_EVENT_KEY_UP:
        {
            KeyCode key = SDLToKeyCode(e.key.scancode);
            if (key != KeyCode::Unknown)
                m_inputState.SetKey(key, false);
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            m_inputState.SetMouseButton(SDLToMouseButton(e.button.button), true);
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            m_inputState.SetMouseButton(SDLToMouseButton(e.button.button), false);
            break;
        }

        case SDL_EVENT_MOUSE_MOTION:
        {
            m_inputState.SetMousePosition(static_cast<int>(e.motion.x), static_cast<int>(e.motion.y));
            m_inputState.AddMouseDelta(static_cast<int>(e.motion.xrel), static_cast<int>(e.motion.yrel));
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
        {
            m_inputState.AddMouseWheel(e.wheel.x, e.wheel.y);
            break;
        }

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        {
            if (m_mainWindow)
                m_mainWindow->SetShouldClose(true);
            break;
        }

        default:
            break;
        }
    }
}

double SDLPlatform::GetTimeSeconds() const
{
    static const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    const Uint64 counter = SDL_GetPerformanceCounter();
    return static_cast<double>(counter) / freq;
}
