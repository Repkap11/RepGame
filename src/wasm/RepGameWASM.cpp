#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include <emscripten.h>
#include <emscripten/wasmfs.h>
#include <emscripten/threading.h>

#include "common/RepGame.hpp"
#include "common/utils/file_utils.hpp"

// wasmfs_create_opfs_backend() spawns a dedicated ProxyWorker and must not run
// on the browser main thread (it blocks until the worker is ready, which
// deadlocks the page). Do the OPFS mount on a helper pthread, then notify JS
// so it can start the game. Falls back to a plain in-memory dir if OPFS is
// unavailable so the game still runs without persistence.
static void *storage_init_thread( void *arg ) {
    backend_t backend = wasmfs_create_opfs_backend( );
    int err = -1;
    if ( backend ) {
        err = wasmfs_create_directory( "/repgame_wasm", 0777, backend );
    }
    if ( !backend || err ) {
        mkdir_p( "/repgame_wasm" );
    }
    MAIN_THREAD_ASYNC_EM_ASM( { repgameStorageReady( $0 ); }, backend && !err );
    return NULL;
}

// Called from JS (index.js) when the user clicks play, before main() runs.
// Mounts /repgame_wasm on OPFS asynchronously; JS waits for
// repgameStorageReady() before calling main() so world files are in place.
extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_start_storage( ) {
    pthread_t thread;
    pthread_create( &thread, NULL, storage_init_thread, NULL );
    pthread_detach( thread );
}

// Set by JS (index.js) before main() runs: the ws:// or wss:// URL of the
// multiplayer WebSocket bridge. Empty string = single-player.
static char g_server_url[ 512 ] = { 0 };

extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_set_server( const char *url ) {
    snprintf( g_server_url, sizeof( g_server_url ), "%s", url ? url : "" );
}

extern "C" int main( int argc, char **argv ) {
    pr_debug( "Entering RepGameWASM Paul Main:%d", argc );
    const char *world_path = "repgame_wasm/World1";
    const char *server = g_server_url[ 0 ] ? g_server_url : NULL;

    return repgame_sdl2_main( world_path, server, server != NULL, false );
}
