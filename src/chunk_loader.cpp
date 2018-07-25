#include "chunk_loader.h"
#include "RepGame.h"
#include "map_storage.h"
#include "terrain_loading_thread.h"
#include "constants.h"
#include <math.h>
#include <stdlib.h>

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

typedef struct {
    float x;
    float y;
    float z;
    unsigned int tex_coord_x;
    unsigned int tex_coord_y;
    unsigned int which_face;
} CubeFace;

#define FACE_TOP 0
#define FACE_BOTTOM 2
#define FACE_FRONT 1
#define FACE_BACK 1
#define FACE_LECT 1
#define FACE_RIGHT 1

CubeFace vd_data_solid[] = {
    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_FRONT}, // 0
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_FRONT}, // 1
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_FRONT}, // 2
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_FRONT}, // 3

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_BACK}, // 4
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_BACK}, // 5
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_BACK}, // 6
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_BACK}, // 7

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_LECT},  // 8
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT}, // 9
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT}, // 10
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_LECT},  // 11

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_LECT},  // 12
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT}, // 13
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT}, // 14
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_LECT},  // 15

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, !0, FACE_BOTTOM}, // 16
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, !0, FACE_BOTTOM}, // 17
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, !1, FACE_TOP},    // 18
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, !1, FACE_TOP},    // 19

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM}, // 20
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM}, // 21
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP},    // 22
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP},    // 23
};
int vd_data_size_solid = 4 * 6;

float water_height = 0.875f;
CubeFace vd_data_water[] = {
    {1.0f, water_height, 0.0f, /*Coords  Texture coords*/ !1, !1, FACE_TOP}, // 18 0
    {0.0f, water_height, 0.0f, /*Coords  Texture coords*/ !0, !1, FACE_TOP}, // 19 1

    {1.0f, water_height, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP}, // 22 2
    {0.0f, water_height, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP}, // 23 3
};
int vd_data_size_water = 2 * 2;

void chunk_loader_init( LoadedChunks *loadedChunks ) {
    map_storage_init( );
    int status = terrain_loading_thread_start( );
    if ( status ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    loadedChunks->chunkArray = ( Chunk * )calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) );
    shader_init( &loadedChunks->shader );
    texture_init_blocks( &loadedChunks->blocksTexture );
    unsigned int textureSlot = 0;
    texture_bind( &loadedChunks->blocksTexture, textureSlot );
    block_definitions_initilize_definitions( &loadedChunks->blocksTexture );
    {
        vertex_buffer_init( &loadedChunks->solid.vb_block );
        vertex_buffer_set_data( &loadedChunks->solid.vb_block, vd_data_solid, sizeof( CubeFace ) * vd_data_size_solid );
    }
    {
        vertex_buffer_init( &loadedChunks->water.vb_block );
        vertex_buffer_set_data( &loadedChunks->water.vb_block, vd_data_water, sizeof( CubeFace ) * vd_data_size_water );
    }
    vertex_buffer_layout_init( &loadedChunks->vbl_block );
    vertex_buffer_layout_bind( &loadedChunks->vbl_block );
    // The sum of these must be elements_per_vertex
    vertex_buffer_layout_push_float( &loadedChunks->vbl_block, 3 ); // Coords
    vertex_buffer_layout_push_float( &loadedChunks->vbl_block, 2 ); // Texture coords
    vertex_buffer_layout_push_float( &loadedChunks->vbl_block, 1 ); // Face type (top, sides, bottom)

    VertexBuffer *vb_block_solid = &loadedChunks->solid.vb_block;
    VertexBuffer *vb_block_water = &loadedChunks->water.vb_block;
    VertexBufferLayout *vbl_block = &loadedChunks->vbl_block;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        chunk_init( &loadedChunks->chunkArray[ i ], vb_block_solid, vb_block_water, vbl_block );
    }

    int nextChunk = 0;
    for ( int i = -CHUNK_RADIUS_X; i <= CHUNK_RADIUS_X; i++ ) {
        int new_i = ( i + CHUNK_RADIUS_X );
        new_i = ( ( new_i * ( new_i % 2 ? 1 : -1 ) ) + ( ( new_i % 2 ) ? 1 : 0 ) ) / 2;
        // pr_debug( "i:%d new_i:%d", i, new_i );
        for ( int j = -CHUNK_RADIUS_Y; j <= CHUNK_RADIUS_Y; j++ ) {
            int new_j = ( j + CHUNK_RADIUS_Y );
            new_j = ( ( new_j * ( new_j % 2 ? 1 : -1 ) ) + ( ( new_j % 2 ) ? 1 : 0 ) ) / 2;
            for ( int k = -CHUNK_RADIUS_Z; k <= CHUNK_RADIUS_Z; k++ ) {
                int new_k = ( k + CHUNK_RADIUS_Z );
                new_k = ( ( new_k * ( new_k % 2 ? 1 : -1 ) ) + ( ( new_k % 2 ) ? 1 : 0 ) ) / 2;
                Chunk *chunk = &loadedChunks->chunkArray[ nextChunk ];
                chunk->chunk_x = new_i;
                chunk->chunk_y = new_j;
                chunk->chunk_z = new_k;
                chunk->chunk_mod_x = new_i < 0 ? -CHUNK_RADIUS_X - new_i - 1 : CHUNK_RADIUS_X - new_i;
                chunk->chunk_mod_y = new_j < 0 ? -CHUNK_RADIUS_Y - new_j - 1 : CHUNK_RADIUS_Y - new_j;
                chunk->chunk_mod_z = new_k < 0 ? -CHUNK_RADIUS_Z - new_k - 1 : CHUNK_RADIUS_Z - new_k;
                // pr_debug( "Initing chunk %d mod_y:%d", nextChunk, chunk->chunk_mod_y );

                chunk->is_loading = 1;
                terrain_loading_thread_enqueue( chunk, TRIP_ARGS( chunk->chunk_ ), 0 );
                nextChunk = ( nextChunk + 1 );
            }
        }
    }
}

// int chunk_loader_is_chunk_loaded( LoadedChunks *loadedChunks, int chunk_x, int chunk_y, int chunk_z ) {
//     for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
//         Chunk *loadedChunk = &loadedChunks->chunkArray[ i ];
//         if ( loadedChunk->loaded ) {
//             if ( loadedChunk->chunk_x == chunk_x && loadedChunk->chunk_y == chunk_y && loadedChunk->chunk_z == chunk_z ) {
//                 return 1;
//             }
//         }
//     }
//     return 0;
// }

// void chunk_loader_load_chunk( LoadedChunks *loadedChunks, int index, int chunk_x, int chunk_y, int chunk_z ) {
//     Chunk *loadedChunk = &loadedChunks->chunkArray[ index ];
//     // if ( loadedChunk->loaded ) {
// }

Chunk *chunk_loader_get_chunk( LoadedChunks *loadedChunks, TRIP_ARGS( int chunk_ ) ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk *chunk = &loadedChunks->chunkArray[ i ];
        if ( chunk->chunk_x == chunk_x && chunk->chunk_y == chunk_y && chunk->chunk_z == chunk_z ) {
            return chunk;
        }
    }
    return NULL;
}

int reload_if_out_of_bounds( Chunk *chunk, TRIP_ARGS( int chunk_ ) ) {
    int new_chunk_x = ( int )floorf( ( ( float )( chunk_x + chunk->chunk_mod_x ) ) / ( ( float )( CHUNK_RADIUS_X * 2 + 1 ) ) ) * ( CHUNK_RADIUS_X * 2 + 1 ) - chunk->chunk_mod_x + CHUNK_RADIUS_X;
    int new_chunk_y = ( int )floorf( ( ( float )( chunk_y + chunk->chunk_mod_y ) ) / ( ( float )( CHUNK_RADIUS_Y * 2 + 1 ) ) ) * ( CHUNK_RADIUS_Y * 2 + 1 ) - chunk->chunk_mod_y + CHUNK_RADIUS_Y;
    int new_chunk_z = ( int )floorf( ( ( float )( chunk_z + chunk->chunk_mod_z ) ) / ( ( float )( CHUNK_RADIUS_Z * 2 + 1 ) ) ) * ( CHUNK_RADIUS_Z * 2 + 1 ) - chunk->chunk_mod_z + CHUNK_RADIUS_Z;
    int changed = new_chunk_x != chunk->chunk_x || new_chunk_y != chunk->chunk_y || new_chunk_z != chunk->chunk_z;

    if ( changed ) {
        chunk_unprogram_terrain( chunk );
        chunk->is_loading = 1;
        terrain_loading_thread_enqueue( chunk, TRIP_ARGS( new_chunk_ ), 1 );
    }
    return changed;
}

inline int process_chunk_position( Chunk *chunk, TRIP_ARGS( int chunk_diff_ ), TRIP_ARGS( int center_previous_ ), TRIP_ARGS( int center_next_ ), int force_reload ) {

    int visiable_changed_x = 0;
    int visiable_changed_y = 0;
    int visiable_changed_z = 0;

    if ( !force_reload ) {
        visiable_changed_x = ( chunk_diff_x != 0 ) && ( chunk->chunk_x == center_previous_x || chunk->chunk_x == center_next_x );
        visiable_changed_y = ( chunk_diff_y != 0 ) && ( chunk->chunk_y == center_previous_y || chunk->chunk_y == center_next_y );
        visiable_changed_z = ( chunk_diff_z != 0 ) && ( chunk->chunk_z == center_previous_z || chunk->chunk_z == center_next_z );
    }
    if ( force_reload || TRIP_OR( visiable_changed_ ) ) {
        // pr_debug( "Vis dir changed for %2d %2d %2d changed because of:%2d %2d %2d same:%2d %2d %2d", //
        //           TRIP_ARGS( chunk->chunk_ ),                                                        //
        //           TRIP_ARGS( chunk_diff_ ),                                                          //
        //           chunk->chunk_x == center_previous_x,                                               //
        //           chunk->chunk_y == center_previous_y,                                               //
        //           chunk->chunk_z == center_previous_z                                                //
        // );
        chunk_calculate_sides( chunk, TRIP_ARGS( center_next_ ) );
        return 1;
    }
    return 0;
}

void chunk_loader_render_chunks( LoadedChunks *loadedChunks, TRIP_ARGS( float camera_ ) ) {
    int chunk_x = floor( camera_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( camera_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( camera_z / ( float )CHUNK_SIZE );

    int loaded_x = loadedChunks->chunk_center_x;
    int loaded_y = loadedChunks->chunk_center_y;
    int loaded_z = loadedChunks->chunk_center_z;

    int chunk_diff_x = chunk_x - loaded_x;
    int chunk_diff_y = chunk_y - loaded_y;
    int chunk_diff_z = chunk_z - loaded_z;

    Chunk *chunk;
    int count = 0;
    do {
        chunk = terrain_loading_thread_dequeue( );
        if ( chunk ) {
            chunk->is_loading = 0;
            int reloaded = reload_if_out_of_bounds( chunk, TRIP_ARGS( chunk_ ) );
            if ( !reloaded ) {
                // pr_debug( "Paul Loading terrain x:%d y%d: z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
                process_chunk_position( chunk, TRIP_ARGS( chunk_diff_ ), TRIP_ARGS( loaded_ ), TRIP_ARGS( chunk_ ), 1 );
                chunk_program_terrain( chunk );
            }
        }
        count += 1;
    } while ( chunk );

    if ( TRIP_OR( 0 != chunk_diff_ ) ) {
        //pr_debug( "Moved outof chunk x:%d y:%d z:%d", TRIP_ARGS( loaded_ ) );
        //pr_debug( "Moved into  chunk x:%d y:%d z:%d", TRIP_ARGS( chunk_ ) );

        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            chunk = &loadedChunks->chunkArray[ i ];
            if ( chunk->is_loading ) {
                continue;
            }
            int reloaded = reload_if_out_of_bounds( chunk, TRIP_ARGS( chunk_ ) );
            if ( !reloaded ) {
                process_chunk_position( chunk, TRIP_ARGS( chunk_diff_ ), TRIP_ARGS( loaded_ ), TRIP_ARGS( chunk_ ), 0 );
            }
        }
        loadedChunks->chunk_center_x = chunk_x;
        loadedChunks->chunk_center_y = chunk_y;
        loadedChunks->chunk_center_z = chunk_z;
    }
}

void chunk_loader_draw_chunks( LoadedChunks *loadedChunks, glm::mat4 &mvp ) {
    shader_set_uniform_mat4f( &loadedChunks->shader, "u_MVP", mvp );
    // pr_debug( "Drawing %d chunks", loadedChunks->numLoadedChunks );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        // pr_debug( "Drawing chunk %d", i );
        chunk_render_solid( &loadedChunks->chunkArray[ i ], &loadedChunks->renderer, &loadedChunks->shader );
    }
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        // pr_debug( "Drawing chunk %d", i );
        chunk_render_water( &loadedChunks->chunkArray[ i ], &loadedChunks->renderer, &loadedChunks->shader );
    }
}

void chunk_loader_cleanup( LoadedChunks *loadedChunks ) {
    terrain_loading_thread_stop( );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk *chunk = &loadedChunks->chunkArray[ i ];
        if ( !chunk->is_loading ) {
            chunk_persist( chunk );
        }
        chunk_destroy( chunk );
    }
    free( loadedChunks->chunkArray );
    texture_destroy( &loadedChunks->blocksTexture );
}