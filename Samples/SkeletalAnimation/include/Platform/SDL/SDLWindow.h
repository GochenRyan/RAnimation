#pragma once

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
    SDL_Window* m_window = nullptr;
    bool m_shouldClose = false;
};