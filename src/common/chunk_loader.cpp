#include "common/chunk_loader.hpp"
#include "common/RepGame.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/abstract/shader.hpp"
#include "common/constants.hpp"
#include <math.h>
#include <stdlib.h>

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

static CubeFace vd_data_solid[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_BACK, CORNER_OFFSET_bbl}, // 0
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_BACK, CORNER_OFFSET_bbr}, // 1
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_BACK, CORNER_OFFSET_tbr}, // 2
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_BACK, CORNER_OFFSET_tbl}, // 3

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_FRONT, CORNER_OFFSET_bfl}, // 4
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_FRONT, CORNER_OFFSET_bfr}, // 5
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_FRONT, CORNER_OFFSET_tfr}, // 6
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_FRONT, CORNER_OFFSET_tfl}, // 7

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_LEFT, CORNER_OFFSET_bbl},  // 8
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT, CORNER_OFFSET_bbr}, // 9
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT, CORNER_OFFSET_tbr}, // 10
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_LEFT, CORNER_OFFSET_tbl},  // 11

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_LEFT, CORNER_OFFSET_bfl},  // 12
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT, CORNER_OFFSET_bfr}, // 13
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT, CORNER_OFFSET_tfr}, // 14
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_LEFT, CORNER_OFFSET_tfl},  // 15

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_BOTTOM, CORNER_OFFSET_bbl}, // 16
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_BOTTOM, CORNER_OFFSET_bbr}, // 17
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_TOP, CORNER_OFFSET_tbr},    // 18
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_TOP, CORNER_OFFSET_tbl},    // 19

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM, CORNER_OFFSET_bfl}, // 20
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM, CORNER_OFFSET_bfr}, // 21
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP, CORNER_OFFSET_tfr},    // 22
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP, CORNER_OFFSET_tfl},    // 23

    {0.5f, 1.0f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_TOP, CORNER_OFFSET_c},    // 24
    {0.5f, 0.0f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BOTTOM, CORNER_OFFSET_c}, // 25
    {1.0f, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_RIGHT, CORNER_OFFSET_c},  // 26
    {0.5f, 0.5f, 1.0f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_FRONT, CORNER_OFFSET_c},  // 27
    {0.0f, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_LEFT, CORNER_OFFSET_c},   // 28
    {0.5f, 0.5f, 0.0f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BACK, CORNER_OFFSET_c},   // 29

};
#define VB_DATA_SIZE_SOLID ( 5 * 6 + 6 )

MK_SHADER( chunk_vertex );
MK_SHADER( chunk_fragment );

void chunk_loader_init( LoadedChunks *loadedChunks ) {
    int status = terrain_loading_thread_start( );
    if ( status ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    loadedChunks->chunkArray = ( Chunk * )calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) );

    shader_init( &loadedChunks->shader, &chunk_vertex, &chunk_fragment );

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
    vertex_buffer_layout_push_float( &loadedChunks->vbl_coords, 3 ); // which texture (block type 1)
    vertex_buffer_layout_push_float( &loadedChunks->vbl_coords, 3 ); // which texture (block type 2)
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
    for ( int i = 0; i <= CHUNK_RADIUS_X; i++ ) {
        for ( int j = 0; j <= CHUNK_RADIUS_Y; j++ ) {
            for ( int k = 0; k <= CHUNK_RADIUS_Z; k++ ) {
                for ( int sign_i = -1; sign_i < 3; sign_i += 2 ) {
                    for ( int sign_j = -1; sign_j < 3; sign_j += 2 ) {
                        for ( int sign_k = -1; sign_k < 3; sign_k += 2 ) {
                            if ( sign_i == -1 && i == CHUNK_RADIUS_X ) {
                                continue;
                            }
                            if ( sign_j == -1 && j == CHUNK_RADIUS_Y ) {
                                continue;
                            }
                            if ( sign_k == -1 && k == CHUNK_RADIUS_Z ) {
                                continue;
                            }
                            int new_i = ( i * sign_i ) - ( sign_i == -1 );
                            int new_j = ( j * sign_j ) - ( sign_j == -1 );
                            int new_k = ( k * sign_k ) - ( sign_k == -1 );
                            Chunk *chunk = &loadedChunks->chunkArray[ nextChunk ];
                            chunk->chunk_x = new_i;
                            chunk->chunk_y = new_j;
                            chunk->chunk_z = new_k;
                            chunk->chunk_mod_x = new_i < 0 ? -CHUNK_RADIUS_X - new_i - 1 : CHUNK_RADIUS_X - new_i;
                            chunk->chunk_mod_y = new_j < 0 ? -CHUNK_RADIUS_Y - new_j - 1 : CHUNK_RADIUS_Y - new_j;
                            chunk->chunk_mod_z = new_k < 0 ? -CHUNK_RADIUS_Z - new_k - 1 : CHUNK_RADIUS_Z - new_k;

                            chunk->is_loading = 1;
                            terrain_loading_thread_enqueue( chunk, TRIP_ARGS( chunk->chunk_ ), 0 );
                            nextChunk = ( nextChunk + 1 );
                        }
                    }
                }
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

void chunk_loader_calculate_cull( LoadedChunks *loadedChunks, glm::mat4 &mvp ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        int final_is_visiable = 1;
        Chunk *chunk = &loadedChunks->chunkArray[ i ];
        if ( CULL_NON_VISIBLE ) {
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
        }
        chunk->cached_cull = !final_is_visiable;
    }
}

static float wave_angle_x = 0.0f;
static float wave_angle_z = 0.0f;
static float wave_angle_y = 0.0f;
void chunk_loader_draw_chunks( LoadedChunks *loadedChunks, glm::mat4 &mvp ) {
    // pr_debug( "Drawing %d chunks", loadedChunks->numLoadedChunks );
    float wave_x = 0, wave_y = 0, wave_z = 0;

    wave_angle_x += 0.01f;
    wave_angle_z += 0.015f;
    wave_angle_y += 0.007f;

    if ( wave_angle_x > 2 * M_PI ) {
        wave_angle_x -= 2 * M_PI;
    }
    if ( wave_angle_z > 2 * M_PI ) {
        wave_angle_z -= 2 * M_PI;
    }
    if ( wave_angle_y > 2 * M_PI ) {
        wave_angle_y -= 2 * M_PI;
    }
    wave_x = sin( wave_angle_x ) * 0.05f;
    wave_z = cos( wave_angle_z ) * 0.05f;
    wave_y = cos( wave_angle_y ) * 0.02f;

    for ( int renderOrder = LAST_RENDER_ORDER - 1; renderOrder > 0; renderOrder-- ) {
        shader_set_uniform1f( &loadedChunks->shader, "u_shouldDiscardAlpha", renderOrder != RenderOrder_Water );

        shader_set_uniform3f( &loadedChunks->shader, "u_WaveOffset", wave_x, wave_y, wave_z );
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            Chunk *chunk = &loadedChunks->chunkArray[ i ];
            if ( !chunk->cached_cull ) {
                chunk_render( &loadedChunks->chunkArray[ i ], &loadedChunks->renderer, &loadedChunks->shader, ( RenderOrder )renderOrder );
            }
        }
    }

    shader_set_uniform1f( &loadedChunks->shader, "u_shouldDiscardAlpha", 1 );
    shader_set_uniform3f( &loadedChunks->shader, "u_WaveOffset", 0, 0, 0 );
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
}