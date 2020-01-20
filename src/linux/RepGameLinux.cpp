#include <string.h>
#include <stdio.h>
#include "common/RepGame.hpp"

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
        pr_debug( "\nusage: %s world_path\n\tWhere world_path is relitive to the executable.", argv[ 0 ] );
        return -1;
    }
    return repgame_sdl2_main( world_path, host, connect_multi, tests );
}