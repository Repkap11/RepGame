#include <math.h>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/chunk_loader.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/abstract/shader.hpp"
#include "common/constants.hpp"

MK_SHADER( chunk_vertex );
MK_SHADER( chunk_fragment );

int mod( int x, int N ) {
    int result = ( x % N + N ) % N;
    if ( result < 0 ) {
        pr_debug( "Bad Mod:%d", result );
    }
    return result;
}
int ChunkLoader::reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos ) {

    int new_chunk_x = ( int )floorf( ( ( float )( chunk_pos.x - chunk.chunk_mod.x + CHUNK_RADIUS_X ) ) / ( ( float )( CHUNK_RADIUS_X * 2 + 1 ) ) ) * ( CHUNK_RADIUS_X * 2 + 1 ) + chunk.chunk_mod.x;
    int new_chunk_y = ( int )floorf( ( ( float )( chunk_pos.y - chunk.chunk_mod.y + CHUNK_RADIUS_Y ) ) / ( ( float )( CHUNK_RADIUS_Y * 2 + 1 ) ) ) * ( CHUNK_RADIUS_Y * 2 + 1 ) + chunk.chunk_mod.y;
    int new_chunk_z = ( int )floorf( ( ( float )( chunk_pos.z - chunk.chunk_mod.z + CHUNK_RADIUS_Z ) ) / ( ( float )( CHUNK_RADIUS_Z * 2 + 1 ) ) ) * ( CHUNK_RADIUS_Z * 2 + 1 ) + chunk.chunk_mod.z;
    int changed = new_chunk_x != chunk.chunk_pos.x || new_chunk_y != chunk.chunk_pos.y || new_chunk_z != chunk.chunk_pos.z;
    if ( changed ) {
        chunk.unprogram_terrain( );
        chunk.is_loading = 1;
        glm::ivec3 new_chunk = glm::ivec3( new_chunk_x, new_chunk_y, new_chunk_z );
        TerrainLoadingThread::enqueue( &chunk, new_chunk, 1 );
    }
    return changed;
}

void ChunkLoader::init( const glm::vec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords ) {
    int status = TerrainLoadingThread::start( );
    if ( status ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    this->chunkArray = ( Chunk * )calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) );
    showErrors( );
    // pr_debug( "Num Total Chunks:%d", MAX_LOADED_CHUNKS );

    Shader &shader = this->shader;
    shader.init( &chunk_vertex, &chunk_fragment );
    if ( !DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        shader.set_uniform1f( "u_ShowRotation", -2 );
    }

    this->solid.vb_block.init( );
    this->solid.vb_block.set_data( vd_data_solid, sizeof( CubeFace ) * VB_DATA_SIZE_SOLID );

    this->water.vb_block.init( );
    this->water.vb_block.set_data( vd_data_water, sizeof( CubeFace ) * VB_DATA_SIZE_WATER );

    VertexBuffer &vb_block_solid = this->solid.vb_block;
    VertexBuffer &vb_block_water = this->water.vb_block;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        this->chunkArray[ i ].init( vb_block_solid, vb_block_water, vbl_block, vbl_coords );
    }

    showErrors( );

    glm::ivec3 camera_chuck = glm::floor( camera_pos / ( float )CHUNK_SIZE );

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
                            glm::ivec3 new_offset = glm::vec3( new_i, new_j, new_k );

                            Chunk &chunk = this->chunkArray[ nextChunk ];
                            chunk.chunk_pos = new_offset + camera_chuck;

                            chunk.chunk_mod.x = mod( chunk.chunk_pos.x, 2 * CHUNK_RADIUS_X + 1 );
                            chunk.chunk_mod.y = mod( chunk.chunk_pos.y, 2 * CHUNK_RADIUS_Y + 1 );
                            chunk.chunk_mod.z = mod( chunk.chunk_pos.z, 2 * CHUNK_RADIUS_Z + 1 );

                            chunk.is_loading = 1;
                            TerrainLoadingThread::enqueue( &chunk, chunk.chunk_pos, 0 );
                            nextChunk = ( nextChunk + 1 );
                        }
                    }
                }
            }
        }
    }
}

Chunk *ChunkLoader::get_chunk( const glm::ivec3 &chunk_pos ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( chunk.chunk_pos.x == chunk_pos.x && chunk.chunk_pos.y == chunk_pos.y && chunk.chunk_pos.z == chunk_pos.z ) {
            return &chunk;
        }
    }
    return NULL;
}

inline int ChunkLoader::process_chunk_position( Chunk &chunk, const glm::ivec3 &chunk_diff, const glm::ivec3 &center_previous, const glm::ivec3 &center_next, int force_reload ) {

    int visible_changed_x = 0;
    int visible_changed_y = 0;
    int visible_changed_z = 0;

    if ( !force_reload ) {
        visible_changed_x = ( chunk_diff.x != 0 ) && ( chunk.chunk_pos.x == center_previous.x || chunk.chunk_pos.x == center_next.x );
        visible_changed_y = ( chunk_diff.y != 0 ) && ( chunk.chunk_pos.y == center_previous.y || chunk.chunk_pos.y == center_next.y );
        visible_changed_z = ( chunk_diff.z != 0 ) && ( chunk.chunk_pos.z == center_previous.z || chunk.chunk_pos.z == center_next.z );
    }
    if ( force_reload || visible_changed_x || visible_changed_y || visible_changed_z ) {
        // pr_debug( "Vis dir changed for %2d %2d %2d changed because of:%2d %2d %2d same:%2d %2d %2d", //
        //           TRIP_ARGS( chunk->chunk_ ),                                                        //
        //           TRIP_ARGS( chunk_diff_ ),                                                          //
        //           chunk->chunk_x == center_previous_x,                                               //
        //           chunk->chunk_y == center_previous_y,                                               //
        //           chunk->chunk_z == center_previous_z                                                //
        // );
        chunk.calculate_sides( center_next );
        return 1;
    }
    return 0;
}

void ChunkLoader::render_chunks( const glm::vec3 &camera_pos, int limit_render ) {
    glm::ivec3 chunk_pos = glm::floor( camera_pos / ( float )CHUNK_SIZE );
    glm::ivec3 &loaded_pos = this->chunk_center;
    glm::ivec3 chunk_diff = chunk_pos - loaded_pos;

    {
        Chunk *chunk_ptr;
        do {
            chunk_ptr = TerrainLoadingThread::dequeue( );
            // pr_debug( "Got chunk %p", chunk );
            if ( chunk_ptr ) {
                Chunk &chunk = *chunk_ptr;
                chunk.is_loading = 0;
                int reloaded = reload_if_out_of_bounds( chunk, chunk_pos );
                if ( !reloaded ) {
                    // pr_debug( "Paul Loading terrain x:%d y%d: z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
                    process_chunk_position( chunk, chunk_diff, loaded_pos, chunk_pos, 1 );
                    chunk.program_terrain( );
                }
            }
        } while ( chunk_ptr && !limit_render );
    }
    if ( 0 != chunk_diff.x || 0 != chunk_diff.y || 0 != chunk_diff.z ) {
        // pr_debug( "Moved outof chunk x:%d y:%d z:%d", loaded_pos );
        // pr_debug( "Moved into  chunk x:%d y:%d z:%d", chunk_pos );

        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            Chunk &chunk = this->chunkArray[ i ];
            if ( chunk.is_loading ) {
                continue;
            }
            int reloaded = reload_if_out_of_bounds( chunk, chunk_pos );
            if ( !reloaded ) {
                process_chunk_position( chunk, chunk_diff, loaded_pos, chunk_pos, 0 );
            }
        }
        this->chunk_center = chunk_pos;
    }
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( chunk.needs_repopulation && !chunk.is_loading && !( chunk.cached_cull_normal && chunk.cached_cull_reflect ) ) {
            chunk.unprogram_terrain( );
            chunk.calculate_populated_blocks( );
            chunk.program_terrain( );
            chunk.needs_repopulation = false;
        }
    }
}
float chunk_diameter = ( CHUNK_SIZE + 1 ) * 1.73205080757; // sqrt(3)

void ChunkLoader::calculate_cull( const glm::mat4 &mvp, bool saveAsReflection ) {
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        int final_is_visiable = 1;
        Chunk *chunk = &this->chunkArray[ i ];
        if ( CULL_NON_VISIBLE ) {
            glm::vec4 chunk_coords = glm::vec4(                   //
                chunk->chunk_pos.x * CHUNK_SIZE + CHUNK_SIZE / 2, //
                chunk->chunk_pos.y * CHUNK_SIZE + CHUNK_SIZE / 2, //
                chunk->chunk_pos.z * CHUNK_SIZE + CHUNK_SIZE / 2, 1 );
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
        if ( saveAsReflection ) {
            chunk->cached_cull_reflect = !final_is_visiable;
        } else {
            chunk->cached_cull_normal = !final_is_visiable;
        }
    }
}

int shouldInc = 0;
int showRotation = 0;
void ChunkLoader::draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, bool reflect_only, bool draw_reflect ) {
    Shader &shader = this->shader;
    shader.set_uniform_mat4f( "u_MVP", mvp );

    // pr_debug( "Drawing %d chunks", this->numLoadedChunks );
    for ( int renderOrder = LAST_RENDER_ORDER - 1; renderOrder > 0; renderOrder-- ) {
        if ( reflect_only == ( renderOrder == RenderOrder_Water ) ) {
            shader.set_uniform1f( "u_shouldDiscardAlpha", renderOrder != RenderOrder_Water );
            for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
                Chunk &chunk = this->chunkArray[ i ];
                if ( draw_reflect ) {
                    if ( !chunk.cached_cull_reflect ) {
                        chunk.draw( renderer, texture, shader, ( RenderOrder )renderOrder, draw_reflect );
                    }
                } else {
                    if ( !chunk.cached_cull_normal ) {
                        chunk.draw( renderer, texture, shader, ( RenderOrder )renderOrder, draw_reflect );
                    }
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
        shader.set_uniform1f( "u_ShowRotation", showRotation );
    }
    shader.set_uniform1f( "u_shouldDiscardAlpha", 1 );
}

void ChunkLoader::process_random_ticks( ) {
}

void ChunkLoader::cleanup( ) {
    TerrainLoadingThread::stop( );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( !chunk.is_loading ) {
            chunk.persist( );
        }
        chunk.destroy( );
    }
    free( this->chunkArray );
}