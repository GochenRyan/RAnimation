#pragma once
#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

#include <Platform/Window.h>

struct SDL_Window;

class SDLWindow final : public IWindow
{
public:
    explicit SDLWindow(SDL_Window* window);
    ~SDLWindow() override;

    SDLWindow(const SDLWindow&) = delete;
    SDLWindow& operator=(const SDLWindow&) = delete;

    int GetWidth() const override;
    int GetHeight() const override;
    bool ShouldClose() const override;

    void SetShouldClose(bool value) override;
    void SetTitle(const char* title) override;

    NativeWindowHandle GetNativeHandle() const override;

    SDL_Window* GetSDLHandle() const { return m_window; }

private:
#if defined(__APPLE__)
    bool EnsureMetalView() const;
#endif

private:
    SDL_Window* m_window = nullptr;
    bool m_shouldClose = false;

#if defined(__APPLE__)
    mutable SDL_MetalView m_metalView = nullptr;
#endif
};