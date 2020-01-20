#include <string.h>
#include <stdio.h>
#include "common/RepGame.hpp"

extern "C" int main( int argc, char **argv ) {
    pr_debug( "Entering RepGameWASM Paul Main:%d", argc );
    const char *world_path = "repgame_wasm/World1";

    return repgame_sdl2_main( world_path, NULL, false, false );
}
