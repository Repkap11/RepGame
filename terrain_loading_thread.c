#include "terrain_loading_thread.h"
#include "Chunk.h"
#include "RepGame.h"
#include "linked_list.h"
#include <pthread.h>
#include <unistd.h>

volatile int cancelThread = 0;
pthread_t background_thread;
LinkedList *work_linked_list;
LinkedList *result_linked_list;

void *process_background_tasks( void *arg ) {
    while ( !cancelThread ) {
        pr_debug( "Working..." );
        Chunk *chunk;
        int poped = 1;
        do {
            poped = linked_list_pop_element( work_linked_list, &chunk );
            if ( poped ) {
                chunk_load_terrain( chunk );
                linked_list_add_element( work_linked_list, &chunk );
            }
        } while ( poped );
        sleep( 1 );
    }
    return 0;
}

int terrain_loading_thread_start( ) {
    work_linked_list = linked_list_create( );
    result_linked_list = linked_list_create( );
    if ( pthread_create( &background_thread, NULL, &process_background_tasks, NULL ) ) {
        pr_debug( "Error creating background thread" );
        return -1;
    }
    return 0;
}
void terrain_loading_thread_enqueue( Chunk *chunk ) {
    linked_list_add_element( work_linked_list, &chunk );
}
int terrain_loading_thread_dequeue( Chunk *chunk ) {
    return linked_list_pop_element( result_linked_list, &chunk );
}
void terrain_loading_thread_stop( ) {
    cancelThread = 1;
    pthread_join( background_thread, NULL );
    cancelThread = 0;
    linked_list_free( work_linked_list );
    linked_list_free( result_linked_list );
}
