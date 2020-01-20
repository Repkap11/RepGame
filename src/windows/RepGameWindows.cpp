#include <string.h>
#include <stdio.h>
#include "common/RepGame.hpp"

int main( int argc, char **argv ) {
    const char *world_path;
    if ( argc == 1 ) {
        world_path = "World1";
    } else if ( argc == 2 ) {
        world_path = argv[ 1 ];
    } else {
        pr_debug( "\nusage: %s world_path\n\tWhere world_path is relitive to the current directory.", argv[ 0 ] );
        return -1;
    }

    return repgame_sdl2_main( world_path, NULL, false, false );
}