#include "terrain_loading_thread.h"
#include "RepGame.h"
#include <pthread.h>
#include <unistd.h>

volatile int cancleThread = 0;
pthread_t background_thread;

void *process_background_tasks( void *arg ) {
    while ( !cancleThread ) {
        pr_debug( "Working..." );
        sleep( 1 );
    }
    cancleThread = 0;
    return 0;
}

int terrain_loading_thread_start( ) {
    if ( pthread_create( &background_thread, NULL, &process_background_tasks, NULL ) ) {
        pr_debug( "Error creating background thread" );
        return -1;
    }
    return 0;
}
void terrain_loading_thread_enqueue( Chunk *chunk ) {
}
void terrain_loading_thread_stop( ) {
    cancleThread = 1;
}
