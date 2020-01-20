#ifndef HEADER_REPGAMESDL2_H
#define HEADER_REPGAMESDL2_H

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

int repgame_sdl2_main( const char *world_path, const char *host, bool connect_multi, bool tests );

#endif