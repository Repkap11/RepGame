// Terrain work ordering: worker pops must be nearest-to-focus, not FIFO.
// A bulk reload (login, dimension hop, long teleport) enqueues the whole
// window in slot order — FIFO would generate the camera's surroundings
// last and leave the player standing in an unloaded region while edge
// chunks generate first.
#include "test_util.hpp"

#include "common/utils/terrain_loading_thread.hpp"

int test_terrain_queue( ) {
    int failures = 0;
    auto check = [ &failures ]( bool cond, const char *msg ) {
        if ( !cond ) {
            failures++;
            fprintf( stderr, "test_terrain_queue: %s\n", msg );
        }
    };

    const int rx = CHUNK_RADIUS_XZ * 2 + 1;
    const int ry = CHUNK_RADIUS_Y * 2 + 1;
    auto mod_ring = []( int v, int ring ) { return ( ( v % ring ) + ring ) % ring; };

    // A Chunk whose slot phase retargets it to `pos` near the focus —
    // chunk_mod is the only field the pick ordering reads.
    Chunk chunks[ 5 ];
    const glm::ivec3 targets[ 5 ] = {
        { 10, 0, 0 }, { -5, 0, 0 }, { 0, 3, 0 }, { 2, 0, 0 }, { 0, 0, 0 },
    };
    for ( int i = 0; i < 5; i++ ) {
        chunks[ i ].chunk_mod = glm::ivec3( mod_ring( targets[ i ].x, rx ),
                                            mod_ring( targets[ i ].y, ry ),
                                            mod_ring( targets[ i ].z, rx ) );
    }

    TerrainLoadingThread t;
    TerrainWorkItem item;

    // No focus yet: pops stay FIFO (the initial-load enqueue order from
    // ChunkLoader::init is already center-out).
    for ( int i = 0; i < 5; i++ ) {
        t.enqueue( &chunks[ i ], targets[ i ], 0 );
    }
    check( t.work_pop( &item ) && item.chunk == &chunks[ 0 ], "no focus: first pop not queue front" );

    // With a focus set, the rest of the queue drains near-to-far
    // regardless of enqueue order: (0,0,0), (2,0,0), (0,3,0), (-5,0,0).
    t.set_focus( glm::ivec3( 0, 0, 0 ), 8 );
    const int expected[ 4 ] = { 4, 3, 2, 1 };
    for ( int e = 0; e < 4; e++ ) {
        check( t.work_pop( &item ) && item.chunk == &chunks[ expected[ e ] ],
               "focus: pop order not nearest-first" );
    }
    check( !t.work_pop( &item ), "focus: pop on empty queue" );

    // Re-focusing mid-queue re-orders around the new center (a dimension
    // hop moves the focus hundreds of chunks): whatever the camera now
    // stands in pops first — order 0,3,4,2,1 around focus (10,0,0).
    for ( int i = 0; i < 5; i++ ) {
        t.enqueue( &chunks[ i ], targets[ i ], 0 );
    }
    t.set_focus( glm::ivec3( 10, 0, 0 ), 8 );
    const int expected2[ 5 ] = { 0, 3, 4, 2, 1 };
    for ( int e = 0; e < 5; e++ ) {
        check( t.work_pop( &item ) && item.chunk == &chunks[ expected2[ e ] ],
               "refocus: pop order not nearest to new focus" );
    }
    return failures;
}
