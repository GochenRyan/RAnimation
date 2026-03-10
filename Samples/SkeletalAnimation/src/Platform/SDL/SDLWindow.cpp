#include <SDL3/SDL.h>

#include <Platform/SDL/SDLWindow.h>

SDLWindow::SDLWindow(SDL_Window* window) : m_window(window)
{
}

SDLWindow::~SDLWindow()
{
#if defined(__APPLE__)
    if (m_metalView)
    {
        SDL_Metal_DestroyView(m_metalView);
        m_metalView = nullptr;
    }
#endif

    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

int SDLWindow::GetWidth() const
{
    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    return w;
}

int SDLWindow::GetHeight() const
{
    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    return h;
}

bool SDLWindow::ShouldClose() const
{
    return m_shouldClose;
}

void SDLWindow::SetShouldClose(bool value)
{
    m_shouldClose = value;
}

void SDLWindow::SetTitle(const char* title)
{
    SDL_SetWindowTitle(m_window, title);
}

#if defined(__APPLE__)
bool SDLWindow::EnsureMetalView() const
{
    if (!m_window)
        return false;

    if (!m_metalView)
        m_metalView = SDL_Metal_CreateView(m_window);

    return m_metalView != nullptr;
}
#endif

NativeWindowHandle SDLWindow::GetNativeHandle() const
{
    NativeWindowHandle handle{};

    if (!m_window)
        return handle;

    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    if (!props)
        return handle;

#if defined(_WIN32)

    handle.backend = NativeWindowBackend::Win32;
    handle.win32.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    return handle;

#elif defined(__APPLE__)

    // On the Apple platform, the rendering layer prioritizes returning Metal's CAMetalLayer*
    // SDL_GetWindowProperties can only directly obtain NSWindow*，
    // a genuine CAMetalLayer* requires SDL_Metal_CreateView + SDL_Metal_GetLayer
    if (EnsureMetalView())
    {
        void* metalLayer = SDL_Metal_GetLayer(m_metalView);
        if (metalLayer)
        {
            handle.backend = NativeWindowBackend::Metal;
            handle.metal.caMetalLayer = metalLayer;
            return handle;
        }
    }

    // Roll back to the NSWindow* of Cocoa
    handle.backend = NativeWindowBackend::Cocoa;
    handle.cocoa.nsWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    return handle;

#elif defined(__linux__)

    // Priority will be given to Wayland. If you fail to get it, it will be returned to X11
    if (void* wlDisplay = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr))
    {
        handle.backend = NativeWindowBackend::Wayland;
        handle.wayland.display = wlDisplay;
        handle.wayland.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        return handle;
    }

    if (void* x11Display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr))
    {
        handle.backend = NativeWindowBackend::X11;
        handle.x11.display = x11Display;
        handle.x11.window =
            static_cast<std::uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        return handle;
    }

    return handle;

#else

    return handle;

#endif
}