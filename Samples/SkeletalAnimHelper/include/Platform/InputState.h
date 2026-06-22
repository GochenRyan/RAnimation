#pragma once

#include <array>
#include <Platform/InputTypes.h>

class InputState
{
public:
    void BeginFrame()
    {
        m_prevKeys = m_keys;
        m_prevMouseButtons = m_mouseButtons;
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
        m_mouseWheelX = 0.0f;
        m_mouseWheelY = 0.0f;
    }

    bool IsKeyDown(KeyCode key) const { return m_keys[static_cast<size_t>(key)]; }

    bool IsKeyPressed(KeyCode key) const
    {
        const size_t i = static_cast<size_t>(key);
        return m_keys[i] && !m_prevKeys[i];
    }

    bool IsKeyReleased(KeyCode key) const
    {
        const size_t i = static_cast<size_t>(key);
        return !m_keys[i] && m_prevKeys[i];
    }

    void SetKey(KeyCode key, bool down) { m_keys[static_cast<size_t>(key)] = down; }

    void SetMouseButton(MouseButton button, bool down) { m_mouseButtons[static_cast<size_t>(button)] = down; }

    void SetMousePosition(int x, int y)
    {
        m_mouseX = x;
        m_mouseY = y;
    }

    void AddMouseDelta(int dx, int dy)
    {
        m_mouseDeltaX += dx;
        m_mouseDeltaY += dy;
    }

    void AddMouseWheel(float x, float y)
    {
        m_mouseWheelX += x;
        m_mouseWheelY += y;
    }

public:
    static constexpr size_t KeyCount = 128;
    static constexpr size_t MouseButtonCount = 5;

private:
    std::array<bool, KeyCount> m_keys{};
    std::array<bool, KeyCount> m_prevKeys{};

    std::array<bool, MouseButtonCount> m_mouseButtons{};
    std::array<bool, MouseButtonCount> m_prevMouseButtons{};

    int m_mouseX = 0;
    int m_mouseY = 0;
    int m_mouseDeltaX = 0;
    int m_mouseDeltaY = 0;
    float m_mouseWheelX = 0.0f;
    float m_mouseWheelY = 0.0f;
};