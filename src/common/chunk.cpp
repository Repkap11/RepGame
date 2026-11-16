#include <string.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/chunk_render.hpp"
#include "common/dimension_profile.hpp"
#include "common/fluid.hpp"
#include "common/light.hpp"
#include "common/map_gen.hpp"
#include "common/mob_types.hpp"
#include "common/net/packet.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/mesh_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/structure_gen.hpp"
#include "common/rail.hpp"
#include "common/repeater.hpp"
#if defined( REPGAME_BUILD_WITH_HIP )
#include "linux/hip/mesh_gen.hpp"
#endif

// Phase-1 meshing (per-cell visibility + packed lighting) is single-source:
// the same dialect text below also builds the HIP kernel and the WGSL
// WebGPU kernel — see mesh_gen_shared.hpp / mesh_gen_fns.hpp /
// mesh_vis_logic.hpp.
#define MG_QUAL static inline
#include "common/mesh_gen_fns.hpp"
#include "common/block_mesh_emit.hpp"

// Two quads crossing through the block center, drawn both-sided. They index
// the dedicated flower verts at the end of vd_data_solid (24-31), whose tex_x
// window is shifted half a texel so the quads' shared crossing column lands
// on a texel centre instead of a transparent column boundary. The first
// diagonal's verts are all FACE_BACK and the second's all FACE_FRONT, so
// each plane samples one texture slot (chains use that for the
// links-interlocked second strip).
constexpr static unsigned int ib_data_flowers[] = {
    26, 24, 25, // Back, Back, Back
    25, 24, 26, //
    26, 24, 27, // Back, Back, Back
    27, 24, 26, //

    28, 29, 30, // Front, Front, Front
    30, 29, 28, //
    28, 29, 31, // Front, Front, Front
    31, 29, 28, //
};

// ---- Shared index buffer --------------------------------------------------
// A chunk's index data is a pure function of its 6-bit face-visibility mask
// (which cube faces point toward the player), so instead of every chunk owning
// per-layer GL buffers — re-uploaded whenever a center plane crossing flipped
// a chunk's visibility row — all variants live in one shared element buffer
// and each layer stores a {byte offset, element count} pair. calculate_sides
// becomes pure CPU (~free) and can run on any thread.

// Mask bits in the order the old per-chunk buffers appended faces.
#define VIS_FRONT 1u
#define VIS_RIGHT 2u
#define VIS_BACK 4u
#define VIS_LEFT 8u
#define VIS_TOP 16u
#define VIS_BOTTOM 32u

static IndexBuffer s_chunk_ib;
static bool s_chunk_ib_ready = false;
static std::vector<unsigned int> s_ib_data;
static unsigned int s_ib_opaque_off[ 64 ], s_ib_opaque_cnt[ 64 ];
static unsigned int s_ib_water_off[ 4 ], s_ib_water_cnt[ 4 ];
static unsigned int s_ib_translucent_off[ 2 ]; // [0]=normal, [1]=reflect — 72 elements each
static unsigned int s_ib_flowers_off;
static bool s_ib_tables_ready = false;

// Pure CPU: fills the variant tables and s_ib_data. Bit order within each
// variant matches the old append order: front, right, back, left, top, bottom.
static void chunk_ib_init_tables( ) {
    if ( s_ib_tables_ready ) {
        return;
    }
    s_ib_tables_ready = true;
    static const int face_for_bit[ 6 ] = { FACE_FRONT, FACE_RIGHT, FACE_BACK, FACE_LEFT, FACE_TOP, FACE_BOTTOM };
    const auto append_face = [&]( const unsigned int *src ) {
        s_ib_data.insert( s_ib_data.end( ), src, src + 6 );
    };
    for ( unsigned int mask = 0; mask < 64; mask++ ) {
        s_ib_opaque_off[ mask ] = static_cast<unsigned int>( s_ib_data.size( ) ) * sizeof( unsigned int );
        unsigned int n = 0;
        for ( int bit = 0; bit < 6; bit++ ) {
            if ( mask & ( 1u << bit ) ) {
                append_face( ib_data_solid + 6 * face_for_bit[ bit ] );
                n += 6;
            }
        }
        s_ib_opaque_cnt[ mask ] = n;
    }
    // Translucent always draws all six faces; the reflect variant swaps the
    // top/bottom faces because the mirrored world flips y.
    static const int trans_order[ 2 ][ 6 ] = {
        { FACE_FRONT, FACE_RIGHT, FACE_BACK, FACE_LEFT, FACE_TOP, FACE_BOTTOM },
        { FACE_FRONT, FACE_RIGHT, FACE_BACK, FACE_LEFT, FACE_BOTTOM, FACE_TOP },
    };
    for ( int v = 0; v < 2; v++ ) {
        s_ib_translucent_off[ v ] = static_cast<unsigned int>( s_ib_data.size( ) ) * sizeof( unsigned int );
        for ( int f = 0; f < 6; f++ ) {
            append_face( ib_data_solid + 6 * trans_order[ v ][ f ] );
        }
    }
    // Fluids draw the full sloped-cube shell: sides + bottom always (both
    // windings — a submerged surface is visible from every direction and
    // per-face culling is done by the instance's packed_lighting anyway),
    // then the camera-mask-selected top windings like before. No reflect
    // variant (the old path left ib_reflect empty, and the reflect pass
    // skips water anyway).
    for ( unsigned int mask = 0; mask < 4; mask++ ) {
        s_ib_water_off[ mask ] = static_cast<unsigned int>( s_ib_data.size( ) ) * sizeof( unsigned int );
        s_ib_data.insert( s_ib_data.end( ), ib_data_water + 6 * IB_POSITION_WATER_SIDES, ib_data_water + 6 * IB_POSITION_WATER_TOP );
        unsigned int n = 6 * ( IB_POSITION_WATER_TOP - IB_POSITION_WATER_SIDES );
        if ( mask & 1u ) {
            append_face( ib_data_water + 6 * IB_POSITION_WATER_TOP );
            n += 6;
        }
        if ( mask & 2u ) {
            append_face( ib_data_water + 6 * IB_POSITION_WATER_BOTTOM );
            n += 6;
        }
        s_ib_water_cnt[ mask ] = n;
    }
    s_ib_flowers_off = static_cast<unsigned int>( s_ib_data.size( ) ) * sizeof( unsigned int );
    s_ib_data.insert( s_ib_data.end( ), ib_data_flowers, ib_data_flowers + sizeof( ib_data_flowers ) / sizeof( ib_data_flowers[ 0 ] ) );
}

// GL upload of the shared buffer — idempotent, called from ensure_gl_init
// (which already runs on the context-owning render thread).
static void chunk_ib_upload( ) {
    chunk_ib_init_tables( );
    if ( s_chunk_ib_ready ) {
        return;
    }
    s_chunk_ib.init( );
    s_chunk_ib.set_data( s_ib_data.data( ), static_cast<unsigned int>( s_ib_data.size( ) ) );
    s_chunk_ib_ready = true;
}

void Chunk::calculate_sides( const glm::ivec3 &center_next ) {
    this->ensure_layers( );
    chunk_ib_init_tables( );
    const unsigned int mask = chunk_face_mask( this->chunk_pos, center_next );
    this->sides_mask = mask;
    // The reflected world flips y: the reflect variant swaps top/bottom.
    const unsigned int mask_reflect = ( mask & ~( VIS_TOP | VIS_BOTTOM ) ) | ( ( mask & VIS_TOP ) << 1 ) | ( ( mask & VIS_BOTTOM ) >> 1 );

    RenderLayer &opaque = this->layers[ RenderOrder_Opaque ];
    opaque.ib_offset[ 0 ] = s_ib_opaque_off[ mask ];
    opaque.ib_count[ 0 ] = s_ib_opaque_cnt[ mask ];
    opaque.ib_offset[ 1 ] = s_ib_opaque_off[ mask_reflect ];
    opaque.ib_count[ 1 ] = s_ib_opaque_cnt[ mask_reflect ];

    RenderLayer &translucent = this->layers[ RenderOrder_Translucent ];
    translucent.ib_offset[ 0 ] = s_ib_translucent_off[ 0 ];
    translucent.ib_offset[ 1 ] = s_ib_translucent_off[ 1 ];
    translucent.ib_count[ 0 ] = translucent.ib_count[ 1 ] = IB_SOLID_SIZE;

    RenderLayer &water = this->layers[ RenderOrder_Water ];
    const unsigned int water_mask = ( mask & ( VIS_TOP | VIS_BOTTOM ) ) >> 4;
    water.ib_offset[ 0 ] = s_ib_water_off[ water_mask ];
    water.ib_count[ 0 ] = s_ib_water_cnt[ water_mask ];
    water.ib_offset[ 1 ] = 0;
    water.ib_count[ 1 ] = 0;

    RenderLayer &flowers = this->layers[ RenderOrder_Flowers ];
    flowers.ib_offset[ 0 ] = flowers.ib_offset[ 1 ] = s_ib_flowers_off;
    flowers.ib_count[ 0 ] = flowers.ib_count[ 1 ] = render_order_ib_size( RenderOrder_Flowers );
}

int Chunk::get_coords_from_index( const int index, int &out_x, int &out_y, int &out_z ) {
    const int y = ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1;
    const int x = ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1;
    const int z = ( index % ( CHUNK_SIZE_INTERNAL_Z ) ) - 1;
    // return 1;
    const int result = x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE_X && y < CHUNK_SIZE_Y && z < CHUNK_SIZE_Z;
    out_x = x;
    out_y = y;
    out_z = z;
    return result;
}

// The shared block vertex buffers and layouts are identical for every chunk, so
// they are stashed here during ChunkLoader::init and reused when each chunk
// lazily creates its GL objects on first load (see ensure_gl_init). Deferring
// GL object creation out of the startup loop avoids ~18k * 20 glGen* calls
// blocking the first frame.
static const VertexBuffer *s_vb_block_solid = nullptr;
static const VertexBuffer *s_vb_block_water = nullptr;
static const VertexBufferLayout *s_vbl_block = nullptr;
static const VertexBufferLayout *s_vbl_coords = nullptr;

// layers[] (chunk_render.hpp) is heap-allocated so the renderer headers
// stay out of chunk.hpp. calloc matches the old embedded-array zero state:
// num_instances 0, populated_blocks nullptr, GL objects untouched until
// ensure_gl_init calls their init(). First use allocates — real slots
// mesh (calculate_populated_blocks) before they ever draw.
void Chunk::ensure_layers( ) {
    if ( this->layers == nullptr ) {
        this->layers = static_cast<RenderLayer *>( calloc( LAST_RENDER_ORDER, sizeof( RenderLayer ) ) );
    }
}

void Chunk::init( const VertexBuffer &vb_block_solid, const VertexBuffer &vb_block_water, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords ) {
    s_vb_block_solid = &vb_block_solid;
    s_vb_block_water = &vb_block_water;
    s_vbl_block = &vbl_block;
    s_vbl_coords = &vbl_coords;
    // GL objects and the blocks array are created lazily (see ensure_gl_init /
    // load_terrain) so the startup loop over MAX_LOADED_CHUNKS doesn't block
    // the first frame with tens of thousands of glGen*/calloc calls.
    this->gl_initialized = 0;
    this->sides_mask = 0;
}

// Recompute this slot's ChunkLoader::slot_want byte from the truth fields.
// Called by set_load_state/set_should_render and after every render-thread
// num_instances write (program/unprogram/release_terrain) so the loader's
// membership mirror can never go stale. Render thread only: slot_want is
// written here and read by rebuild_drawable_list, both render-side, while
// calculate_populated_blocks also runs on terrain workers (load_terrain) —
// its num_instances writes are covered by the program_terrain refresh that
// follows on the render thread. No-op on hand-built chunks (slot_flags
// null).
void Chunk::refresh_slot_flags( ) {
    if ( this->slot_flags == nullptr ) {
        return;
    }
    uint8_t flags = 0;
    if ( this->load_state == CHUNK_LIVE && this->should_render ) {
        flags |= SLOT_DRAWABLE;
        if ( this->layers != nullptr && //
             ( this->layers[ RenderOrder_Translucent ].num_instances != 0 || //
               this->layers[ RenderOrder_Water ].num_instances != 0 ) ) {
            flags |= SLOT_BLENDED;
        }
    }
    *this->slot_flags = flags;
}

void Chunk::ensure_gl_init( ) {
    if ( this->gl_initialized ) {
        return;
    }
    this->gl_initialized = 1;
    this->ensure_layers( );
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        const VertexBuffer *vb_prt;
        switch ( renderOrder ) {
            case RenderOrder_Water:
                vb_prt = s_vb_block_water;
                break;
            default:
                vb_prt = s_vb_block_solid;
        }
        const VertexBuffer &vb = *vb_prt;

        RenderLayer &renderLayer = this->layers[ renderOrder ];
        renderLayer.vb_coords.init( );
        renderLayer.va.init( );
        renderLayer.va.add_buffer( vb, *s_vbl_block );
        renderLayer.va.add_buffer( renderLayer.vb_coords, *s_vbl_coords );
    }
    // Index data comes from the shared per-mask buffer — upload it once.
    chunk_ib_upload( );
}

void Chunk::destroy( ) {
    this->release_terrain( );
}

// Free every owned buffer so the slot holds no terrain data — parked chunks
// take this path (chunk_loader.cpp) so memory follows the active window
// rather than the torus capacity, and destroy() shares it. Render thread
// only (deletes GL objects); the pooled buffers go to thread-local free
// lists, so a light worker still holding a stale pointer from a just-
// finished job writes into a recycled buffer — the same pre-existing window
// as the slot-reposition path, where load_terrain frees under a worker.
void Chunk::release_terrain( ) {
    // should_render=0, occlusion-probe reset, populated_blocks frees.
    this->unprogram_terrain( );
    if ( this->layers != nullptr ) {
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            RenderLayer &layer = this->layers[ renderOrder ];
            layer.num_instances = 0;
            layer.vb_coords.destroy( );
            layer.va.destroy( );
        }
        free( this->layers );
        this->layers = nullptr;
    }
    this->refresh_slot_flags( );
    // Cleared so ensure_gl_init rebuilds the destroyed layer objects on
    // reactivation.
    this->gl_initialized = 0;
    if constexpr ( REMEMBER_BLOCKS ) {
        this->free_block_storage( );
        free( this->edited );
        this->edited = nullptr;
    }
    chunk_free_light( this->light.volume );
    this->light.volume = nullptr;
    this->light.uniform = 0;
    chunk_free_light_columns( this->light.columns );
    this->light.columns = nullptr;
    chunk_free_air_mask( this->light.air_mask );
    this->light.air_mask = nullptr;
    chunk_free_air_mask( this->light.shade_mask );
    this->light.shade_mask = nullptr;
    if ( this->light.texture ) {
        glDeleteTextures( 1, &this->light.texture );
        this->light.texture = 0;
    }
    if ( this->occl.query ) {
        glDeleteQueries( 1, &this->occl.query );
        this->occl.query = 0;
    }
    // unprogram_terrain keeps occl.pending (the query object survives a
    // reposition); here the query is gone, so the pending flag must be too.
    this->occl.pending = 0;
    // Light bookkeeping for a volume that no longer exists. light_pending_
    // listed / light_upload_listed stay whatever they were — their drain
    // loops own those flags and skip non-live chunks after clearing them.
    this->light.pending_count = 0;
    this->light.reseed = 0;
    this->light.stale_clear( );
    this->light.upload_defers = 0;
    this->light.uploaded_once = 0;
    // Staged upload in flight: bump the generation so the worker's publish
    // CAS fails and it frees its own buffer; a ready buffer (status==2) is
    // render-owned and returned to the pool. The pool lives in light.cpp.
    light_reset_staging( this );
    this->light.tex_mode = 0;
    this->light.tex_flip = 0;
    this->light.finalized.store( 0, std::memory_order_relaxed );
    this->light.write_tick.store( 0, std::memory_order_relaxed );
    this->light.dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    this->needs_repopulation = 0;
    this->is_empty_chunk = true;
    // Callers persist before releasing, so nothing here is unsaved.
    this->dirty = 0;
    this->edits_read_failed = 0;
    this->applying_edits = 0;
    this->mpload_diff_ms = 0;
    this->containers.chests.clear( );
    this->containers.chests_loaded = 0;
    this->containers.furnaces.clear( );
    this->containers.furnaces_loaded = 0;
    this->containers.signs.clear( );
    this->containers.signs_loaded = 0;
    this->sidecars.mobs.clear( );
    this->sidecars.mobs_file_present = 0;
    this->sidecars.mobs_spawned = 0;
    this->sidecars.mobs_removed.clear( );
    this->sidecars.player_edited = 0;
    this->sidecars.items.clear( );
    this->sidecars.items_file_present = 0;
    this->sidecars.vehicles.clear( );
    this->sidecars.vehicles_file_present = 0;
    // Every owned buffer was dropped — invalidate any async mesh snapshot
    // taken before this release.
    this->cell_version.fetch_add( 1u, std::memory_order_relaxed );
}

void Chunk::draw( const Renderer &renderer, const Texture &texture, Shader &shader, RenderOrder renderOrder, bool draw_reflect, const glm::vec3 &render_origin, int light_base_loc ) {
    if ( this->layers == nullptr ) {
        return;
    }
    const RenderLayer &renderLayer = this->layers[ renderOrder ];
    if ( this->should_render && renderLayer.num_instances != 0 ) {
        if ( this->load_state ) {
            pr_debug( "Error, attempting to render loading chunk" );
        }
        const unsigned int ib_pass = draw_reflect ? 1 : 0;
        // Bind this chunk's light volume to the shared light texture unit.
        // u_LightBase is the chunk's world origin in the rebased (u_Origin)
        // coordinate frame the shader uses; see chunk_fragment.glsl.
        extern long long light_dbg_us_bind;
        const long long t_bind = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
        this->light_ensure_texture( );
        if ( this->light.texture != 0 && !this->light.tex_stale.load( std::memory_order_relaxed ) ) {
            // The light unit's glActiveTexture is hoisted to
            // ChunkLoader::draw — nothing between chunk draws reselects it.
            glBindTexture( GL_TEXTURE_3D, this->light.texture );
            const glm::vec3 light_base = glm::vec3( this->chunk_pos * CHUNK_SIZE_I ) - render_origin;
            // Direct glUniform3f on the cached location — set_uniform3f does a
            // glGetUniformLocation every call, which costs ~us per chunk per
            // render pass (~6000 lookups/frame).
            glUniform3f( light_base_loc, light_base.x, light_base.y, light_base.z );
        } else {
            // No texture yet (create deferred by the per-frame budget) or
            // the texture holds a previous position's field (tex_stale) —
            // draw the shared full-sky stand-in instead of whatever volume
            // the previous chunk left bound.
            glBindTexture( GL_TEXTURE_3D, light_fallback_texture( ) );
        }
        light_dbg_us_bind += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( ) - t_bind;
        // Texture wrap mode is now set once per render order in ChunkLoader::draw.
        renderer.draw( renderLayer.va, s_chunk_ib, shader, renderLayer.num_instances,
                       renderLayer.ib_count[ ib_pass ], renderLayer.ib_offset[ ib_pass ] );
    }
}

BlockState Chunk::get_block( const glm::ivec3 &pos ) const {
    if constexpr ( !REMEMBER_BLOCKS ) {
        return BLOCK_STATE_AIR;
    }
    if ( this->cells == nullptr ) {
        // nullptr cells means the whole chunk is AIR.
        return BLOCK_STATE_AIR;
    }
    if ( pos.x >= CHUNK_SIZE_X + 1 || //
         pos.y >= CHUNK_SIZE_Y + 1 || //
         pos.z >= CHUNK_SIZE_Z + 1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    if ( pos.x < -1 || //
         pos.y < -1 || //
         pos.z < -1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    return this->state_at( get_index_from_coords( pos ) );
}

// Cells/extras only — `edited` is deliberately not freed here: the
// all-air fold at the end of load_terrain calls this while the edit
// bits must survive to the next persist. Owners of `edited` are
// load_terrain (slot reuse), release_terrain, free_generated_buffers
// and inspect_load's teardown.
void Chunk::free_block_storage( ) {
    chunk_free_cells( this->cells );
    this->cells = nullptr;
    chunk_free_extras( this->extras );
    this->extras = nullptr;
}

void Chunk::free_generated_buffers( ) {
    chunk_free_cells( this->cells );
    this->cells = nullptr;
    chunk_free_extras( this->extras );
    this->extras = nullptr;
    free( this->edited );
    this->edited = nullptr;
    chunk_free_air_mask( this->light.air_mask );
    this->light.air_mask = nullptr;
    chunk_free_air_mask( this->light.shade_mask );
    this->light.shade_mask = nullptr;
}

void Chunk::set_block( const glm::ivec3 &pos, const BlockState &blockState ) const {
    if ( pos.x >= CHUNK_SIZE_X + 1 || //
         pos.y >= CHUNK_SIZE_Y + 1 || //
         pos.z >= CHUNK_SIZE_Z + 1 ) {
        return;
    }
    if ( pos.x < -1 || //
         pos.y < -1 || //
         pos.z < -1 ) {
        return;
    }
    if ( this->cells == nullptr ) {
        // All-air chunk: an AIR write changes nothing, so only materialize
        // the array when the new state actually differs from AIR.
        if ( BlockStates_equal( BLOCK_STATE_AIR, blockState ) ) {
            return;
        }
        this->cells = chunk_alloc_cells( );
        // The fresh cells[] is calloc'd = all AIR = all non-opaque, so its
        // air mask starts fully set; the write below stamps its own cell.
        if ( light_mipmaps_enabled( ) ) {
            if ( this->light.air_mask == nullptr ) {
                this->light.air_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.air_mask, 0xFF, CHUNK_BLOCK_SIZE / 8 );
            if ( this->light.shade_mask == nullptr ) {
                this->light.shade_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.shade_mask, 0, CHUNK_BLOCK_SIZE / 8 );
        }
    }

    const int index = get_index_from_coords( pos );
    const BlockState old_state = this->state_at( index );
    if ( block_is_chest( old_state.id ) && !block_is_chest( blockState.id ) ) {
        this->containers.chests.erase( index );
    }
    if ( block_is_furnace( old_state.id ) && !block_is_furnace( blockState.id ) ) {
        this->containers.furnaces.erase( index );
    }
    if ( block_is_sign( old_state.id ) && !block_is_sign( blockState.id ) ) {
        this->containers.signs.erase( index );
    }
    this->store_state( index, block_state_normalize( old_state, blockState ) );
    light_air_mask_set( *this, index, this->id_at( index ) );
    this->mark_edited( index );
}

int Chunk::set_block_by_index_if_different( int index, const BlockState *blockState ) {
    // Defensive bounds check: index comes from the network (multiplayer chunk
    // diffs). An out-of-range value would write to arbitrary chunk memory and
    // corrupt the world. Drop the update silently rather than crash.
    if ( index < 0 || index >= CHUNK_BLOCK_SIZE ) {
        pr_debug( "Ignoring out-of-range block index:%d (max:%d)", index, CHUNK_BLOCK_SIZE );
        return 0;
    }
    if ( this->cells == nullptr ) {
        // All-air chunk: an AIR write is a no-op; anything else allocates.
        if ( BlockStates_equal( BLOCK_STATE_AIR, *blockState ) ) {
            return 0;
        }
        this->cells = chunk_alloc_cells( );
        // Same as set_block: fresh calloc'd cells[] = all non-opaque.
        if ( light_mipmaps_enabled( ) ) {
            if ( this->light.air_mask == nullptr ) {
                this->light.air_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.air_mask, 0xFF, CHUNK_BLOCK_SIZE / 8 );
            if ( this->light.shade_mask == nullptr ) {
                this->light.shade_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.shade_mask, 0, CHUNK_BLOCK_SIZE / 8 );
        }
    }
    const BlockState oldBlockState = this->state_at( index );
    if ( BlockStates_equal( oldBlockState, *blockState ) ) {
        return 0;
    }
    if ( block_is_chest( oldBlockState.id ) && !block_is_chest( blockState->id ) ) {
        this->containers.chests.erase( index );
    }
    if ( block_is_furnace( oldBlockState.id ) && !block_is_furnace( blockState->id ) ) {
        this->containers.furnaces.erase( index );
    }
    if ( block_is_sign( oldBlockState.id ) && !block_is_sign( blockState->id ) ) {
        this->containers.signs.erase( index );
    }
    this->store_state( index, *blockState );
    light_air_mask_set( *this, index, blockState->id );
    this->mark_edited( index );
    this->dirty = true;
    this->needs_repopulation = true;
    // Record the change for the light engine; converted to a world-space
    // recheck seed by the lighting thread (ChunkLoader::light_drain_pending).
    if ( this->light.pending_count < static_cast<int>( sizeof( this->light.pending ) / sizeof( this->light.pending[ 0 ] ) ) ) {
        this->light.pending[ this->light.pending_count++ ] = index;
    } else {
        this->light.reseed = 1;
    }
    light_pending_enqueue( *this );
    this->is_empty_chunk = false;
    // Ensure the chunk enters the drawable list so the remeshing loop visits
    // it. An empty chunk has should_render == 0; without this, a block placed
    // in it (e.g. via a multiplayer chunk diff) wouldn't render until reload.
    this->set_should_render( 1 );
    return 1;
}

void Chunk::persist( MapStorage &map_storage ) const {
    if constexpr ( REMEMBER_BLOCKS ) {
        map_storage.persist_chunk( *this );
    }
}

void Chunk::program_terrain( ) {
    // REPGAME_VALIDATE_INSTANCES=1: sanity-check every BlockCoords before
    // upload. Catches host-memory corruption (stray DMA writes from
    // concurrent GPU compute) before it reaches the GPU as garbage
    // instances / stray fragments.
    static const int validate_instances = env_is_set( "REPGAME_VALIDATE_INSTANCES" );
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        RenderLayer &renderLayer = this->layers[ renderOrder ];
        // populated_blocks is null for a re-finalized chunk that already
        // uploaded its mesh (a dirty park that rode the work queue back
        // in-window before its data was released) — the VBO is intact, so
        // there is nothing new to upload.
        if ( renderLayer.num_instances != 0 && renderLayer.populated_blocks != nullptr ) {
            if ( validate_instances ) {
                static int reported = 0;
                const int ox = this->chunk_pos.x * CHUNK_SIZE_X;
                const int oy = this->chunk_pos.y * CHUNK_SIZE_Y;
                const int oz = this->chunk_pos.z * CHUNK_SIZE_Z;
                for ( int i = 0; i < renderLayer.num_instances && reported < 40; i++ ) {
                    const BlockCoords &b = renderLayer.populated_blocks[ i ];
                    int bad = b.mesh_x < 1 || b.mesh_x > 32 || b.mesh_y < 1 || b.mesh_y > 32 || b.mesh_z < 1 || b.mesh_z > 32;
                    bad |= ( b.face_shift & BLOCK_COORDS_ROTATION_MASK ) > 7u || ( b.face_shift & ~( BLOCK_COORDS_ROTATION_MASK | BLOCK_COORDS_INSIDE_OUT | BLOCK_COORDS_HALF_PIXEL | BLOCK_COORDS_FLUID | BLOCK_COORDS_SLOPE ) ) != 0;
                    bad |= ( b.x < ox - 1.0f ) || ( b.x > ox + CHUNK_SIZE_X + 1.0f );
                    bad |= ( b.y < oy - 1.0f ) || ( b.y > oy + CHUNK_SIZE_Y + 1.0f );
                    bad |= ( b.z < oz - 1.0f ) || ( b.z > oz + CHUNK_SIZE_Z + 1.0f );
                    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                        bad |= b.face[ f ] >= TEXTURE_COUNT;
                    }
                    if ( bad ) {
                        reported++;
                        pr_debug( "VALIDATE: bad instance chunk=(%d,%d,%d) order=%d i=%d mesh=(%d,%d,%d) pos=(%.1f,%.1f,%.1f) faces=(%u,%u,%u,%u,%u,%u) shift=%u",
                                  this->chunk_pos.x, this->chunk_pos.y, this->chunk_pos.z, renderOrder, i, b.mesh_x, b.mesh_y, b.mesh_z, b.x, b.y, b.z,
                                  b.face[ 0 ], b.face[ 1 ], b.face[ 2 ], b.face[ 3 ], b.face[ 4 ], b.face[ 5 ], b.face_shift );
                    }
                }
            }
            renderLayer.vb_coords.set_data( renderLayer.populated_blocks, sizeof( BlockCoords ) * renderLayer.num_instances );
            this->set_should_render( 1 );
        }
        if ( renderOrder == RenderOrder_Translucent ) {
            // Keep the CPU copy: sort_translucent_instances re-orders it
            // back-to-front for the camera and re-uploads, fixing
            // far-surface-over-near blending inside a chunk.
            renderLayer.translucent_sort_valid = 0;
        } else {
            free( renderLayer.populated_blocks );
            renderLayer.populated_blocks = nullptr;
        }
    }
    this->refresh_slot_flags( );
}

void Chunk::unprogram_terrain( ) {
    this->set_should_render( 0 );
    // Slot is moving to a new chunk_pos: assume visible until its own
    // probes say otherwise. occl.pending may still be in flight; its stale
    // result shifts occl.fails by at most one, under the hide threshold.
    this->occl.reset_streak( );
    this->occl.fails_gen = 0;
    if ( this->layers == nullptr ) {
        return;
    }
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        RenderLayer &renderLayer = this->layers[ renderOrder ];
        if ( renderLayer.populated_blocks ) {
            free( renderLayer.populated_blocks );
        }
        renderLayer.populated_blocks = nullptr;
    }
}

// In-chunk painter order for the Translucent pass: with depth writes off,
// VBO instance order is the compositing order, and emission order (y, x, z)
// puts the far portal/glass surface last whenever the camera looks the
// wrong way — it then blends ON TOP of the nearer surface. Chunks already
// sort back-to-front by view depth; do the same per instance. Depth along
// view_dir — not radial distance — is the right key: two screen-overlapping
// surfaces separate mostly along the view, so their projected order is
// stable under small camera moves, while radial distance flips whenever a
// slightly-off-axis far surface sits nearer in absolute terms.
// Re-sorts are rate-limited by a position and a rotation hysteresis; small
// camera moves and sub-degree rotations skip the sort + re-upload.
static constexpr float TRANSLUCENT_SORT_RESORT_DIST = 1.0f;
// ~5.7 degrees of view rotation before a re-sort: pairs whose projected
// depth differs meaningfully need a much larger swing to swap, and pairs
// that swap inside this band barely overlap on screen.
static constexpr float TRANSLUCENT_SORT_RESORT_DOT = 0.995f;
void Chunk::sort_translucent_instances( const glm::vec3 &cam_abs, const glm::vec3 &view_dir ) {
    if ( this->layers == nullptr ) {
        return;
    }
    RenderLayer &layer = this->layers[ RenderOrder_Translucent ];
    if ( !this->gl_initialized || layer.num_instances == 0 || layer.populated_blocks == nullptr ) {
        return;
    }
    const glm::vec3 moved = cam_abs - layer.translucent_sort_cam;
    if ( layer.translucent_sort_valid && //
         glm::dot( moved, moved ) <= TRANSLUCENT_SORT_RESORT_DIST * TRANSLUCENT_SORT_RESORT_DIST &&
         glm::dot( view_dir, layer.translucent_sort_dir ) >= TRANSLUCENT_SORT_RESORT_DOT ) {
        return;
    }
    // Back-to-front: largest view depth first so it composites under nearer
    // surfaces. Ties (equal depth) break lexicographically on position so
    // the order is deterministic like chunk_sort_key_less.
    std::sort( layer.populated_blocks, layer.populated_blocks + layer.num_instances,
               [ &cam_abs, &view_dir ]( const BlockCoords &a, const BlockCoords &b ) {
                   const float da = glm::dot( glm::vec3( a.x, a.y, a.z ) - cam_abs, view_dir );
                   const float db = glm::dot( glm::vec3( b.x, b.y, b.z ) - cam_abs, view_dir );
                   if ( da != db ) {
                       return da > db;
                   }
                   if ( a.x != b.x ) {
                       return a.x < b.x;
                   }
                   if ( a.y != b.y ) {
                       return a.y < b.y;
                   }
                   return a.z < b.z;
               } );
    layer.vb_coords.set_data( layer.populated_blocks, sizeof( BlockCoords ) * layer.num_instances );
    layer.translucent_sort_cam = cam_abs;
    layer.translucent_sort_dir = view_dir;
    layer.translucent_sort_valid = 1;
}

// Full-shell index-buffer ranges for one render order — what a chunk whose
// visibility mask is all-ones draws. Used by the block-body instance batch
// (ChunkLoader::draw) so movers render the same face set the mesher emits;
// water has no reflect range, matching per-chunk behaviour.
void chunk_ib_full_face_range( const RenderOrder order, const bool draw_reflect, unsigned *offset, unsigned *count ) {
    chunk_ib_init_tables( );
    const unsigned ib_pass = draw_reflect ? 1 : 0;
    switch ( order ) {
        case RenderOrder_Opaque:
            *offset = s_ib_opaque_off[ 63 ];
            *count = s_ib_opaque_cnt[ 63 ];
            return;
        case RenderOrder_Translucent:
            *offset = s_ib_translucent_off[ ib_pass ];
            *count = IB_SOLID_SIZE;
            return;
        case RenderOrder_Water:
            *offset = s_ib_water_off[ 3 ];
            *count = draw_reflect ? 0 : s_ib_water_cnt[ 3 ];
            return;
        default:
            *offset = s_ib_flowers_off;
            *count = render_order_ib_size( RenderOrder_Flowers );
            return;
    }
}

const IndexBuffer &chunk_ib_buffer( ) {
    return s_chunk_ib;
}
