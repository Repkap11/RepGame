#pragma once

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

int repgame_sdl2_main( const char *world_path, const char *host, bool connect_multi, bool tests );