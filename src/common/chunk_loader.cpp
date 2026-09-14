#include <math.h>
#include <stdlib.h>
#include <chrono>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chunk_loader.hpp"
#include "common/multiplayer.hpp"
#include "common/net/packet.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/renderer/shader.hpp"
#include "common/constants.hpp"

MK_SHADER( chunk_vertex );
MK_SHADER( chunk_fragment );

// When limit_render is set (WASM, no background loading thread), cap how long
// render_chunks spends generating/loading chunks per frame so the main thread
// keeps up a steady frame rate while still making progress every frame.
static constexpr long long WASM_LOAD_BUDGET_US = 4000; // 4 ms

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

int mod( const int x, const int N ) {
    const int result = ( x % N + N ) % N;
    if ( result < 0 ) {
        pr_debug( "Bad Mod:%d", result );
    }
    return result;
}
int ChunkLoader::reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos, glm::ivec3 *out_new_pos ) {

    const int new_chunk_x = static_cast<int>( floorf( static_cast<float>( chunk_pos.x - chunk.chunk_mod.x + CHUNK_RADIUS_X ) / static_cast<float>( ( CHUNK_RADIUS_X * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_X * 2 + 1 ) + chunk.chunk_mod.x;
    const int new_chunk_y = static_cast<int>( floorf( static_cast<float>( chunk_pos.y - chunk.chunk_mod.y + CHUNK_RADIUS_Y ) / static_cast<float>( ( CHUNK_RADIUS_Y * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_Y * 2 + 1 ) + chunk.chunk_mod.y;
    const int new_chunk_z = static_cast<int>( floorf( static_cast<float>( chunk_pos.z - chunk.chunk_mod.z + CHUNK_RADIUS_Z ) / static_cast<float>( ( CHUNK_RADIUS_Z * 2 + 1 ) ) ) ) * ( CHUNK_RADIUS_Z * 2 + 1 ) + chunk.chunk_mod.z;
    const int changed = new_chunk_x != chunk.chunk_pos.x || new_chunk_y != chunk.chunk_pos.y || new_chunk_z != chunk.chunk_pos.z;
    if ( changed ) {
        chunk.unprogram_terrain( );
        chunk.is_loading = 1;
        const glm::ivec3 new_chunk = glm::ivec3( new_chunk_x, new_chunk_y, new_chunk_z );
        if ( out_new_pos ) {
            *out_new_pos = new_chunk;
        }
        this->terrain_loading_thread.enqueue( &chunk, new_chunk, 1 );
    }
    return changed;
}

void ChunkLoader::init( const glm::vec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords, MapStorage &map_storage ) {
    if ( this->terrain_loading_thread.start( map_storage ) ) {
        pr_debug( "Terrain loading thread failed to start." );
    }
    this->chunkArray = static_cast<Chunk *>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk ) ) );
    this->drawable_chunks = static_cast<Chunk **>( calloc( MAX_LOADED_CHUNKS, sizeof( Chunk * ) ) );
    this->num_drawable = 0;
    showErrors( );
    // pr_debug( "Num Total Chunks:%d", MAX_LOADED_CHUNKS );

    Shader &shader = this->shader;
    shader.init( &chunk_vertex, &chunk_fragment );
    if constexpr ( !DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
        shader.set_uniform1f( "u_ShowRotation", -2 );
    }

    this->solid.vb_block.init( );
    this->solid.vb_block.set_data( vd_data_solid, sizeof( CubeFace ) * VB_DATA_SIZE_SOLID );

    this->water.vb_block.init( );
    this->water.vb_block.set_data( vd_data_water, sizeof( CubeFace ) * VB_DATA_SIZE_WATER );

    const VertexBuffer &vb_block_solid = this->solid.vb_block;
    const VertexBuffer &vb_block_water = this->water.vb_block;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        this->chunkArray[ i ].init( vb_block_solid, vb_block_water, vbl_block, vbl_coords );
    }

    showErrors( );

    glm::vec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    glm::ivec3 camera_chuck = glm::floor( camera_pos / chunk_size );

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

                            glm::ivec3 chunk_pos = new_offset + camera_chuck;

                            glm::ivec3 chunk_mod;
                            chunk_mod.x = mod( chunk_pos.x, 2 * CHUNK_RADIUS_X + 1 );
                            chunk_mod.y = mod( chunk_pos.y, 2 * CHUNK_RADIUS_Y + 1 );
                            chunk_mod.z = mod( chunk_pos.z, 2 * CHUNK_RADIUS_Z + 1 );

                            int slot = chunk_slot_from_mod( chunk_mod );
                            Chunk &chunk = this->chunkArray[ slot ];
                            chunk.chunk_pos = chunk_pos;
                            chunk.chunk_mod = chunk_mod;

                            chunk.is_loading = 1;
                            this->terrain_loading_thread.enqueue( &chunk, chunk.chunk_pos, 0 );
                            nextChunk = ( nextChunk + 1 );
                        }
                    }
                }
            }
        }
    }
    this->rebuild_drawable_list( );
    this->initial_chunk_request_pending = true;
}

Chunk *ChunkLoader::get_chunk( const glm::ivec3 &chunk_pos ) const {
    const int slot = chunk_slot_from_pos( chunk_pos );
    Chunk &chunk = this->chunkArray[ slot ];
    if ( chunk.chunk_pos == chunk_pos ) {
        return &chunk;
    }
    return nullptr;
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

void ChunkLoader::render_chunks( Multiplayer &multiplayer, const glm::vec3 &camera_pos, int limit_render ) {
    this->num_remeshed_this_frame = 0;
    glm::vec3 chunk_size = glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );

    glm::ivec3 chunk_pos = glm::floor( camera_pos / chunk_size );
    glm::ivec3 &loaded_pos = this->chunk_center;
    glm::ivec3 chunk_diff = chunk_pos - loaded_pos;

    // Phase 2: fire one box request for the initial visible area instead of
    // per-chunk requests as terrain gen finishes. This overlaps the network
    // round-trip with GPU terrain gen.
    if ( this->initial_chunk_request_pending ) {
        this->initial_chunk_request_pending = false;
        glm::ivec3 box_min = chunk_pos - glm::ivec3( CHUNK_RADIUS_X, CHUNK_RADIUS_Y, CHUNK_RADIUS_Z );
        multiplayer.request_chunks_box( box_min,
                                        static_cast<uint8_t>( 2 * CHUNK_RADIUS_X + 1 ),
                                        static_cast<uint8_t>( 2 * CHUNK_RADIUS_Y + 1 ),
                                        static_cast<uint8_t>( 2 * CHUNK_RADIUS_Z + 1 ) );
    }

    {
        Chunk *chunk_ptr;
        const long long t_budget_start = now_us( );
        do {
            chunk_ptr = this->terrain_loading_thread.dequeue( );
            if ( chunk_ptr ) {
                Chunk &chunk = *chunk_ptr;
                chunk.is_loading = 0;
                int reloaded = reload_if_out_of_bounds( chunk, chunk_pos );
                if ( !reloaded ) {
                    // Lazily create this chunk's GL objects on first load so the
                    // startup loop doesn't block the first frame.
                    chunk.ensure_gl_init( );
                    process_chunk_position( chunk, chunk_diff, loaded_pos, chunk_pos, 1 );
                    // Apply any chunk diffs that arrived from the server before
                    // this chunk's terrain gen finished (Phase 2 eager requests).
                    multiplayer.apply_pending_diffs( chunk );
                    chunk.program_terrain( );
                }
            }
            // When limit_render is true (WASM without pthreads), terrain is
            // generated inline in dequeue() on the main thread, so cap the work
            // per frame to a time budget. When limit_render is false (native, or
            // WASM with pthreads), the budget term short-circuits and we drain all
            // chunks already finished by the background threads.
        } while ( chunk_ptr && ( !limit_render || ( now_us( ) - t_budget_start < WASM_LOAD_BUDGET_US ) ) );
    }
    if ( 0 != chunk_diff.x || 0 != chunk_diff.y || 0 != chunk_diff.z ) {
        // Collect new chunk positions from the reload loop so we can fire one
        // box request for all of them at once (Phase 2).
        std::vector<glm::ivec3> new_positions;
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            Chunk &chunk = this->chunkArray[ i ];
            if ( chunk.is_loading ) {
                continue;
            }
            glm::ivec3 new_pos;
            int reloaded = reload_if_out_of_bounds( chunk, chunk_pos, &new_pos );
            if ( !reloaded ) {
                process_chunk_position( chunk, chunk_diff, loaded_pos, chunk_pos, 0 );
            } else {
                new_positions.push_back( new_pos );
            }
        }
        // Fire one box request covering all newly loaded chunks. The box may
        // include chunks that don't need loading; the server just returns no
        // diffs for those, so it's harmless.
        if ( !new_positions.empty( ) ) {
            glm::ivec3 box_min = new_positions[ 0 ];
            glm::ivec3 box_max = new_positions[ 0 ];
            for ( const auto &p : new_positions ) {
                box_min = glm::min( box_min, p );
                box_max = glm::max( box_max, p );
            }
            glm::ivec3 box_size = box_max - box_min + glm::ivec3( 1 );
            // Clamp to NET_MAX_BOX_DIM per axis (shouldn't exceed on any platform,
            // but guard against pathological cases).
            uint8_t sx = static_cast<uint8_t>( std::min( box_size.x, static_cast<int>( NET_MAX_BOX_DIM ) ) );
            uint8_t sy = static_cast<uint8_t>( std::min( box_size.y, static_cast<int>( NET_MAX_BOX_DIM ) ) );
            uint8_t sz = static_cast<uint8_t>( std::min( box_size.z, static_cast<int>( NET_MAX_BOX_DIM ) ) );
            multiplayer.request_chunks_box( box_min, sx, sy, sz );
        }
        this->chunk_center = chunk_pos;
    }
    this->rebuild_drawable_list( );
    for ( int i = 0; i < this->num_drawable; i++ ) {
        Chunk &chunk = *this->drawable_chunks[ i ];
        if ( chunk.needs_repopulation && !( chunk.cached_cull_normal && chunk.cached_cull_reflect ) ) {
            chunk.unprogram_terrain( );
            chunk.calculate_populated_blocks( );
            chunk.program_terrain( );
            chunk.needs_repopulation = false;
            this->num_remeshed_this_frame++;
        }
    }
    this->rebuild_drawable_list( );
}

void ChunkLoader::rebuild_drawable_list( ) {
    this->num_drawable = 0;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( !chunk.is_loading && chunk.should_render ) {
            this->drawable_chunks[ this->num_drawable++ ] = &chunk;
        }
    }
}
// float chunk_diameter = ( CHUNK_SIZE + 1 ) * 1.73205080757; // sqrt(3)
float chunk_diameter = ( CHUNK_SIZE_Z + 1 ) * 1.73205080757; // sqrt(3)

void ChunkLoader::calculate_cull( const glm::mat4 &mvp, const bool saveAsReflection, const glm::ivec3 &renderOrigin ) const {
    for ( int i = 0; i < this->num_drawable; i++ ) {
        int final_is_visible;
        Chunk *chunk = this->drawable_chunks[ i ];
        if constexpr ( CULL_NON_VISIBLE ) {
            glm::vec4 chunk_coords = glm::vec4( chunk->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 - renderOrigin, 1 );
            glm::vec4 result_v = mvp * chunk_coords;
            const float adjusted_diameter = chunk_diameter / fabsf( result_v.w );

            result_v.x /= result_v.w;
            result_v.y /= result_v.w;
            result_v.z /= result_v.w;
            if ( fabsf( result_v.x ) > 1 + adjusted_diameter || //
                 fabsf( result_v.y ) > 1 + adjusted_diameter || //
                 fabsf( result_v.z ) > 1 + adjusted_diameter ) {
                final_is_visible = 0;
            } else {
                final_is_visible = 1;
            }
        }
        if ( saveAsReflection ) {
            chunk->cached_cull_reflect = !final_is_visible;
        } else {
            chunk->cached_cull_normal = !final_is_visible;
        }
    }
}

int shouldInc = 0;
int showRotation = 0;
void ChunkLoader::draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, bool reflect_only, bool draw_reflect, bool use_frame_buffer ) {
    Shader &shader = this->shader;
    shader.set_uniform_mat4f( "u_MVP", mvp );

    // pr_debug( "Drawing %d chunks", this->numLoadedChunks );
    for ( int renderOrder = LAST_RENDER_ORDER - 1; renderOrder > 0; renderOrder-- ) {
        if ( reflect_only == ( renderOrder == RenderOrder_Water ) ) {
            shader.set_uniform1f( "u_shouldDiscardAlpha", renderOrder != RenderOrder_Water && renderOrder != RenderOrder_Translucent);
            // Set texture wrap mode once per render order instead of per chunk draw call.
            if ( renderOrder == RenderOrder_Flowers ) {
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            } else {
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
                glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );
            }
            for ( int i = 0; i < this->num_drawable; i++ ) {
                Chunk &chunk = *this->drawable_chunks[ i ];
                if ( draw_reflect ) {
                    if ( !chunk.cached_cull_reflect ) {
                        chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect );
                    }
                } else {
                    if ( !chunk.cached_cull_normal ) {
                        chunk.draw( renderer, texture, shader, static_cast<RenderOrder>( renderOrder ), draw_reflect );
                    }
                }
            }
        }
    }
    if constexpr ( DEBUG_CYCLE_AUTO_ROTATING_BLOCKS ) {
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

void ChunkLoader::cleanup( MapStorage &map_storage ) {
    this->terrain_loading_thread.stop( );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( !chunk.is_loading ) {
            chunk.persist( map_storage );
        }
        chunk.destroy( );
    }
    free( this->chunkArray );
    free( this->drawable_chunks );
}