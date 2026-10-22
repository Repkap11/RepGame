#pragma once

#define SDL_MAIN_HANDLED
#if defined( REPGAME_WASM )
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#else
#include <SDL3/SDL.h>
#if !defined( REPGAME_GLES )
// SDL_opengl.h pulls in desktop GL headers, which clash with the GLES
// headers RepGameLinux.hpp includes on the arm build. GL types come from
// the platform header there.
#include <SDL3/SDL_opengl.h>
#endif
#endif

int repgame_sdl2_main( const char *world_path, const char *host, bool connect_multi, bool tests );