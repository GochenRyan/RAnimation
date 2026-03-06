// Platform/SDL/SDLPlatform.h
#pragma once

#include <Platform/Platform.h>

class SDLWindow;

class SDLPlatform final : public IPlatform
{
public:
    SDLPlatform() = default;
    ~SDLPlatform() override = default;

    bool Initialize() override;
    void Shutdown() override;

    std::unique_ptr<IWindow> CreateWindow(const WindowDesc& desc) override;

    void PumpEvents() override;
    double GetTimeSeconds() const override;

    InputState& GetInputState() override { return m_inputState; }
    const InputState& GetInputState() const override { return m_inputState; }

private:
    InputState m_inputState{};
    SDLWindow* m_mainWindow = nullptr;
    bool m_initialized = false;
};