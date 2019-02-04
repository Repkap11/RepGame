#include "common/chunk_loader.hpp"
#include "common/RepGame.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/constants.hpp"
#include <math.h>
#include <stdlib.h>

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

static CubeFace vd_data_solid[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_BACK, CORNER_OFFSET_bbl}, // 0
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_BACK, CORNER_OFFSET_bbr}, // 1
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_BACK, CORNER_OFFSET_tbr}, // 2
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_BACK, CORNER_OFFSET_tbl}, // 3

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_FRONT, CORNER_OFFSET_bfl}, // 4
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_FRONT, CORNER_OFFSET_bfr}, // 5
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_FRONT, CORNER_OFFSET_tfr}, // 6
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_FRONT, CORNER_OFFSET_tfl}, // 7

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_LEFT, CORNER_OFFSET_bbl},  // 8
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT, CORNER_OFFSET_bbr}, // 9
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT, CORNER_OFFSET_tbr}, // 10
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_LEFT, CORNER_OFFSET_tbl},  // 11

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_LEFT, CORNER_OFFSET_bfl},  // 12
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT, CORNER_OFFSET_bfr}, // 13
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT, CORNER_OFFSET_tfr}, // 14
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_LEFT, CORNER_OFFSET_tfl},  // 15

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_BOTTOM, CORNER_OFFSET_bbl}, // 16
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_BOTTOM, CORNER_OFFSET_bbr}, // 17
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_TOP, CORNER_OFFSET_tbr},    // 18
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_TOP, CORNER_OFFSET_tbl},    // 19

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM, CORNER_OFFSET_bfl}, // 20
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM, CORNER_OFFSET_bfr}, // 21
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_TOP, CORNER_OFFSET_tfr},    // 22
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_TOP, CORNER_OFFSET_tfl},    // 23

    {0.5f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_TOP, CORNER_OFFSET_c},    // 24
    {0.5f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BOTTOM, CORNER_OFFSET_c}, // 25
    {1.0f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_RIGHT, CORNER_OFFSET_c},  // 26
    {0.5f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_FRONT, CORNER_OFFSET_c},  // 27
    {0.0f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_LEFT, CORNER_OFFSET_c},   // 28
    {0.5f * BLOCK_SCALE, 0.5f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BACK, CORNER_OFFSET_c},   // 29

};
#define VB_DATA_SIZE_SOLID ( 5 * 6 + 6 )

void chunk_loader_init( LoadedChunks *loadedChunks ) {
    map_storage_init( );
    int status = terrain_loading_thread_start( );
    if ( status ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    loadedChunks->chunkArray = ( Chunk * )calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) );
    shader_init( &loadedChunks->shader, "chunk_vertex.glsl", "chunk_fragment.glsl" );
    texture_init_blocks( &loadedChunks->blocksTexture );
    // texture_bind( &loadedChunks->blocksTexture, 2 );

    block_definitions_initilize_definitions( &loadedChunks->blocksTexture );
    {
        vertex_buffer_init( &loadedChunks->solid.vb_block );
        vertex_buffer_set_data( &loadedChunks->solid.vb_block, vd_data_solid, sizeof( CubeFace ) * VB_DATA_SIZE_SOLID );
    }
    {
        vertex_buffer_init( &loadedChunks->water.vb_block );
        vertex_buffer_set_data( &loadedChunks->water.vb_block, vd_data_water, sizeof( CubeFace ) * VB_DATA_SIZE_WATER );
    }

    // These are from CubeFace
    vertex_buffer_layout_init( &loadedChunks->vbl_block );
    vertex_buffer_layout_push_float( &loadedChunks->vbl_block, 3 ); // Coords
    vertex_buffer_layout_push_float( &loadedChunks->vbl_block, 2 ); // Texture coords
    vertex_buffer_layout_push_float( &loadedChunks->vbl_block, 1 ); // Face type (top, sides, bottom)
    vertex_buffer_layout_push_float( &loadedChunks->vbl_block, 1 ); // Corner_shift

    // These are from BlockCoords
    vertex_buffer_layout_init( &loadedChunks->vbl_coords );
    vertex_buffer_layout_push_float( &loadedChunks->vbl_coords, 3 ); // block 3d world coords
    vertex_buffer_layout_push_float( &loadedChunks->vbl_coords, 3 ); // Multiples (mesh)
    vertex_buffer_layout_push_float( &loadedChunks->vbl_coords, 3 ); // which texture (block type)
    vertex_buffer_layout_push_float( &loadedChunks->vbl_coords, 3 ); // packed lighting
    vertex_buffer_layout_push_float( &loadedChunks->vbl_coords, 3 ); // packed lighting

    {
        VertexBuffer *vb_block_solid = &loadedChunks->solid.vb_block;
        VertexBuffer *vb_block_water = &loadedChunks->water.vb_block;
        VertexBufferLayout *vbl_block = &loadedChunks->vbl_block;
        VertexBufferLayout *vbl_coords = &loadedChunks->vbl_coords;
        mouse_selection_init( &loadedChunks->mouseSelection, vbl_block, vbl_coords );
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            chunk_init( &loadedChunks->chunkArray[ i ], vb_block_solid, vb_block_water, vbl_block, vbl_coords );
        }
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
                // pr_debug( "Chunk Done" );
                nextChunk = ( nextChunk + 1 );
            }
        }
    }
}

void chunk_loader_set_selected_block( LoadedChunks *loadedChunks, TRIP_ARGS( int selected_ ), int shouldDraw ) {
    mouse_selection_set_block( &loadedChunks->mouseSelection, TRIP_ARGS( selected_ ), shouldDraw );
}

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
#if defined( REPGAME_WASM )
    static int count = 0;
    count++;
    if ( count >= 3 ) {
        count = 0;
#else
    do {
#endif
        chunk = terrain_loading_thread_dequeue( );
        // pr_debug( "Got chunk %p", chunk );
        if ( chunk ) {
            chunk->is_loading = 0;
            int reloaded = reload_if_out_of_bounds( chunk, TRIP_ARGS( chunk_ ) );
            if ( !reloaded ) {
                // pr_debug( "Paul Loading terrain x:%d y%d: z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
                process_chunk_position( chunk, TRIP_ARGS( chunk_diff_ ), TRIP_ARGS( loaded_ ), TRIP_ARGS( chunk_ ), 1 );
                chunk_program_terrain( chunk );
            }
        }
#if defined( REPGAME_WASM )
    }
#else
    } while ( chunk );
#endif
    if ( TRIP_OR( 0 != chunk_diff_ ) ) {
        // pr_debug( "Moved outof chunk x:%d y:%d z:%d", TRIP_ARGS( loaded_ ) );
        // pr_debug( "Moved into  chunk x:%d y:%d z:%d", TRIP_ARGS( chunk_ ) );

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
float chunk_diameter = ( CHUNK_SIZE + 1 ) * 1.73205080757; // sqrt(3)

void chunk_loader_draw_chunks_solid( LoadedChunks *loadedChunks, glm::mat4 &mvp ) {
    shader_set_uniform1i( &loadedChunks->shader, "u_Texture", loadedChunks->blocksTexture.slot );
    shader_set_uniform_mat4f( &loadedChunks->shader, "u_MVP", mvp );
    // pr_debug( "Drawing %d chunks", loadedChunks->numLoadedChunks );

    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        int final_is_visiable = 1;
        Chunk *chunk = &loadedChunks->chunkArray[ i ];
        glm::vec4 chunk_coords = glm::vec4(               //
            chunk->chunk_x * CHUNK_SIZE + CHUNK_SIZE / 2, //
            chunk->chunk_y * CHUNK_SIZE + CHUNK_SIZE / 2, //
            chunk->chunk_z * CHUNK_SIZE + CHUNK_SIZE / 2, 1 );
        glm::vec4 result_v = mvp * chunk_coords;
        float adjusted_diameter = chunk_diameter / fabsf( result_v.w );

        result_v.x /= result_v.w;
        result_v.y /= result_v.w;
        result_v.z /= result_v.w;
        if ( fabsf( result_v.x ) > 1 + adjusted_diameter || //
             fabsf( result_v.y ) > 1 + adjusted_diameter || //
             fabsf( result_v.z ) > 1 + adjusted_diameter ) {
            final_is_visiable = 0;
        } else {
            final_is_visiable = 1;
        }
        if ( final_is_visiable ) {
            chunk->cached_cull_water = 0;
            chunk_render_solid( chunk, &loadedChunks->renderer, &loadedChunks->shader );
        } else {
            chunk->cached_cull_water = 1;
        }
    }
}
void chunk_loader_draw_chunks_liquid( LoadedChunks *loadedChunks, glm::mat4 &mvp ) {

    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk *chunk = &loadedChunks->chunkArray[ i ];
        if ( !chunk->cached_cull_water ) {
            chunk_render_water( &loadedChunks->chunkArray[ i ], &loadedChunks->renderer, &loadedChunks->shader );
        }
    }
}

void chunk_loader_draw_mouse_selection( LoadedChunks *loadedChunks ) {
    mouse_selection_draw( &loadedChunks->mouseSelection, &loadedChunks->renderer, &loadedChunks->shader );
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