// Platform/Window.h
#pragma once

#include <string>
#include <Platform/NativeWindowHandle.h>

struct WindowDesc
{
    std::string title = "Engine";
    int width = 1280;
    int height = 720;

    bool resizable = true;
    bool highDPI = true;
    bool maximized = false;
};

class IWindow
{
public:
    virtual ~IWindow() = default;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual bool ShouldClose() const = 0;

    virtual void SetShouldClose(bool value) = 0;
    virtual void SetTitle(const char* title) = 0;

    virtual NativeWindowHandle GetNativeHandle() const = 0;
};