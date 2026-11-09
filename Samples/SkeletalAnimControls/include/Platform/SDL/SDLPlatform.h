// Platform/SDL/SDLPlatform.h
#pragma once

#include <memory>
#include <Platform/Platform.h>

class SDLWindow;

class SDLPlatform final : public IPlatform
{
public:
    SDLPlatform() = default;
    ~SDLPlatform() override = default;

    bool Initialize() override;
    void Shutdown() override;

    IWindow* CreateWindow(const WindowDesc& desc) override;
    IWindow* GetMainWindow() const override { return m_mainWindow.get(); }

    void PumpEvents() override;
    double GetTimeSeconds() const override;

    InputState& GetInputState() override { return m_inputState; }
    const InputState& GetInputState() const override { return m_inputState; }

private:
    InputState m_inputState{};
    std::unique_ptr<IWindow> m_mainWindow = nullptr;
    bool m_initialized = false;
};