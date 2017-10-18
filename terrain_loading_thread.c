#include "terrain_loading_thread.h"
#include "RepGame.h"
#include "chunk.h"
#include "linked_list.h"
#include <pthread.h>
#include <unistd.h>

volatile int cancelThread = 0;
#define NUM_RENDER_THREADS 7
pthread_t background_threads[ NUM_RENDER_THREADS ];
LinkedList *work_linked_list;
LinkedList *result_linked_list;

void *process_background_tasks( void *arg ) {
    while ( !cancelThread ) {
        // pr_debug( "Working..." );
        // sleep( 1 );
        // pr_debug( "Working2... Work Size: %d", work_linked_list->count );

        Chunk *chunk;
        int poped = 1;
        chunk = linked_list_pop_element( work_linked_list );
        if ( chunk ) {
            chunk_load_terrain( chunk );
            linked_list_add_element( result_linked_list, chunk );
            // pr_debug( "Paul Loading terrain x:%d y%d: z:%d work:%d results:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z, work_linked_list->count, result_linked_list->count );
        } else {
            usleep( 1000 );
        }
    }
    return 0;
}

int terrain_loading_thread_start( ) {
    work_linked_list = linked_list_create( );
    result_linked_list = linked_list_create( );
    for ( int i = 0; i < NUM_RENDER_THREADS; i++ ) {
        if ( pthread_create( &background_threads[ i ], NULL, &process_background_tasks, NULL ) ) {
            pr_debug( "Error creating background thread %d", i );
            return -1;
        }
    }

    return 0;
}
void terrain_loading_thread_enqueue( Chunk *chunk ) {
    linked_list_add_element( work_linked_list, chunk );
    // pr_debug( "Paul enqueued x:%d y%d: z:%d work:%d h:%p t:%p", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z, work_linked_list->count, work_linked_list->head, work_linked_list->tail );
}
Chunk *terrain_loading_thread_dequeue( ) {
    Chunk *chunk = linked_list_pop_element( result_linked_list );
    // if ( chunk ) {
    // pr_debug( "Paul dequeued x:%d y%d: z:%d results:%d h:%p t:%p", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z, result_linked_list->count, result_linked_list->head, result_linked_list->tail );
    // }
    return chunk;
}
void terrain_loading_thread_stop( ) {
    cancelThread = 1;
    for ( int i = 0; i < NUM_RENDER_THREADS; i++ ) {
        pthread_join( background_threads[ i ], NULL );
    }
    cancelThread = 0;
    linked_list_free( work_linked_list );
    linked_list_free( result_linked_list );
}
