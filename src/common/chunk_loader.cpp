#include <math.h>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/chunk_loader.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/abstract/shader.hpp"
#include "common/constants.hpp"

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

MK_SHADER( chunk_vertex );
MK_SHADER( chunk_fragment );

int mod( int x, int N ) {
    int result = ( x % N + N ) % N;
    if ( result < 0 ) {
        pr_debug( "Bad Mod:%d", result );
    }
    return result;
}
int reload_if_out_of_bounds( Chunk *chunk, TRIP_ARGS( int chunk_ ) ) {

    int new_chunk_x = ( int )floorf( ( ( float )( chunk_x - chunk->chunk_mod_x + CHUNK_RADIUS_X ) ) / ( ( float )( CHUNK_RADIUS_X * 2 + 1 ) ) ) * ( CHUNK_RADIUS_X * 2 + 1 ) + chunk->chunk_mod_x;
    int new_chunk_y = ( int )floorf( ( ( float )( chunk_y - chunk->chunk_mod_y + CHUNK_RADIUS_Y ) ) / ( ( float )( CHUNK_RADIUS_Y * 2 + 1 ) ) ) * ( CHUNK_RADIUS_Y * 2 + 1 ) + chunk->chunk_mod_y;
    int new_chunk_z = ( int )floorf( ( ( float )( chunk_z - chunk->chunk_mod_z + CHUNK_RADIUS_Z ) ) / ( ( float )( CHUNK_RADIUS_Z * 2 + 1 ) ) ) * ( CHUNK_RADIUS_Z * 2 + 1 ) + chunk->chunk_mod_z;
    int changed = new_chunk_x != chunk->chunk_x || new_chunk_y != chunk->chunk_y || new_chunk_z != chunk->chunk_z;

    if ( changed ) {
        chunk_unprogram_terrain( chunk );
        chunk->is_loading = 1;
        terrain_loading_thread_enqueue( chunk, new_chunk_x, new_chunk_y, new_chunk_z, 1 );
    }
    return changed;
}

void chunk_loader_init( LoadedChunks *loadedChunks, TRIP_ARGS( float camera_ ), VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords ) {
    int status = terrain_loading_thread_start( );
    if ( status ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    loadedChunks->chunkArray = ( Chunk * )calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) );
    showErrors( );

    shader_init( &loadedChunks->shader, &chunk_vertex, &chunk_fragment );
    if ( !DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        shader_set_uniform1f( &loadedChunks->shader, "u_ShowRotation", -2 );
    }

    {
        vertex_buffer_init( &loadedChunks->solid.vb_block );
        vertex_buffer_set_data( &loadedChunks->solid.vb_block, vd_data_solid, sizeof( CubeFace ) * VB_DATA_SIZE_SOLID );
    }

    {
        vertex_buffer_init( &loadedChunks->water.vb_block );
        vertex_buffer_set_data( &loadedChunks->water.vb_block, vd_data_water, sizeof( CubeFace ) * VB_DATA_SIZE_WATER );
    }

    {
        VertexBuffer *vb_block_solid = &loadedChunks->solid.vb_block;
        VertexBuffer *vb_block_water = &loadedChunks->water.vb_block;
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            chunk_init( &loadedChunks->chunkArray[ i ], vb_block_solid, vb_block_water, vbl_block, vbl_coords );
        }
    }

    showErrors( );

    int camera_chunk_x = floor( camera_x / ( float )CHUNK_SIZE );
    int camera_chunk_y = floor( camera_y / ( float )CHUNK_SIZE );
    int camera_chunk_z = floor( camera_z / ( float )CHUNK_SIZE );

    int nextChunk = 0;
    for ( int i = 0; i <= CHUNK_RADIUS_X; i++ ) {
        for ( int j = 0; j <= CHUNK_RADIUS_Y; j++ ) {
            for ( int k = 0; k <= CHUNK_RADIUS_Z; k++ ) {
                for ( int sign_i = -1; sign_i < 3; sign_i += 2 ) {
                    if ( sign_i == -1 && i == CHUNK_RADIUS_X ) {
                        continue;
                    }
                    for ( int sign_j = -1; sign_j < 3; sign_j += 2 ) {
                        if ( sign_j == -1 && j == CHUNK_RADIUS_Y ) {
                            continue;
                        }
                        for ( int sign_k = -1; sign_k < 3; sign_k += 2 ) {
                            if ( sign_k == -1 && k == CHUNK_RADIUS_Z ) {
                                continue;
                            }
                            int new_i = ( i * sign_i ) - ( sign_i == -1 );
                            int new_j = ( j * sign_j ) - ( sign_j == -1 );
                            int new_k = ( k * sign_k ) - ( sign_k == -1 );

                            Chunk *chunk = &loadedChunks->chunkArray[ nextChunk ];
                            chunk->chunk_x = new_i + camera_chunk_x;
                            chunk->chunk_y = new_j + camera_chunk_y;
                            chunk->chunk_z = new_k + camera_chunk_z;

                            chunk->chunk_mod_x = mod( chunk->chunk_x, 2 * CHUNK_RADIUS_X + 1 );
                            chunk->chunk_mod_y = mod( chunk->chunk_y, 2 * CHUNK_RADIUS_Y + 1 );
                            chunk->chunk_mod_z = mod( chunk->chunk_z, 2 * CHUNK_RADIUS_Z + 1 );

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

Chunk *chunk_loader_get_chunk( LoadedChunks *loadedChunks, TRIP_ARGS( int chunk_ ) ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk *chunk = &loadedChunks->chunkArray[ i ];
        if ( chunk->chunk_x == chunk_x && chunk->chunk_y == chunk_y && chunk->chunk_z == chunk_z ) {
            return chunk;
        }
    }
    return NULL;
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
    if ( force_reload || visiable_changed_x || visiable_changed_y || visiable_changed_z ) {
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

void chunk_loader_render_chunks( LoadedChunks *loadedChunks, TRIP_ARGS( float camera_ ), int limit_render ) {
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
    do {
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
    } while ( chunk && !limit_render );
    if ( 0 != chunk_diff_x || 0 != chunk_diff_y || 0 != chunk_diff_z ) {
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
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk *chunk = &loadedChunks->chunkArray[ i ];
        if ( !chunk->is_loading && chunk->needs_repopulation && !chunk->cached_cull ) {
            chunk_unprogram_terrain( chunk );
            chunk_calculate_popupated_blocks( chunk );
            chunk_program_terrain( chunk );
            chunk->needs_repopulation = false;
        }
    }
}
float chunk_diameter = ( CHUNK_SIZE + 1 ) * 1.73205080757; // sqrt(3)

void chunk_loader_calculate_cull( LoadedChunks *loadedChunks, const glm::mat4 &mvp ) {
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

int shouldInc = 0;
int showRotation = 0;
void chunk_loader_draw_chunks( LoadedChunks *loadedChunks, const glm::mat4 &mvp, Renderer *renderer, bool reflect_only, bool draw_reflect ) {
    shader_set_uniform_mat4f( &loadedChunks->shader, "u_MVP", mvp );

    // pr_debug( "Drawing %d chunks", loadedChunks->numLoadedChunks );
    for ( int renderOrder = LAST_RENDER_ORDER - 1; renderOrder > 0; renderOrder-- ) {
        if ( reflect_only == ( renderOrder == RenderOrder_Water ) ) {
            shader_set_uniform1f( &loadedChunks->shader, "u_shouldDiscardAlpha", renderOrder != RenderOrder_Water );
            for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
                Chunk *chunk = &loadedChunks->chunkArray[ i ];
                if ( !chunk->cached_cull ) {
                    chunk_render( &loadedChunks->chunkArray[ i ], renderer, &loadedChunks->shader, ( RenderOrder )renderOrder, draw_reflect );
                }
            }
        }
    }
    if ( DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        shouldInc++;
        if ( shouldInc > 100 ) {
            shouldInc = 0;
            showRotation++;
            if ( showRotation >= 8 ) {
                showRotation = 0;
            }
        }
        shader_set_uniform1f( &loadedChunks->shader, "u_ShowRotation", showRotation );
    }

    shader_set_uniform1f( &loadedChunks->shader, "u_shouldDiscardAlpha", 1 );
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