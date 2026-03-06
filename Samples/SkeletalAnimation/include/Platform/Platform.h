#pragma once

#include <memory>
#include <Platform/Window.h>
#include <Platform/InputState.h>

class IPlatform
{
public:
    virtual ~IPlatform() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual std::unique_ptr<IWindow> CreateWindow(const WindowDesc& desc) = 0;

    virtual void PumpEvents() = 0;
    virtual double GetTimeSeconds() const = 0;

    virtual InputState& GetInputState() = 0;
    virtual const InputState& GetInputState() const = 0;
};