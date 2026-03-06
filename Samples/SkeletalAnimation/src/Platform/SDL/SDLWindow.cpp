#include <SDL3/SDL.h>

#include <Platform/SDL/SDLWindow.h>

SDLWindow::SDLWindow(SDL_Window* window) : m_window(window)
{
}

SDLWindow::~SDLWindow()
{
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

NativeWindowHandle SDLWindow::GetNativeHandle() const
{
    NativeWindowHandle handle{};

    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    if (!props)
        return handle;

    if (void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
    {
        handle.backend = NativeWindowBackend::Win32;
        handle.win32.hwnd = hwnd;
        return handle;
    }

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

    if (void* cocoaWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr))
    {
        handle.backend = NativeWindowBackend::Cocoa;
        handle.cocoa.nsWindow = cocoaWindow;
        return handle;
    }

    return handle;
}