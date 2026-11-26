// Platform/SDL/SDLInputConverter.h
#pragma once

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <Platform/InputTypes.h>

KeyCode SDLToKeyCode(SDL_Scancode scancode);
MouseButton SDLToMouseButton(int sdlButton);
