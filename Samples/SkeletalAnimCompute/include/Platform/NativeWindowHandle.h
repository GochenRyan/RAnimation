#pragma once

#include <cstdint>

enum class NativeWindowBackend
{
    Unknown,
    Win32,
    Wayland,
    X11,
    Cocoa,
    Metal
};

struct NativeWindowHandle
{
    NativeWindowBackend backend = NativeWindowBackend::Unknown;

    union
    {
        struct
        {
            void* hwnd;
        } win32;

        struct
        {
            void* display;
            void* surface;
        } wayland;

        struct
        {
            void* display;
            std::uint64_t window;
        } x11;

        struct
        {
            void* nsWindow;
        } cocoa;

        struct
        {
            void* caMetalLayer;
        } metal;
    };

    NativeWindowHandle()
    {
        win32.hwnd = nullptr;
    }
};