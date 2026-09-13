#include <string.h>
#include <stdio.h>
#include "common/RepGame.hpp"
#include <signal.h>

int main( int argc, char **argv ) {
    const char *world_path;
    const char *host;
    bool connect_multi = true;
    bool tests = false;
    if ( argc == 1 ) {
        world_path = "World1";
        connect_multi = false;
        host = "";
    } else if ( argc == 2 ) {
        if ( strcmp( argv[ 1 ], "tests" ) == 0 ) {
            tests = true;
        }
        world_path = argv[ 1 ];
        connect_multi = false;
        host = "";
    } else if ( argc == 3 ) {
        world_path = argv[ 1 ];
        host = argv[ 2 ];
    } else {
        pr_debug( "\nusage: %s world_path\n\tWhere world_path is relative to the executable.", argv[ 0 ] );
        return -1;
    }

    struct sigaction action;
    sigaction( SIGINT, nullptr, &action );
    // Prefer native Wayland over Xwayland when available, but fall back to
    // X11. Must be set before the first SDL_Init below — SDL_Init is
    // idempotent, so the video driver chosen here can't be changed later.
    // Respect a non-empty SDL_VIDEODRIVER env var if the user sets one;
    // an empty value (e.g. exported as "") is treated as unset.
    const char *sdl_driver_env = getenv( "SDL_VIDEODRIVER" );
    if ( !sdl_driver_env || sdl_driver_env[ 0 ] == '\0' ) {
        SDL_SetHintWithPriority( SDL_HINT_VIDEO_DRIVER, "wayland,x11", SDL_HINT_OVERRIDE );
    }
    SDL_Init( SDL_INIT_VIDEO );
    sigaction( SIGINT, &action, nullptr );
    return repgame_sdl2_main( world_path, host, connect_multi, tests );
}