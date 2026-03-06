#include <SDL3/SDL.h>

#include <Platform/SDL/SDLInputConverter.h>

KeyCode SDLToKeyCode(SDL_Scancode scancode)
{
    switch (scancode)
    {
        case SDL_SCANCODE_A:
            return KeyCode::A;
        case SDL_SCANCODE_B:
            return KeyCode::B;
        case SDL_SCANCODE_C:
            return KeyCode::C;
        case SDL_SCANCODE_D:
            return KeyCode::D;
        case SDL_SCANCODE_W:
            return KeyCode::W;
        case SDL_SCANCODE_S:
            return KeyCode::S;
        case SDL_SCANCODE_ESCAPE:
            return KeyCode::Escape;
        case SDL_SCANCODE_SPACE:
            return KeyCode::Space;
        case SDL_SCANCODE_RETURN:
            return KeyCode::Enter;
        case SDL_SCANCODE_UP:
            return KeyCode::Up;
        case SDL_SCANCODE_DOWN:
            return KeyCode::Down;
        case SDL_SCANCODE_LEFT:
            return KeyCode::Left;
        case SDL_SCANCODE_RIGHT:
            return KeyCode::Right;
        default:
            return KeyCode::Unknown;
    }
}

MouseButton SDLToMouseButton(int sdlButton)
{
    switch (sdlButton)
    {
        case SDL_BUTTON_LEFT:
            return MouseButton::Left;
        case SDL_BUTTON_RIGHT:
            return MouseButton::Right;
        case SDL_BUTTON_MIDDLE:
            return MouseButton::Middle;
        case SDL_BUTTON_X1:
            return MouseButton::X1;
        case SDL_BUTTON_X2:
            return MouseButton::X2;
        default:
            return MouseButton::Left;
    }
}