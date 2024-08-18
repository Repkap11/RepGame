#include <unistd.h>

#include "common/utils/terrain_loading_thread.hpp"
#include "common/chunk.hpp"
#include "common/utils/linked_list.hpp"

void TerrainLoadingThread::process_value( LinkedListValue *value ) {
    Chunk &chunk = *value->chunk;
    chunk.persist( this->map_storage );
    chunk.chunk_pos = value->new_chunk_pos;
    chunk.load_terrain( this->map_storage );
    // pr_debug( "Paul Loading terrain x:%d y%d: z:%d work:%d results:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z, work_linked_list->count, result_linked_list->count );
}

#if defined( REPGAME_WASM )
void TerrainLoadingThread::enqueue( Chunk *chunk, const glm::ivec3 &new_chunk_pos, int persist ) {
    LinkedListValue value;
    value.valid = 1;
    value.chunk = chunk;
    value.new_chunk_pos = new_chunk_pos;
    value.persist = persist;
    if ( value.chunk ) {
        linked_list_add_element( result_linked_list, value );
    }
}

Chunk *TerrainLoadingThread::dequeue( ) {
    LinkedListValue value = linked_list_pop_element( result_linked_list );
    if ( value.valid ) {
        process_value( &value );
        return value.chunk;
    } else {
        return NULL;
    }
}
int TerrainLoadingThread::start( MapStorage &map_storage ) {
    result_linked_list = linked_list_create( );

    return 0;
}

void TerrainLoadingThread::stop( ) {
}

#else // not REPGAME_WASM

#include <pthread.h>

volatile int cancelThread = 0;
pthread_t background_threads[ NUM_RENDER_THREADS ];

void *TerrainLoadingThread::process_background_tasks( void *arg ) {
    TerrainLoadingThread &self = *( TerrainLoadingThread * )arg;
    while ( !cancelThread ) {
        // pr_debug( "Working..." );
        // sleep( 1 );
        // pr_debug( "Working2... Work Size: %d", work_linked_list->count );
        LinkedListValue value = linked_list_pop_element( self.work_linked_list );
        if ( value.valid ) {
            self.process_value( &value );
            if ( value.chunk ) {
                linked_list_add_element( self.result_linked_list, value );
            }
        } else {
            usleep( 100000 );
        }
    }
    return 0;
}

int TerrainLoadingThread::start( MapStorage &map_storage ) {
    this->map_storage = map_storage;
    work_linked_list = linked_list_create( );
    result_linked_list = linked_list_create( );
    for ( int i = 0; i < NUM_RENDER_THREADS; i++ ) {
        if ( pthread_create( &background_threads[ i ], NULL, &TerrainLoadingThread::process_background_tasks, this ) ) {
            pr_debug( "Error creating background thread %d", i );
            return -1;
        }
    }
    return 0;
}

void TerrainLoadingThread::enqueue( Chunk *chunk, const glm::ivec3 &new_chunk_pos, int persist ) {
    LinkedListValue value;
    value.chunk = chunk;
    value.new_chunk_pos = new_chunk_pos;
    value.persist = persist;
    value.valid = 0;
    linked_list_add_element( work_linked_list, value );
    // pr_debug( "Paul enqueued x:%d y%d: z:%d work:%d h:%p t:%p", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z, work_linked_list->count, work_linked_list->head, work_linked_list->tail );
}

void TerrainLoadingThread::stop( ) {
    cancelThread = 1;
    for ( int i = 0; i < NUM_RENDER_THREADS; i++ ) {
        pthread_join( background_threads[ i ], NULL );
    }
    cancelThread = 0;
    linked_list_free( work_linked_list );
    linked_list_free( result_linked_list );
}

Chunk *TerrainLoadingThread::dequeue( ) {
    LinkedListValue value = linked_list_pop_element( result_linked_list );
    if ( value.valid ) {
        return value.chunk;
    } else {
        return NULL;
    }
}

#endif // else REPGAME_WASM
