#include <string.h>
#include <stdio.h>
#include "common/RepGame.hpp"

int main( int argc, char **argv ) {
    // Argument handling mirrors src/linux/RepGameLinux.cpp.
    const char *world_path;
    const char *host;
    bool connect_multi = true;
    if ( argc >= 2 && strcmp( argv[ 1 ], "--tests" ) == 0 ) {
        // `RepGame.exe --tests [filter ...]` — same as the Linux client.
        return repgame_sdl2_main( argv[ 1 ], NULL, false, argc - 2, argv + 2 );
    } else if ( argc == 1 ) {
        // No-arg launch joins the public server on World1.
        world_path = "World1";
        host = "www.repkap11.com";
    } else if ( argc == 2 ) {
        // A world alone is single-player.
        world_path = argv[ 1 ];
        connect_multi = false;
        host = "";
    } else if ( argc == 3 ) {
        world_path = argv[ 1 ];
        host = argv[ 2 ];
    } else {
        pr_debug( "\nusage: %s world_path [host]\n\tWhere world_path is relative to the current directory.", argv[ 0 ] );
        return -1;
    }

    return repgame_sdl2_main( world_path, host, connect_multi, 0, nullptr );
}