// Platform/SDL/SDLInputConverter.h
#pragma once

#include <Platform/InputTypes.h>

typedef int SDL_Scancode;
typedef int SDL_MouseButtonFlags;

KeyCode SDLToKeyCode(SDL_Scancode scancode);
MouseButton SDLToMouseButton(int sdlButton);