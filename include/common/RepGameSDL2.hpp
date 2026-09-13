#pragma once

#define SDL_MAIN_HANDLED
#if defined( REPGAME_WASM )
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#endif

int repgame_sdl2_main( const char *world_path, const char *host, bool connect_multi, bool tests );