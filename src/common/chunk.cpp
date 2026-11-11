#include <string.h>
#include <chrono>
#include <random>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/chunk_render.hpp"
#include "common/fluid.hpp"
#include "common/map_gen.hpp"
#include "common/mob_types.hpp"
#include "common/net/packet.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/mesh_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/structure_gen.hpp"
#if defined( REPGAME_BUILD_WITH_HIP )
#include "linux/hip/mesh_gen.hpp"
#endif

// Phase-1 meshing (per-cell visibility + packed lighting) is single-source:
// the same dialect text below also builds the HIP kernel and the WGSL
// WebGPU kernel — see mesh_gen_shared.hpp / mesh_gen_fns.hpp /
// mesh_vis_logic.hpp.
#define MG_QUAL static inline
#include "common/mesh_gen_fns.hpp"

// Two quads crossing through the block center, drawn both-sided. They index
// the dedicated flower verts at the end of vd_data_solid (24-31), whose tex_x
// window is shifted half a texel so the quads' shared crossing column lands
// on a texel centre instead of a transparent column boundary.
constexpr static unsigned int ib_data_flowers[] = {
    26, 24, 25, // Right, Back, Right
    25, 24, 26, //
    26, 24, 27, // Right, Back, Back
    27, 24, 26, //

    28, 29, 30, // Front, Right, Front
    30, 29, 28, //
    28, 29, 31, // Front, Right, Right
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
    this->sidecars.mobs.clear( );
    this->sidecars.mobs_file_present = 0;
    this->sidecars.mobs_spawned = 0;
    this->sidecars.mobs_removed.clear( );
    this->sidecars.player_edited = 0;
    this->sidecars.items.clear( );
    this->sidecars.items_file_present = 0;
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
        if ( this->light.texture != 0 ) {
            // The light unit's glActiveTexture is hoisted to
            // ChunkLoader::draw — nothing between chunk draws reselects it.
            glBindTexture( GL_TEXTURE_3D, this->light.texture );
            const glm::vec3 light_base = glm::vec3( this->chunk_pos * CHUNK_SIZE_I ) - render_origin;
            // Direct glUniform3f on the cached location — set_uniform3f does a
            // glGetUniformLocation every call, which costs ~us per chunk per
            // render pass (~6000 lookups/frame).
            glUniform3f( light_base_loc, light_base.x, light_base.y, light_base.z );
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

int Chunk::inspect_load( MapStorage &map_storage, const glm::ivec3 &pos, BlockState *out, uint8_t *edited_bits ) {
    Chunk *c = new Chunk( ); // value-init: POD fields zero, no GL objects
    c->chunk_pos = pos;
    c->chunk_mod = pos;
    const int existed = map_storage.check_if_chunk_exists( pos );
    c->load_terrain( map_storage );
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        out[ i ] = c->cells != nullptr ? c->state_at( i ) : BLOCK_STATE_AIR;
    }
    if ( edited_bits != nullptr ) {
        memset( edited_bits, 0, CHUNK_BLOCK_SIZE / 8 );
        if ( c->edited != nullptr ) {
            memcpy( edited_bits, c->edited, CHUNK_BLOCK_SIZE / 8 );
        }
    }
    // CPU-side teardown only: nothing here made GL objects (release_terrain
    // would call into the GL layer for the layer VertexBuffers).
    c->free_block_storage( );
    free( c->edited );
    chunk_free_light( c->light.volume );
    chunk_free_light_columns( c->light.columns );
    chunk_free_air_mask( c->light.air_mask );
    chunk_free_air_mask( c->light.shade_mask );
    if ( c->layers != nullptr ) {
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            free( c->layers[ renderOrder ].populated_blocks );
        }
        free( c->layers );
    }
    delete c;
    return existed;
}

int firstTime = 1;

#if TERRAIN_GEN_PROFILING
#include <chrono>
#include <atomic>
static std::atomic<int> profiling_chunk_count{ 0 };
static std::atomic<long long> profiling_total_us{ 0 };
static std::atomic<long long> profiling_load_disk_us{ 0 };
static std::atomic<long long> profiling_map_gen_us{ 0 };
static std::atomic<long long> profiling_structure_gen_us{ 0 };
static std::atomic<long long> profiling_populated_blocks_us{ 0 };
static std::atomic<long long> profiling_mesh_vis_light_us{ 0 };
static std::atomic<long long> profiling_mesh_greedy_us{ 0 };
static std::atomic<int> profiling_map_gen_count{ 0 };
static std::atomic<int> profiling_disk_load_count{ 0 };
static std::atomic<long long> profiling_instance_count{ 0 };
extern std::atomic<long long> terrain_persist_us;

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}
#endif

// FNV-1a mix of chunk pos, mob type and the map seed — the deterministic
// natural-spawn key (the same hash drives the spawn roll, herd size,
// positions and variants).
static uint64_t mob_spawn_seed( const glm::ivec3 &pos, int type ) {
    uint64_t h = 1469598103934665603ull;
    const uint64_t vals[] = { static_cast<uint64_t>( static_cast<uint32_t>( pos.x ) ), static_cast<uint64_t>( static_cast<uint32_t>( pos.y ) ),
                              static_cast<uint64_t>( static_cast<uint32_t>( pos.z ) ), static_cast<uint64_t>( type ), static_cast<uint64_t>( MAP_SEED ) };
    for ( uint64_t v : vals ) {
        h ^= v;
        h *= 1099511628211ull;
    }
    return h;
}

// Natural spawn roll for a freshly generated chunk: per mob type, the hash
// decides whether a herd appears; members stand on `spawn_on` column tops
// that have hitbox headroom inside the chunk.
bool Chunk::spawn_mobs( const std::set<uint8_t> *skip ) {
    if ( this->cells == nullptr ) {
        return false;
    }
    bool spawned = false;
    // Roll index of the next emitted record; skipped indices still consume
    // one so tombstones match the deterministic ordering.
    int roll_index = 0;
    const glm::ivec3 origin = this->chunk_pos * CHUNK_SIZE_I;
    for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
        const MobTypeDef &def = mob_types[ t ];
        if ( def.spawn_chance <= 0.0f ) {
            continue;
        }
        std::mt19937_64 rng( mob_spawn_seed( this->chunk_pos, t ) );
        std::uniform_real_distribution<double> roll( 0.0, 1.0 );
        if ( roll( rng ) >= def.spawn_chance ) {
            continue;
        }
        // Interior columns only, and the whole hitbox must fit inside the
        // chunk: the top colliding cell is the floor; the headroom cells
        // above it must be inside the chunk and non-colliding.
        const int headroom = static_cast<int>( ceilf( def.hitbox_h ) );
        std::vector<glm::ivec3> spots;
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                int top = -1;
                for ( int y = CHUNK_SIZE_Y - 1; y >= 0; y-- ) {
                    const BlockID id = this->id_at( Chunk::get_index_from_coords( x, y, z ) );
                    if ( id != AIR && block_definition_get_definition( id )->collides_with_player ) {
                        top = y;
                        break;
                    }
                }
                if ( top < 0 || top + headroom >= CHUNK_SIZE_Y ||
                     this->id_at( Chunk::get_index_from_coords( x, top, z ) ) != def.spawn_on ) {
                    continue;
                }
                bool fits = true;
                for ( int y = top + 1; y <= top + headroom; y++ ) {
                    const BlockID id = this->id_at( Chunk::get_index_from_coords( x, y, z ) );
                    if ( id != AIR && block_definition_get_definition( id )->collides_with_player ) {
                        fits = false;
                        break;
                    }
                }
                if ( fits ) {
                    spots.push_back( glm::ivec3( x, top + 1, z ) );
                }
            }
        }
        if ( spots.empty( ) ) {
            continue;
        }
        const int herd_want = def.herd_min + static_cast<int>( rng( ) % static_cast<uint64_t>( def.herd_max - def.herd_min + 1 ) );
        const int herd = herd_want < static_cast<int>( spots.size( ) ) ? herd_want : static_cast<int>( spots.size( ) );
        std::uniform_real_distribution<float> yaw_pick( 0.0f, 360.0f );
        for ( int i = 0; i < herd; i++ ) {
            const size_t pick = rng( ) % spots.size( );
            const glm::ivec3 spot = spots[ pick ];
            spots[ pick ] = spots.back( );
            spots.pop_back( );
            const int index = roll_index++;
            // Natural ids carry a 6-bit index; nothing past 63 can be
            // encoded, so it simply doesn't spawn.
            if ( index >= 64 || ( skip != nullptr && skip->count( static_cast<uint8_t>( index ) ) ) ) {
                continue;
            }
            MobFileRecord record = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u, 0u, 0 };
            record.type = static_cast<uint16_t>( t );
            record.variant = def.skin_count > 1 ? static_cast<uint16_t>( rng( ) % static_cast<uint64_t>( def.skin_count ) ) : 0;
            record.x = origin.x + spot.x + 0.5;
            record.y = origin.y + spot.y;
            record.z = origin.z + spot.z + 0.5;
            record.yaw = yaw_pick( rng );
            record.health = def.max_health;
            record.flags = NET_MOB_FLAG_NATURAL;
            record.net_id = net_natural_mob_id( this->chunk_pos, index );
            this->sidecars.mobs.push_back( record );
            spawned = true;
        }
    }
    return spawned;
}

void Chunk::load_terrain( MapStorage &map_storage ) {
    if constexpr ( !REMEMBER_BLOCKS ) {
        if ( this->cells == nullptr ) {
            this->cells = chunk_alloc_cells( );
        }
    }
    // A recycled slot can carry a stale extras array from its previous
    // chunk; both reload paths overwrite cells wholesale but only touch
    // extras on demand, so drop it up front (disk decode re-materializes
    // it only if the saved data actually needs it).
    chunk_free_extras( this->extras );
    this->extras = nullptr;
    // Edit bits belong to the previous position too — a recycled slot
    // must not carry them into the new chunk's file (persist already ran
    // in process_value). Kept out of free_block_storage: the all-air
    // fold below calls that while edits must survive to the next persist.
    free( this->edited );
    this->edited = nullptr;
    this->mpload_diff_ms = 0;
    // Under REMEMBER_BLOCKS, cells stays null while the chunk is all-air:
    // MapStorage::load_chunk allocates it only when a save file exists, and
    // Chunk::set_block allocates on the first real write (every existing cell
    // is AIR anyway). light.volume[] likewise stays unallocated for uniform volumes —
    // light_fill_chunk folds constant fills to Chunk::light_uniform instead.
    if ( this->light.columns == nullptr ) {
        this->light.columns = chunk_alloc_light_columns( );
    } else {
        memset( this->light.columns, 0, 3 * LIGHT_FLAGS_COUNT );
    }
    this->light.pending_count = 0;
    this->light.reseed = 0;
    this->light.stale_clear( );
    this->light.pending_listed = 0;
    this->light.upload_listed = 0;
    this->light.write_tick.store( 0, std::memory_order_relaxed );
    this->light.upload_defers = 0;
    this->light.uploaded_once = 0;
    // See release_terrain — invalidates any staged upload claimed by a
    // worker before this slot was reassigned.
    light_reset_staging( this );
    this->light.tex_mode = 0;
    this->light.tex_flip = 0;
    this->light.finalized.store( 0, std::memory_order_relaxed );
    this->light.dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    this->light.uniform = 0;
    this->is_empty_chunk = false;
#if TERRAIN_GEN_PROFILING
    long long t_start = now_us( );
    long long t_load_disk = 0, t_map_gen = 0, t_struct = 0, t_pop = 0;
    long long t0 = now_us( );
#endif
    // Saved edits apply over generated terrain, so generation always runs.
    // applying_edits keeps the structure-gen set_block calls (and the overlay
    // itself) from being recorded as runtime edits.
    this->applying_edits = 1;
    this->edits_read_failed = 0;
    ChunkEdits edits;
    const int edits_status = map_storage.load_chunk_edits( this->chunk_pos, edits );
    if ( edits_status < 0 ) {
        this->edits_read_failed = 1;
    }
    const bool flat_world = repgame_flat_world( );
#if TERRAIN_GEN_PROFILING
    t_load_disk = now_us( ) - t0;
    if ( edits_status == 1 ) profiling_disk_load_count.fetch_add( 1, std::memory_order_relaxed );
#endif
    static const bool block_world = repgame_block_world( );
    if ( block_world || flat_world ) {
        // Block-world debug layout (REPGAME_BLOCK_WORLD): solid sandstone
        // ground up to y=0, every defined block on a grid at y=1, air above.
        // Flat worlds (world_type `flat`) are the same minus the grid.
        // Chunks entirely above the layout (border included) are all air;
        // chunks below fill solid the same way as any other cell range.
        const int bw_min_y = this->chunk_pos.y * CHUNK_SIZE_Y - 1;
        if ( bw_min_y <= ( flat_world ? 0 : 2 ) ) {
            if ( this->cells == nullptr ) {
                this->cells = chunk_alloc_cells( );
            }
            const glm::ivec3 chunk_offset = this->chunk_pos * glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
            for ( int x = chunk_offset.x - 1; x < chunk_offset.x + CHUNK_SIZE_INTERNAL_X - 1; x++ ) {
                for ( int z = chunk_offset.z - 1; z < chunk_offset.z + CHUNK_SIZE_INTERNAL_Z - 1; z++ ) {
                    for ( int y = chunk_offset.y - 1; y < chunk_offset.y + CHUNK_SIZE_INTERNAL_Y - 1; y++ ) {
                        const int index = Chunk::get_index_from_coords( glm::ivec3( x, y, z ) - chunk_offset );
                        this->cells[ index ] = static_cast<BlockCell>( flat_world ? MapGen::flat_world_id( x, y, z ) : MapGen::block_world_id( x, y, z ) );
                    }
                }
            }
        } else {
            // Entirely above the layout: all air. A recycled slot can still
            // hold the previous position's cells — drop them.
            this->free_block_storage( );
            chunk_free_air_mask( this->light.air_mask );
            this->light.air_mask = nullptr;
            chunk_free_air_mask( this->light.shade_mask );
            this->light.shade_mask = nullptr;
            this->is_empty_chunk = true;
        }
        this->dirty = 0;
        goto apply_edits;
    }
    {
        // Cheap CPU-side check: if the entire chunk is above the terrain surface,
        // every block is air. Skip GPU map gen, structure gen, and meshing entirely.
        const int min_y = this->chunk_pos.y * CHUNK_SIZE_Y - 1; // include -1 border
        if ( min_y >= WATER_LEVEL ) {
            // If the chunk is above the theoretical max terrain height + the
            // reach of a tree growing on it, it's guaranteed empty with zero
            // perlin noise evaluation. The bound is derived from the terrain
            // formulas in map_gen.cpp — see MapGen::maxTerrainHeight().
            if ( (float)min_y >= MapGen::maxTerrainHeight( ) + MAPGEN_MAX_TREE_REACH ) {
                // Entire chunk is air — keep cells null instead of filling a
                // 262K-cell array with AIR. A recycled slot can still hold
                // the previous position's cells — drop them.
                this->free_block_storage( );
                chunk_free_air_mask( this->light.air_mask );
                this->light.air_mask = nullptr;
                chunk_free_air_mask( this->light.shade_mask );
                this->light.shade_mask = nullptr;
                this->is_empty_chunk = true;
                this->dirty = 0;
#if TERRAIN_GEN_PROFILING
                t_map_gen = now_us( ) - t0;
                profiling_map_gen_count.fetch_add( 1, std::memory_order_relaxed );
#endif
                goto apply_edits;
            }
            // Below the theoretical max but still above water: compute actual terrain height
            // per column to catch chunks that are empty at lower altitudes.
            const int x_start = this->chunk_pos.x * CHUNK_SIZE_X - 1;
            const int z_start = this->chunk_pos.z * CHUNK_SIZE_Z - 1;
            float max_terrain_height = -1e30f;
            for ( int x = x_start; x < x_start + CHUNK_SIZE_INTERNAL_X; x++ ) {
                for ( int z = z_start; z < z_start + CHUNK_SIZE_INTERNAL_Z; z++ ) {
                    float h = MapGen::calculateTerrainHeight( x, z );
                    if ( h > max_terrain_height ) max_terrain_height = h;
                }
            }
            // Overhang noise can add solid blocks up to OVERHANG_MAX_RISE above
            // the heightmap, and tree canopies reach a bit higher still — the
            // empty bound must include both.
            if ( (float)min_y >= max_terrain_height + OVERHANG_MAX_RISE + MAPGEN_MAX_TREE_REACH ) {
                // Entire chunk (including border) is above terrain → all air
                this->free_block_storage( );
                chunk_free_air_mask( this->light.air_mask );
                this->light.air_mask = nullptr;
                chunk_free_air_mask( this->light.shade_mask );
                this->light.shade_mask = nullptr;
                this->is_empty_chunk = true;
                this->dirty = 0;
#if TERRAIN_GEN_PROFILING
                t_map_gen = now_us( ) - t0;
                profiling_map_gen_count.fetch_add( 1, std::memory_order_relaxed );
#endif
                goto apply_edits;
            }
        }
#if TERRAIN_GEN_PROFILING
        t0 = now_us( );
#endif
        if ( this->cells == nullptr ) {
            this->cells = chunk_alloc_cells( );
        }
        // Per-chunk env checks are cached — this runs once per chunk on every
        // terrain worker, and the values can't change mid-process.
        static const bool no_hip_mapgen = env_is_set( "REPGAME_NO_HIP_MAPGEN" );
        static const bool no_webgpu_mapgen = env_is_set( "REPGAME_NO_WEBGPU_MAPGEN" );
        if ( LOAD_CHUNKS_SUPPORTS_CUDA && MapGen::supports_cuda( ) ) {
            if ( firstTime ) {
                firstTime = 0;
                pr_debug( "Using CUDA" );
            }
            MapGen::load_block_cuda( this );
        } else if ( LOAD_CHUNKS_SUPPORTS_HIP && MapGen::supports_hip( ) && !no_hip_mapgen ) {
            if ( firstTime ) {
                firstTime = 0;
                pr_debug( "Using HIP" );
            }
            if ( !MapGen::load_block_hip( this ) ) {
                // HIP failed (e.g. no code object for this GPU's arch); fall
                // back to the CPU path instead of leaving blocks uninitialized.
                MapGen::load_block_c( this );
            }
        } else if ( LOAD_CHUNKS_SUPPORTS_WEBGPU && !no_webgpu_mapgen && MapGen::supports_webgpu( ) ) {
            // firstTime is long gone by the time async WebGPU init completes
            // (early chunks legitimately used C), so announce the switch
            // separately the first time a chunk actually takes this branch.
            static int firstWebGPU = 1;
            if ( firstWebGPU ) {
                firstWebGPU = 0;
                pr_debug( "Using WebGPU" );
            }
            if ( !MapGen::load_block_webgpu( this ) ) {
                // WebGPU failed (init failure, device loss, readback error);
                // fall back to the CPU path for this chunk.
                MapGen::load_block_c( this );
            }
        } else {
            if ( firstTime ) {
                firstTime = 0;
                pr_debug( "Using C" );
            }
            MapGen::load_block_c( this );
        }
#if TERRAIN_GEN_PROFILING
        t_map_gen = now_us( ) - t0;
        profiling_map_gen_count.fetch_add( 1, std::memory_order_relaxed );
        t0 = now_us( );
#endif
        StructureGen::place( *this );
#if TERRAIN_GEN_PROFILING
        t_struct = now_us( ) - t0;
#endif
        this->dirty = 0;
    }
apply_edits:
    if constexpr ( REMEMBER_BLOCKS ) {
        if ( edits_status == 1 && !edits.runs.empty( ) ) {
            // Snapshot the generated cells before the overlay for the
            // legacy_snapshot compare: stored cells that match natural
            // terrain aren't real edits and are dropped (migrating the file
            // down to its true sparse diff).
            std::vector<BlockState> natural;
            if ( edits.legacy_snapshot && this->cells != nullptr ) {
                natural.resize( CHUNK_BLOCK_SIZE );
                for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                    natural[ i ] = this->state_at( i );
                }
            }
            for ( const ChunkEditRun &run : edits.runs ) {
                // Defensive bounds: a corrupt file must not write past the
                // cells array (the decoders already clamp; double-check
                // here because the overlay indexes cells[] directly).
                if ( run.start >= CHUNK_BLOCK_SIZE ) {
                    pr_debug( "edit run start %u past chunk cells; skipped", run.start );
                    break;
                }
                const uint32_t run_end = run.start + run.count > CHUNK_BLOCK_SIZE
                                             ? CHUNK_BLOCK_SIZE
                                             : run.start + run.count;
                for ( uint32_t i = run.start; i < run_end; i++ ) {
                    if ( edits.legacy_snapshot ) {
                        // Snapshot pruning only: stored cells that match
                        // natural terrain aren't real edits (v1 runs keep
                        // their mark even when they equal natural — there
                        // is no "revert to natural" in that format).
                        const BlockState natural_at = !natural.empty( )
                                                          ? natural[ i ]
                                                          : ( this->cells != nullptr ? this->state_at( i ) : BLOCK_STATE_AIR );
                        if ( BlockStates_equal( run.state, natural_at ) ) {
                            continue;
                        }
                    }
                    if ( this->edited == nullptr ) {
                        this->edited = static_cast<uint8_t *>( calloc( CHUNK_BLOCK_SIZE / 8, 1 ) );
                    }
                    this->edited[ i >> 3 ] |= static_cast<uint8_t>( 1u << ( i & 7 ) );
                    const BlockState old_state = this->cells != nullptr ? this->state_at( i ) : BLOCK_STATE_AIR;
                    if ( BlockStates_equal( old_state, run.state ) ) {
                        continue;
                    }
                    if ( this->cells == nullptr ) {
                        // An edit materialized an all-air chunk: same
                        // cells/mask allocation as set_block (the fresh
                        // cells[] is all non-opaque, then restamped below).
                        this->cells = chunk_alloc_cells( );
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
                    if ( block_is_chest( old_state.id ) && !block_is_chest( run.state.id ) ) {
                        this->containers.chests.erase( i );
                    }
                    if ( block_is_furnace( old_state.id ) && !block_is_furnace( run.state.id ) ) {
                        this->containers.furnaces.erase( i );
                    }
                    this->store_state( i, run.state );
                    light_air_mask_set( *this, i, run.state.id );
                }
            }
        }
        if ( edits.needs_rewrite ) {
            // A legacy file (or a v1 file with a foreign record size) is
            // rewritten in v1 — or unlinked when nothing differs from
            // natural — on the next persist.
            this->dirty = 1;
        }
        if ( this->cells != nullptr ) {
            // A chunk can decode or generate to all-air (e.g. mined flat).
            // Fold it to the null representation so it skips meshing like a
            // generated air chunk.
            bool all_air = true;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                if ( this->id_at( i ) != AIR ) {
                    all_air = false;
                    break;
                }
            }
            if ( all_air ) {
                this->free_block_storage( );
                chunk_free_air_mask( this->light.air_mask );
                this->light.air_mask = nullptr;
                chunk_free_air_mask( this->light.shade_mask );
                this->light.shade_mask = nullptr;
            }
        }
        this->is_empty_chunk = this->cells == nullptr;
    }
    this->applying_edits = 0;
    // Chest contents live in a per-chunk sidecar file (world-pos keyed);
    // convert to the block-index keying used by chests[].
    this->containers.chests.clear( );
    ChestContentsMap chest_world_map;
    this->containers.chests_loaded = map_storage.load_chests( this->chunk_pos, chest_world_map ) ? 1 : 0;
    for ( const auto &pair : chest_world_map ) {
        const glm::ivec3 diff = pair.first - this->chunk_pos * CHUNK_SIZE_I;
        // Entries must fall inside this chunk (file corruption guard).
        if ( diff.x < 0 || diff.x >= CHUNK_SIZE_X || diff.y < 0 || diff.y >= CHUNK_SIZE_Y || diff.z < 0 || diff.z >= CHUNK_SIZE_Z ) {
            continue;
        }
        this->containers.chests[ Chunk::get_index_from_coords( diff ) ] = pair.second;
    }
    // Furnace contents: same sidecar pattern as chests, world-pos keyed
    // file converted to in-chunk block indices.
    this->containers.furnaces.clear( );
    FurnaceContentsMap furnace_world_map;
    this->containers.furnaces_loaded = map_storage.load_furnaces( this->chunk_pos, furnace_world_map ) ? 1 : 0;
    for ( const auto &pair : furnace_world_map ) {
        const glm::ivec3 diff = pair.first - this->chunk_pos * CHUNK_SIZE_I;
        if ( diff.x < 0 || diff.x >= CHUNK_SIZE_X || diff.y < 0 || diff.y >= CHUNK_SIZE_Y || diff.z < 0 || diff.z >= CHUNK_SIZE_Z ) {
            continue;
        }
        this->containers.furnaces[ Chunk::get_index_from_coords( diff ) ] = pair.second;
    }
    // Mob sidecar (same pattern as chests): file records load first, then the
    // deterministic natural roll always runs — pristine naturals are never
    // persisted, so the roll recreates them on every load. Roll indices in
    // the file's removed section (touched/killed naturals) or whose id is
    // already among the loaded records are skipped. Skipped entirely for
    // REPGAME_MOB_TEST runs and REPGAME_MOBS=0. Static env reads: this runs
    // once per chunk on every terrain worker and env can't change mid-run.
    static const bool no_mobs = !env_bool( "REPGAME_MOBS", true ) || env_is_set( "REPGAME_MOB_TEST" );
    this->sidecars.mobs.clear( );
    this->sidecars.mobs_file_present = 0;
    this->sidecars.mobs_spawned = 0;
    this->sidecars.mobs_removed.clear( );
    this->sidecars.items.clear( );
    this->sidecars.items_file_present = 0;
    this->sidecars.player_edited = this->edited != nullptr ? 1 : 0;
    if ( !no_mobs ) {
        this->sidecars.mobs_file_present = map_storage.load_mobs( this->chunk_pos, this->sidecars.mobs, &this->sidecars.mobs_removed ) ? 1 : 0;
        std::set<uint8_t> roll_skip( this->sidecars.mobs_removed.begin( ), this->sidecars.mobs_removed.end( ) );
        for ( const MobFileRecord &record : this->sidecars.mobs ) {
            if ( net_is_natural_mob_id( record.net_id ) && net_natural_mob_home( record.net_id ) == this->chunk_pos ) {
                roll_skip.insert( static_cast<uint8_t>( net_natural_mob_index( record.net_id ) ) );
            }
        }
        this->sidecars.mobs_spawned = this->spawn_mobs( &roll_skip ) ? 1 : 0;
        // Dropped items follow the same test-mode rules as mobs: no file
        // reads (or writes) under REPGAME_MOBS=0 / REPGAME_MOB_TEST.
        this->sidecars.items_file_present = map_storage.load_items( this->chunk_pos, this->sidecars.items ) ? 1 : 0;
    }
    // Flood-fill emitters + column_open flags on the worker. Skylight is
    // resolved on the lighting thread by ChunkLoader::light_finalize_chunk.
    light_fill_chunk( *this );
#if TERRAIN_GEN_PROFILING
    t0 = now_us( );
#endif
    this->calculate_populated_blocks( );
#if TERRAIN_GEN_PROFILING
    t_pop = now_us( ) - t0;

    long long total = now_us( ) - t_start;
    int count = profiling_chunk_count.fetch_add( 1, std::memory_order_relaxed ) + 1;
    profiling_total_us.fetch_add( total, std::memory_order_relaxed );
    profiling_load_disk_us.fetch_add( t_load_disk, std::memory_order_relaxed );
    profiling_map_gen_us.fetch_add( t_map_gen, std::memory_order_relaxed );
    profiling_structure_gen_us.fetch_add( t_struct, std::memory_order_relaxed );
    profiling_populated_blocks_us.fetch_add( t_pop, std::memory_order_relaxed );

    if ( count % 100 == 0 ) {
        long long tot = profiling_total_us.load( std::memory_order_relaxed );
        long long disk = profiling_load_disk_us.load( std::memory_order_relaxed );
        long long gen = profiling_map_gen_us.load( std::memory_order_relaxed );
        long long struc = profiling_structure_gen_us.load( std::memory_order_relaxed );
        long long pop = profiling_populated_blocks_us.load( std::memory_order_relaxed );
        int gen_count = profiling_map_gen_count.load( std::memory_order_relaxed );
        int disk_count = profiling_disk_load_count.load( std::memory_order_relaxed );
        long long pers = terrain_persist_us.load( std::memory_order_relaxed );
        long long mesh_vl = profiling_mesh_vis_light_us.load( std::memory_order_relaxed );
        long long mesh_gr = profiling_mesh_greedy_us.load( std::memory_order_relaxed );
        pr_debug( "Terrain profiling: %d chunks | total=%.1fms avg=%.2fms | persist=%.1fms | disk=%.1fms (%d hits) | map_gen=%.1fms (%d gens) | struct=%.1fms | mesh=%.1fms (vis_light=%.1fms greedy=%.1fms)",
            count, tot / 1000.0, ( tot / 1000.0 ) / count,
            pers / 1000.0,
            disk / 1000.0, disk_count,
            gen / 1000.0, gen_count,
            struc / 1000.0, pop / 1000.0,
            mesh_vl / 1000.0, mesh_gr / 1000.0 );
        pr_debug( "INSTANCES total=%lld", profiling_instance_count.load( std::memory_order_relaxed ) );
    }
#endif
}

// ---- Fluid corner heights -------------------------------------------------
// Milestone-3 sloped fluids: per-cell corner heights in 1/64 units, packed
// as four bytes in BlockCoords::fluid_h0/fluid_h order — corner byte i is
// the corner (sx, sz) with sx = i&1, sz = i>>1, i.e. (x=0,z=0), (x=1,z=0),
// (x=0,z=1), (x=1,z=1). Filled once per chunk mesh so emission and the
// greedy-merge check stay cheap.
// FLAT = all four corners at FLUID_SOURCE_HEIGHT — the mergeable case;
// NOT_FLUID marks non-fluid cells (never read, but keeps the array defined).
#define FLUID_CORNERS_FLAT 0xFFFFFFFFu
#define FLUID_CORNERS_NOT_FLUID 0xFFFFFFFEu
static thread_local unsigned int s_fluid_corners[ CHUNK_BLOCK_SIZE ];

// Fluid id in a chunk cell word (WATER for a waterlogged cell, the block id
// for a fluid, LAST_BLOCK_ID otherwise) — the cell-space twin of
// fluid_in_cell(state_at).
static inline BlockID fluid_in_block_cell( const BlockCell cell ) {
    if ( block_cell_waterlogged( cell ) ) {
        return WATER;
    }
    const BlockID id = block_cell_id( cell );
    if ( id < LAST_BLOCK_ID && block_is_fluid( block_definition_get_definition( id ) ) ) {
        return id;
    }
    return LAST_BLOCK_ID;
}

// The cell's level against `fluid` (-1 when it holds no/other fluid, 0 for
// a waterlogged or source cell, 1..FLUID_MAX_LEVEL flowing).
static inline int fluid_cell_level( const BlockCell cell, const BlockID fluid ) {
    if ( fluid_in_block_cell( cell ) != fluid ) {
        return -1;
    }
    if ( block_cell_waterlogged( cell ) ) {
        return 0;
    }
    const int rot = block_cell_rotation( cell );
    return rot <= FLUID_MAX_LEVEL ? rot : FLUID_MAX_LEVEL;
}

// Packed corner heights for the fluid cell at (x,y,z), or FLUID_CORNERS_FLAT
// when all four corners sit at source height. The corner at (sx, sz) averages
// the four cells sharing it: self, (dx,0), (0,dz), (dx,dz) with dx/dz the
// step toward that corner.
static unsigned int fluid_pack_corners( const BlockCell *cells, int x, int y, int z ) {
    const BlockCell self = cells[ Chunk::get_index_from_coords( x, y, z ) ];
    const BlockID fluid = fluid_in_block_cell( self );
    if ( fluid == LAST_BLOCK_ID ) {
        return FLUID_CORNERS_NOT_FLUID;
    }
    const int flow_range = block_definition_get_definition( fluid )->flow_range;
    const int own_height = fluid_level_height( fluid_cell_level( self, fluid ), flow_range );
    unsigned int packed_corners = 0;
    bool all_source = true;
    for ( int corner = 0; corner < 4; corner++ ) {
        const int dx = ( corner & 1 ) ? 1 : -1;
        const int dz = ( corner & 2 ) ? 1 : -1;
        FluidCornerCell cc[ 4 ];
        for ( int o = 0; o < 4; o++ ) {
            const int cx = x + ( ( o & 1 ) ? dx : 0 );
            const int cz = z + ( ( o & 2 ) ? dz : 0 );
            const BlockCell c = cells[ Chunk::get_index_from_coords( cx, y, cz ) ];
            const int level = fluid_cell_level( c, fluid );
            const bool same = level >= 0;
            cc[ o ].level = same ? fluid_level_height( level, flow_range ) : -1;
            cc[ o ].fluid_above = fluid_in_block_cell( cells[ Chunk::get_index_from_coords( cx, y + 1, cz ) ] ) == fluid;
            cc[ o ].solid = block_definition_get_definition( block_cell_id( c ) )->collides_with_player && !same;
        }
        const int h = fluid_corner_height( own_height, cc );
        packed_corners |= static_cast<unsigned int>( h & 0xff ) << ( corner * 8 );
        if ( h != FLUID_SOURCE_HEIGHT ) {
            all_source = false;
        }
    }
    return all_source ? FLUID_CORNERS_FLAT : packed_corners;
}

// Six-bit per-face visibility of the fluid shell at (x,y,z) — the "water
// and glass" branch of mesh_vis_logic.hpp re-evaluated on CPU. Water and
// lava carry no_light = NO_LIGHT_BRIGHT, so phase 1 can't encode a hidden
// face; recomputing here (CPU-only, over the same halo cells) keeps the
// HIP/WGSL path correct too.
static unsigned int fluid_face_vis_mask( const BlockCell *cells, const GPUBlockDef *defs, int x, int y, int z, const BlockID fluid ) {
    unsigned int vis = 0;
    for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
        const BlockCell nc = cells[ Chunk::get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face ], y + FACE_DIR_Y_OFFSETS[ face ], z + FACE_DIR_Z_OFFSETS[ face ] ) ];
        const BlockID nid = block_cell_id( nc );
        const int nrot = block_cell_rotation( nc );
        const GPUBlockDef &ndef = defs[ nid ];
        const int liquid_boundary = block_cell_waterlogged( nc ) != 0 || ndef.renderOrder == RenderOrder_Translucent;
        int face_vis;
        if ( face == FACE_TOP ) {
            face_vis = nid != fluid && !liquid_boundary;
        } else {
            const int opposing_face = OPPOSITE_FACE[ mesh_rotated_face( face, nrot ) ];
            face_vis = mesh_face_seethrough( ndef, block_cell_powered( nc ), nrot, opposing_face ) != 0 && nid != fluid && !liquid_boundary;
        }
        vis |= static_cast<unsigned int>( face_vis != 0 ) << face;
    }
    return vis;
}

int Chunk::can_extend_rect( const BlockState &blockState, const unsigned int *packed_lighting, const WorkingSpace *workingSpace, const glm::ivec3 &starting, const glm::ivec3 &size, const glm::ivec3 &dir ) const {
    if constexpr ( DISABLE_GROUPING_BLOCKS ) {
        return 0;
    }
    if ( starting.x + size.x + dir.x > CHUNK_SIZE_X )
        return 0;
    if ( starting.y + size.y + dir.y > CHUNK_SIZE_Y )
        return 0;
    if ( starting.z + size.z + dir.z > CHUNK_SIZE_Z )
        return 0;
    // if ( ( ( size_x > 1 ) + ( size_y > 1 ) + ( size_z > 1 ) ) == 3 ) {
    //     if ( !( size_x == 1 && size_y == 1 && size_z == 1 ) ) {
    //         pr_debug( "Error, there must be 2 values different in a plane" );
    //     }
    // }
    // if ( blockState.id == REDSTONE_WIRE_CROSS ) {
    //     pr_debug( "Trying to mesh" );
    // }
    // Fluid merge gate: only FLAT (all-56 corner) candidates may join a
    // rect — a sloped or falling fluid carries its own corner heights.
    const bool fluid_merge = blockState.id < LAST_BLOCK_ID && block_is_fluid( block_definition_get_definition( blockState.id ) );
    int num_checked_blocks = 0;
    for ( int new_x = starting.x; new_x < starting.x + size.x; new_x++ ) {
        for ( int new_y = starting.y; new_y < starting.y + size.y; new_y++ ) {
            for ( int new_z = starting.z; new_z < starting.z + size.z; new_z++ ) {
                const int index = Chunk::get_index_from_coords( new_x + dir.x, new_y + dir.y, new_z + dir.z );
                num_checked_blocks++;
                if ( workingSpace[ index ].has_been_drawn ) {
                    return 0;
                }
                if ( !workingSpace[ index ].can_be_seen ) {
                    return 0;
                }
                const BlockState new_blockState = this->state_at( index );
                if ( !BlockStates_equal( new_blockState, blockState ) ) {
                    return 0;
                }
                if ( fluid_merge && s_fluid_corners[ index ] != FLUID_CORNERS_FLAT ) {
                    return 0;
                }
                for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                    if ( packed_lighting[ i ] != workingSpace[ index ].packed_lighting[ i ] ) {
                        return 0;
                    }
                }
            }
        }
    }
    int expected_checked_blocks = size.x * size.y * size.z;
    if ( num_checked_blocks != expected_checked_blocks ) {
        pr_debug( "Error, Didnt check enough blocks" );
    }
    return 1;
}


void Chunk::calculate_populated_blocks( ) {
    PROFILE_SCOPE( "chunk_mesh" );
    this->ensure_layers( );
    // Meshing writes instances into a per-render-order thread-local scratch
    // buffer (same lifetime model as tls_workingSpace below): lazily malloc'd
    // at CHUNK_BLOCK_SIZE on first use and never freed, so chunks don't pay
    // a multi-megabyte malloc + page faults per mesh. The used instances are
    // memcpy'd into a right-sized malloc at the end.
    static thread_local BlockCoords *tls_mesh_scratch[ LAST_RENDER_ORDER ] = { nullptr };
    static thread_local int tls_mesh_scratch_cap[ LAST_RENDER_ORDER ] = { 0 };
    int num_instances[ LAST_RENDER_ORDER ] = { 0 };
    // Each populated_blocks layer starts at CHUNK_BLOCK_SIZE slots, but a
    // fence cell emits up to FENCE_MAX_BOXES instances (a gate
    // FENCE_GATE_MAX_BOXES), so a fence-heavy chunk can overflow it. Grow by
    // doubling before an emit that wouldn't fit. Never keep a BlockCoords*
    // across a grow — realloc can move the buffer.
    int capacity[ LAST_RENDER_ORDER ];
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        capacity[ renderOrder ] = CHUNK_BLOCK_SIZE;
    }
    auto ensure_capacity = [ & ]( int renderOrder, int extra ) {
        const int needed = num_instances[ renderOrder ] + extra;
        if ( needed <= capacity[ renderOrder ] ) {
            return;
        }
        int new_capacity = capacity[ renderOrder ];
        while ( new_capacity < needed ) {
            new_capacity *= 2;
        }
        BlockCoords *grown = static_cast<BlockCoords *>( realloc( this->layers[ renderOrder ].populated_blocks, new_capacity * sizeof( BlockCoords ) ) );
        if ( grown == nullptr ) {
            pr_debug( "Error, out of memory growing populated_blocks" );
            return;
        }
        this->layers[ renderOrder ].populated_blocks = grown;
        capacity[ renderOrder ] = new_capacity;
        tls_mesh_scratch[ renderOrder ] = grown;
        tls_mesh_scratch_cap[ renderOrder ] = new_capacity;
    };

    // Empty chunks (all air): skip meshing entirely — and skip the
    // populated_blocks mallocs + workingSpace memset, which are only needed
    // when real blocks exist.
    if ( this->is_empty_chunk ) {
        if constexpr ( !REMEMBER_BLOCKS ) {
            this->free_block_storage( );
            free( this->edited );
            this->edited = nullptr;
            chunk_free_air_mask( this->light.air_mask );
            this->light.air_mask = nullptr;
            chunk_free_air_mask( this->light.shade_mask );
            this->light.shade_mask = nullptr;
        }
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            this->layers[ renderOrder ].num_instances = 0;
        }
        // No refresh_slot_flags here: this runs on terrain workers too
        // (load_terrain), and the following program_terrain refreshes on the
        // render thread.
        return;
    }

    // populated_blocks are filled sequentially (only num_instances entries are read
    // later) into the scratch buffer — no need to zero ~18.8MB per chunk.
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        RenderLayer &layer = this->layers[ renderOrder ];
        if ( layer.populated_blocks ) {
            pr_debug( "Error, populated blocks already populated" );
        }
        if ( !tls_mesh_scratch[ renderOrder ] ) {
            tls_mesh_scratch[ renderOrder ] = static_cast<BlockCoords *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockCoords ) ) );
            tls_mesh_scratch_cap[ renderOrder ] = CHUNK_BLOCK_SIZE;
        }
        layer.populated_blocks = tls_mesh_scratch[ renderOrder ];
        capacity[ renderOrder ] = tls_mesh_scratch_cap[ renderOrder ];
    }
    // workingSpace needs has_been_drawn=false initially; reuse a thread-local buffer
    // to avoid calloc's page-mapping overhead. memset on resident pages is much faster.
    static thread_local WorkingSpace *tls_workingSpace = nullptr;
    if ( !tls_workingSpace ) {
        tls_workingSpace = static_cast<WorkingSpace *>( malloc( CHUNK_BLOCK_SIZE * sizeof( WorkingSpace ) ) );
    }
    WorkingSpace *workingSpace = tls_workingSpace;
    memset( workingSpace, 0, CHUNK_BLOCK_SIZE * sizeof( WorkingSpace ) );

#if TERRAIN_GEN_PROFILING
    long long t_mesh_start = now_us( );
    long long t_phase1 = 0, t_phase2 = 0;
    long long t_ph1 = now_us( );
#endif

    bool used_gpu_mesh = false;
#if defined( REPGAME_BUILD_WITH_HIP )
    static const bool no_hip_mesh = env_is_set( "REPGAME_NO_HIP_MESH" );
    if ( LOAD_CHUNKS_SUPPORTS_HIP && MapGen::supports_hip( ) && !no_hip_mesh ) {
        used_gpu_mesh = mesh_gen_calculate_hip( this->cells, workingSpace );
    }
#endif
#if defined( REPGAME_WASM )
    static const bool no_webgpu_mesh = env_is_set( "REPGAME_NO_WEBGPU_MESH" );
    if ( !used_gpu_mesh && LOAD_CHUNKS_SUPPORTS_WEBGPU && !no_webgpu_mesh && MapGen::supports_webgpu( ) ) {
        used_gpu_mesh = mesh_gen_calculate_webgpu( this->cells, workingSpace );
    }
#endif

#if MESH_VERIFY
    if ( used_gpu_mesh ) {
        // Save GPU results, then run CPU meshing and compare
        WorkingSpace *gpu_results = static_cast<WorkingSpace *>( malloc( CHUNK_BLOCK_SIZE * sizeof( WorkingSpace ) ) );
        memcpy( gpu_results, workingSpace, CHUNK_BLOCK_SIZE * sizeof( WorkingSpace ) );
        memset( workingSpace, 0, CHUNK_BLOCK_SIZE * sizeof( WorkingSpace ) );
        // Fall through to CPU meshing by setting used_gpu_mesh = false for the CPU path
        used_gpu_mesh = false;
        // But remember we need to compare after
        static int verify_mismatches = 0;
        static int verify_chunks = 0;
        verify_chunks++;
#endif

    if ( !used_gpu_mesh ) {
        const GPUBlockDef *block_defs = mesh_def_table( );
#define MESH_CELL_AT( i ) ( (unsigned)( this->cells[ i ] ) )
#define MESH_OUT_FLAGS( idx, seen, vis )                 \
    workingSpace[ idx ].can_be_seen = ( seen );          \
    workingSpace[ idx ].visible = ( vis );
#define MESH_OUT_LIGHT( idx, f, v ) workingSpace[ idx ].packed_lighting[ f ] = ( v );
        for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
            int x, y, z;
            if ( Chunk::get_coords_from_index( index, x, y, z ) ) {
                // Single-source per-cell logic — identical text drives the
                // HIP kernel and the WGSL WebGPU kernel.
#include "common/mesh_vis_logic.hpp"
            }
        }
#undef MESH_CELL_AT
#undef MESH_OUT_FLAGS
#undef MESH_OUT_LIGHT
    } // end if ( !used_gpu_mesh )
#if MESH_VERIFY
        // Compare CPU results (in workingSpace) with GPU results (in gpu_results)
        for ( int index = 0; index < CHUNK_BLOCK_SIZE; index++ ) {
            if ( workingSpace[ index ].visible != gpu_results[ index ].visible ||
                 workingSpace[ index ].can_be_seen != gpu_results[ index ].can_be_seen ) {
                int x, y, z;
                Chunk::get_coords_from_index( index, x, y, z );
                pr_debug( "MESH MISMATCH at (%d,%d,%d): visible cpu=%d gpu=%d, can_be_seen cpu=%d gpu=%d",
                    x, y, z,
                    (int)workingSpace[ index ].visible, (int)gpu_results[ index ].visible,
                    (int)workingSpace[ index ].can_be_seen, (int)gpu_results[ index ].can_be_seen );
                verify_mismatches++;
                break;
            }
            for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                if ( workingSpace[ index ].packed_lighting[ f ] != gpu_results[ index ].packed_lighting[ f ] ) {
                    int x, y, z;
                    Chunk::get_coords_from_index( index, x, y, z );
                    pr_debug( "MESH MISMATCH at (%d,%d,%d) face %d: lighting cpu=%u gpu=%u",
                        x, y, z, f,
                        workingSpace[ index ].packed_lighting[ f ],
                        gpu_results[ index ].packed_lighting[ f ] );
                    verify_mismatches++;
                    break;
                }
            }
        }
        if ( verify_chunks % 100 == 0 ) {
            pr_debug( "MESH VERIFY: %d chunks checked, %d mismatches", verify_chunks, verify_mismatches );
        }
        free( gpu_results );
        // Use the CPU results (workingSpace already has them)
    }
#endif
#if TERRAIN_GEN_PROFILING
    t_phase1 = now_us( ) - t_ph1;
    profiling_mesh_vis_light_us.fetch_add( t_phase1, std::memory_order_relaxed );
    t_ph1 = now_us( );
#endif
    // Fluid surfaces (CPU-only — also corrects GPU-meshed workingSpace):
    // per-cell corner heights for emission + the greedy merge check, and a
    // packed_lighting rewrite. Phase 1 marks a fluid face "lit" with the
    // block's lit value but hidden faces only get no_light — both are
    // NO_LIGHT_BRIGHT for water/lava, so the hidden faces would draw with
    // the new full-shell geometry unless they get an explicit
    // NO_LIGHT_NO_DRAW here.
    {
        const GPUBlockDef *fluid_defs = mesh_def_table( );
        for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
            for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
                for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                    const int index = get_index_from_coords( x, y, z );
                    const BlockCell cell = this->cells[ index ];
                    s_fluid_corners[ index ] = FLUID_CORNERS_NOT_FLUID;
                    const BlockID id = block_cell_id( cell );
                    if ( id >= LAST_BLOCK_ID || fluid_defs[ id ].renderOrder != RenderOrder_Water ) {
                        continue;
                    }
                    s_fluid_corners[ index ] = fluid_pack_corners( this->cells, x, y, z );
                    const unsigned int vis = fluid_face_vis_mask( this->cells, fluid_defs, x, y, z, id );
                    for ( int f = FACE_TOP; f < NUM_FACES_IN_CUBE; f++ ) {
                        workingSpace[ index ].packed_lighting[ f ] = ( vis >> f ) & 1u ? NO_LIGHT_BRIGHT : NO_LIGHT_NO_DRAW;
                    }
                }
            }
        }
    }
    for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                int index = get_index_from_coords( x, y, z );
                if ( !workingSpace[ index ].has_been_drawn ) {

                    const BlockState blockState = this->state_at( index );
                    Block *block = block_definition_get_definition( blockState.display_id );
                    // if ( blockState.display_id == REDSTONE_WIRE_CROSS ) {
                    //     pr_debug( "Got cross" );
                    // }
                    bool visiable_block = workingSpace[ index ].visible;
                    bool can_be_seen = workingSpace[ index ].can_be_seen;
                    bool has_been_drawn = workingSpace[ index ].has_been_drawn;
                    unsigned int *packed_lighting = workingSpace[ index ].packed_lighting;

                    if ( visiable_block && can_be_seen && !has_been_drawn ) {

                        int block_can_mesh_x;
                        int block_can_mesh_y;
                        int block_can_mesh_z;

                        if ( block->is_piston_head || ( block->is_piston && blockState.current_redstone_power > 0 ) ) {
                            // Piston boxes depend on state, not just the def:
                            // a head plate is thin on its facing axis (the def
                            // is only the canonical front-facing plate — for
                            // up/down block_adjust bakes it thin in y) and an
                            // extended base loses the 4px at its head end. A
                            // merged run only tiles along axes the effective
                            // world-space box fully spans, so take the flags
                            // from the shape helpers instead of the def scale.
                            short mesh_scale[ 3 ], mesh_offset[ 3 ];
                            if ( block->is_piston_head ) {
                                piston_head_shape( blockState.rotation, mesh_scale, mesh_offset );
                            } else {
                                piston_base_shape( blockState.rotation, mesh_scale, mesh_offset );
                            }
                            block_can_mesh_x = mesh_scale[ 0 ] == 16;
                            block_can_mesh_y = mesh_scale[ 1 ] == 16;
                            block_can_mesh_z = mesh_scale[ 2 ] == 16;
                        } else if ( blockState.rotation == BLOCK_ROTATE_0 || blockState.rotation == BLOCK_ROTATE_180 ) {
                            block_can_mesh_x = block->calculated.can_mesh_x;
                            block_can_mesh_y = block->calculated.can_mesh_y;
                            block_can_mesh_z = block->calculated.can_mesh_z;
                        } else {
                            block_can_mesh_x = block->calculated.can_mesh_z;
                            block_can_mesh_y = block->calculated.can_mesh_y;
                            block_can_mesh_z = block->calculated.can_mesh_x;
                        }
                        if ( block_is_fluid( block ) && s_fluid_corners[ index ] != FLUID_CORNERS_FLAT ) {
                            // Sloped/falling fluids carry per-cell corner
                            // heights — they never merge. Flat source fields
                            // still merge on every axis.
                            block_can_mesh_x = 0;
                            block_can_mesh_y = 0;
                            block_can_mesh_z = 0;
                        }

                        int size_x = 1;
                        int size_y = 1;
                        int size_z = 1;
                        int can_extend_x = 0;
                        int can_extend_y = 0;
                        int can_extend_z = 0;
                        do {
                            if ( block_can_mesh_x ) {
                                glm::ivec3 starting = glm::ivec3( x + size_x - 1, y, z );
                                glm::ivec3 size = glm::ivec3( 1, size_y, size_z );
                                glm::ivec3 dir = glm::ivec3( 1, 0, 0 );
                                can_extend_x = this->can_extend_rect( blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_x ) {
                                    size_x++;
                                }
                            }
                            if ( block_can_mesh_z ) {
                                glm::ivec3 starting = glm::ivec3( x, y, z + size_z - 1 );
                                glm::ivec3 size = glm::ivec3( size_x, size_y, 1 );
                                glm::ivec3 dir = glm::ivec3( 0, 0, 1 );
                                can_extend_z = this->can_extend_rect( blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_z ) {
                                    size_z++;
                                }
                            }
                            if ( block_can_mesh_y ) {
                                glm::ivec3 starting = glm::ivec3( x, y + size_y - 1, z );
                                glm::ivec3 size = glm::ivec3( size_x, 1, size_z );
                                glm::ivec3 dir = glm::ivec3( 0, 1, 0 );
                                can_extend_y = this->can_extend_rect( blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_y ) {
                                    size_y++;
                                }
                            }
                        } while ( can_extend_x || can_extend_z || can_extend_y );

                        for ( int new_x = x; new_x < x + size_x; new_x++ ) {
                            for ( int new_z = z; new_z < z + size_z; new_z++ ) {
                                for ( int new_y = y; new_y < y + size_y; new_y++ ) {
                                    int index2 = get_index_from_coords( new_x, new_y, new_z );
                                    workingSpace[ index2 ].has_been_drawn = true;
                                }
                            }
                        }
                        workingSpace[ index ].has_been_drawn = true;

                        // Fences and gates emit several box instances per
                        // cell instead of the single scaled cube: the fence
                        // post plus rails for each connected side (connection
                        // mask from the 4 horizontal neighbor cells — the
                        // halo is valid there, the same reads the
                        // redstone-dust code does), or the gate's closed/open
                        // definition-space model.
                        BlockBox boxes[ FENCE_GATE_MAX_BOXES ];
                        int num_boxes = 0;
                        if ( block->is_fence ) {
                            unsigned connections = 0;
                            const int conn_dir[ 4 ][ 4 ] = { // dx, dz, connect bit, along_x
                                { 1, 0, FENCE_CONNECT_POS_X, 1 },
                                { -1, 0, FENCE_CONNECT_NEG_X, 1 },
                                { 0, 1, FENCE_CONNECT_POS_Z, 0 },
                                { 0, -1, FENCE_CONNECT_NEG_Z, 0 },
                            };
                            for ( int d = 0; d < 4; d++ ) {
                                const BlockCell ncell = this->cells[ get_index_from_coords( x + conn_dir[ d ][ 0 ], y, z + conn_dir[ d ][ 1 ] ) ];
                                if ( fence_connects_to( block, block_cell_id( ncell ), block_cell_rotation( ncell ), conn_dir[ d ][ 3 ] ) ) {
                                    connections |= conn_dir[ d ][ 2 ];
                                }
                            }
                            num_boxes = fence_boxes( connections, boxes );
                        } else if ( block->is_fence_gate ) {
                            num_boxes = fence_gate_boxes( ( blockState.rotation & BLOCK_ROTATE_GATE_OPEN ) != 0, boxes );
                        } else if ( block->is_stairs ) {
                            // Neighbor stair rotations for the corner derive
                            // (stair_rots[d] = rotation of the stair in world
                            // direction d, -1 = not a stair) — same gather as
                            // collision.cpp's is_stairs branch.
                            static const int stair_dir[ 4 ][ 2 ] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
                            int stair_rots[ 4 ];
                            for ( int d = 0; d < 4; d++ ) {
                                const BlockCell ncell = this->cells[ get_index_from_coords( x + stair_dir[ d ][ 0 ], y, z + stair_dir[ d ][ 1 ] ) ];
                                stair_rots[ d ] = block_definition_get_definition( block_cell_id( ncell ) )->is_stairs ? block_cell_rotation( ncell ) : -1;
                            }
                            num_boxes = stairs_boxes( stairs_shape_derive( blockState.rotation, stair_rots ),
                                                      ( blockState.rotation & BLOCK_ROTATE_STAIRS_TOP ) != 0, boxes );
                        } else if ( block->is_standing_sign ) {
                            // Board plate + post — def space, yawed by the
                            // shader like the fence/stair boxes.
                            num_boxes = standing_sign_boxes( boxes );
                        }
                        // A lit redstone torch emits a second instance: the
                        // 4-wide orb head around the stick's top.
                        const bool redstone_orb = blockState.display_id == REDSTONE_TORCH && blockState.current_redstone_power > 0;
                        const bool piston_arm_cell = block->is_piston_head || ( block->is_piston && blockState.current_redstone_power > 0 );
                        const int emit_count = ( num_boxes > 0 ? num_boxes : 1 ) + ( redstone_orb ? 1 : 0 ) + ( block->spine_shell ? 2 : 0 ) +
                                               ( piston_arm_cell ? size_x * size_y * size_z : 0 );
                        ensure_capacity( block->renderOrder, emit_count );
                        if ( num_instances[ block->renderOrder ] + emit_count > capacity[ block->renderOrder ] ) {
                            continue; // OOM — drop this cell's instances.
                        }

                        BlockCoords *blockCoord;

                        blockCoord = &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                        num_instances[ block->renderOrder ] += 1;

                        blockCoord->x = this->chunk_pos.x * CHUNK_SIZE_X + x;
                        blockCoord->y = this->chunk_pos.y * CHUNK_SIZE_Y + y;
                        blockCoord->z = this->chunk_pos.z * CHUNK_SIZE_Z + z;

                        blockCoord->mesh_x = size_x;
                        blockCoord->mesh_y = size_y;
                        blockCoord->mesh_z = size_z;
                        blockCoord->face_shift = block_yaw( block, blockState.rotation );
                        blockCoord->scale_x = block->scale.x;
                        blockCoord->scale_y = block->scale.y;
                        blockCoord->scale_z = block->scale.z;

                        blockCoord->offset_x = block->offset.x;
                        blockCoord->offset_y = block->offset.y;
                        blockCoord->offset_z = block->offset.z;

                        blockCoord->tex_offset_x = block->tex_offset.x;
                        blockCoord->tex_offset_y = block->tex_offset.y;
                        blockCoord->tex_offset_z = block->tex_offset.z;

                        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                            blockCoord->packed_lighting[ i ] = workingSpace[ index ].packed_lighting[ i ];
                            blockCoord->face[ i ] = block->textures[ i ];
                        }
                        block_adjust_coord_based_on_state( block, &blockState, blockCoord );

                        if ( block_is_fluid( block ) ) {
                            // Sloped-fluid metadata: yaw 0 with the fluid
                            // flag (rotation carries the sim level, not a
                            // yaw), and the precomputed corner heights ride
                            // the free BlockCoords bytes. A non-flat surface
                            // swaps its top to the flow texture — the vertex
                            // shader rotates the top UVs onto the flow.
                            const unsigned int corners = s_fluid_corners[ index ];
                            const bool fluid_flat = corners == FLUID_CORNERS_FLAT;
                            blockCoord->face_shift = BLOCK_COORDS_FLUID;
                            blockCoord->fluid_h0 = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( corners & 0xffu );
                            blockCoord->fluid_h[ 0 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 8 ) & 0xffu );
                            blockCoord->fluid_h[ 1 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 16 ) & 0xffu );
                            blockCoord->fluid_h[ 2 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 24 ) & 0xffu );
                            if ( !fluid_flat ) {
                                blockCoord->face[ FACE_TOP ] = blockCoord->face[ FACE_FRONT ];
                            }
                        }

                        // Render redstone dust on the sides of dust blocks for vertical connections.
                        // When dust has air to the side and dust below that air, the dust's side face
                        // is visible and shows a dust strip on the side of the solid block below.
                        if ( block->is_redstone_dust ) {
                            // Use LINE_2 (perpendicular to LINE_1) so the line appears vertical on side faces
                            TextureID side_texture = blockState.current_redstone_power > 0 ? TEX_REDSTONE_WIRE_LINE_2_POWERED : TEX_REDSTONE_WIRE_LINE_2_UNPOWERED;
                            const int horiz_dx[ 4 ] = { 1, -1, 0, 0 };
                            const int horiz_dz[ 4 ] = { 0, 0, 1, -1 };
                            const int horiz_face[ 4 ] = { FACE_RIGHT, FACE_LEFT, FACE_FRONT, FACE_BACK };
                            bool has_vertical_connection = false;
                            for ( int d = 0; d < 4; d++ ) {
                                int nx = x + horiz_dx[ d ];
                                int nz = z + horiz_dz[ d ];
                                const BlockState neighbor = this->state_at( get_index_from_coords( nx, y, nz ) );
                                const Block *neighbor_block = block_definition_get_definition( neighbor.display_id );
                                // Only render side face when neighbor is non-solid and not dust (air)
                                if ( !neighbor_block->collides_with_player && !neighbor_block->is_redstone_dust ) {
                                    const BlockState below = this->state_at( get_index_from_coords( nx, y - 1, nz ) );
                                    const Block *below_block = block_definition_get_definition( below.display_id );
                                    if ( below_block->is_redstone_dust ) {
                                        blockCoord->face[ horiz_face[ d ] ] = side_texture;
                                        blockCoord->packed_lighting[ horiz_face[ d ] ] = NO_LIGHT_BRIGHT;
                                        has_vertical_connection = true;
                                    }
                                }
                            }
                            // Extend the dust block downward to cover the solid block below,
                            // so the side face is visible as a full-height strip.
                            // Outset slightly (1 pixel) so the dust face renders in front of
                            // the solid block's face, preventing z-fighting and ensuring visibility.
                            // tex_offset is set to keep the texture centered (opposite of centered_border).
                            if ( has_vertical_connection ) {
                                blockCoord->scale_y = 16;
                                blockCoord->offset_y = -15;
                                blockCoord->scale_x = 18;
                                blockCoord->offset_x = -1;
                                blockCoord->scale_z = 18;
                                blockCoord->offset_z = -1;
                                blockCoord->tex_offset_x = 1;
                                blockCoord->tex_offset_z = 1;
                            }
                        }

                        // Offset torches that are mounted on the side of blocks.
                        // rotation 4=attached LEFT, 5=attached FRONT, 6=attached RIGHT, 7=attached BACK
                        // Push the torch towards the wall it's attached to.
                        if ( block->is_torch && blockState.rotation >= 4 ) {
                            // Torch scale is (2, 10, 2), offset is (7, 0, 7)
                            // Push towards the wall: set offset to 0 on the wall side,
                            // or 14 (=16-2) on the opposite side
                            if ( blockState.rotation == 4 ) {
                                // Attached LEFT: push to left wall
                                blockCoord->offset_x = 0;
                            } else if ( blockState.rotation == 5 ) {
                                // Attached FRONT: push to back (towards wall at z+1)
                                blockCoord->offset_z = 14;
                            } else if ( blockState.rotation == 6 ) {
                                // Attached RIGHT: push to right wall
                                blockCoord->offset_x = 14;
                            } else if ( blockState.rotation == 7 ) {
                                // Attached BACK: push to front (towards wall at z-1)
                                blockCoord->offset_z = 0;
                            }
                        }

                        // Lit redstone torches get Java's
                        // template_redstone_torch glow: a hollow 3x3x3 shell
                        // around the stick's top whose quads face inward
                        // (BLOCK_COORDS_INSIDE_OUT), so back-face culling
                        // leaves the far walls as a red glow behind the
                        // stick. It copies the stick instance (lighting,
                        // face_shift) and follows the wall-mount shift via
                        // offset * 2 - 1, with a fixed tex_offset like the
                        // stick. BLOCK_COORDS_HALF_PIXEL gives the half-pixel
                        // placement Java uses (6.5 -> 13 in 1/32 units).
                        if ( redstone_orb ) {
                            BlockCoords *orb = &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                            num_instances[ block->renderOrder ] += 1;
                            *orb = *blockCoord;
                            orb->face_shift = ( blockCoord->face_shift & BLOCK_COORDS_ROTATION_MASK ) | BLOCK_COORDS_INSIDE_OUT | BLOCK_COORDS_HALF_PIXEL;
                            orb->scale_x = 6;
                            orb->scale_y = 6;
                            orb->scale_z = 6;
                            orb->offset_x = blockCoord->offset_x * 2 - 1;
                            orb->offset_y = 15;
                            orb->offset_z = blockCoord->offset_z * 2 - 1;
                            orb->tex_offset_x = -13;
                            orb->tex_offset_y = -15;
                            orb->tex_offset_z = -13;
                            for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                                orb->face[ f ] = TEX_REDSTONE_TORCH_GLOW;
                            }
                        }

                        // Piston heads emit a second instance per run cell:
                        // the 4x4 arm spanning from the plate back to the
                        // cell's base end. Horizontal heads use the
                        // definition-space shaft (the shader yaws it with the
                        // plate); up/down get the world-baked column the same
                        // way block_adjust bakes the plate. Laterals use the
                        // wood arm tile (Bedrock pistonArm.png's board style:
                        // planks framed in steel) and both end caps are
                        // skipped — the head end is buried in the plate, the
                        // base end meets the throat stub the extended base
                        // emits. Plate runs merge across cells, so the arms
                        // loop the run volume to give every merged cell its
                        // own shaft.
                        if ( block->is_piston_head ) {
                            const int head_face = piston_head_face( blockState.rotation );
                            const unsigned char yaw = blockState.rotation & 3;
                            for ( int ax = 0; ax < size_x; ax++ ) {
                                for ( int ay = 0; ay < size_y; ay++ ) {
                                    for ( int az = 0; az < size_z; az++ ) {
                                        BlockCoords *arm =
                                            &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                                        num_instances[ block->renderOrder ] += 1;
                                        *arm = *blockCoord;
                                        arm->x = blockCoord->x + ax;
                                        arm->y = blockCoord->y + ay;
                                        arm->z = blockCoord->z + az;
                                        arm->mesh_x = 1;
                                        arm->mesh_y = 1;
                                        arm->mesh_z = 1;
                                        // Shaft quads are all interior to
                                        // the cell: a boundary-covering
                                        // neighbor can't hide a lateral
                                        // while the adjoining cell's
                                        // throat stays open, so force
                                        // every face lit before the cap
                                        // skips. A fully sealed cavity
                                        // just makes them overdraw.
                                        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                                            arm->packed_lighting[ f ] = NO_LIGHT_BRIGHT;
                                        }
                                        if ( head_face == FACE_TOP || head_face == FACE_BOTTOM ) {
                                            arm->scale_x = 4;
                                            arm->scale_y = 12;
                                            arm->scale_z = 4;
                                            arm->offset_x = 6;
                                            arm->offset_y = head_face == FACE_TOP ? 0 : 4;
                                            arm->offset_z = 6;
                                            arm->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
                                            arm->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
                                        } else {
                                            arm->scale_x = 4;
                                            arm->scale_y = 4;
                                            arm->scale_z = 12;
                                            arm->offset_x = 6;
                                            arm->offset_y = 6;
                                            arm->offset_z = 0;
                                            // Definition-space end caps: land
                                            // them on their world-space
                                            // packed_lighting slots like the
                                            // multi-box skip_faces remap.
                                            arm->packed_lighting[ mesh_rotated_face( FACE_FRONT, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                                            arm->packed_lighting[ mesh_rotated_face( FACE_BACK, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                                        }
                                        // Wood grain runs along the shaft on
                                        // every lateral. In def space the
                                        // shaft always lies along its
                                        // sampled v — except the horizontal
                                        // LEFT/RIGHT slots, whose u runs
                                        // along z; those take the rotated
                                        // tile.
                                        const bool vertical = head_face == FACE_TOP || head_face == FACE_BOTTOM;
                                        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                                            arm->face[ f ] = !vertical && ( f == FACE_LEFT || f == FACE_RIGHT ) ? TEX_PISTON_ARM_H : TEX_PISTON_ARM;
                                        }
                                    }
                                }
                            }
                        }

                        // Extended piston bases emit the other piece of the
                        // arm: the 4x4x4 stub inside the body's recessed
                        // throat (Java's piston_extended model keeps it on
                        // the base). It continues the head's shaft through
                        // the open 4px of this cell up to the body face.
                        // Same layout as the head arm — def-space for
                        // horizontal yaws, world-baked for up/down — and the
                        // same arm tiles, so the whole arm reads as one
                        // wooden shaft. Both caps are skipped: the head end
                        // meets the head arm's skipped cap at the boundary,
                        // the body end is coplanar with the recessed face.
                        if ( block->is_piston && blockState.current_redstone_power > 0 ) {
                            const int head_face = piston_head_face( blockState.rotation );
                            const unsigned char yaw = blockState.rotation & 3;
                            for ( int ax = 0; ax < size_x; ax++ ) {
                                for ( int ay = 0; ay < size_y; ay++ ) {
                                    for ( int az = 0; az < size_z; az++ ) {
                                        BlockCoords *arm =
                                            &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                                        num_instances[ block->renderOrder ] += 1;
                                        *arm = *blockCoord;
                                        arm->x = blockCoord->x + ax;
                                        arm->y = blockCoord->y + ay;
                                        arm->z = blockCoord->z + az;
                                        arm->mesh_x = 1;
                                        arm->mesh_y = 1;
                                        arm->mesh_z = 1;
                                        // Same interior-face rule as the
                                        // head shaft: force laterals lit,
                                        // then skip the end caps.
                                        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                                            arm->packed_lighting[ f ] = NO_LIGHT_BRIGHT;
                                        }
                                        arm->scale_x = 4;
                                        arm->scale_y = 4;
                                        arm->scale_z = 4;
                                        arm->offset_x = 6;
                                        arm->offset_z = 6;
                                        if ( head_face == FACE_TOP || head_face == FACE_BOTTOM ) {
                                            arm->offset_y = head_face == FACE_TOP ? 12 : 0;
                                            arm->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
                                            arm->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
                                        } else {
                                            arm->offset_y = 6;
                                            arm->offset_z = 12;
                                            arm->packed_lighting[ mesh_rotated_face( FACE_FRONT, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                                            arm->packed_lighting[ mesh_rotated_face( FACE_BACK, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                                        }
                                        // Same grain rule as the head arm.
                                        const bool vertical = head_face == FACE_TOP || head_face == FACE_BOTTOM;
                                        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                                            arm->face[ f ] = !vertical && ( f == FACE_LEFT || f == FACE_RIGHT ) ? TEX_PISTON_ARM_H : TEX_PISTON_ARM;
                                        }
                                    }
                                }
                            }
                        }

                        // Spine-shell blocks (cactus) replace the core box
                        // with Java's three-element model: a full-cell box
                        // drawing only top and bottom, plus two 14-wide
                        // boxes whose only faces are the side quads sitting
                        // 1px inside each cell edge. Every face samples its
                        // whole tile, so the side tile's opaque interior
                        // forms the inset body while its mostly-transparent
                        // edge columns read as spines poking out at the cell
                        // boundary; the quads cross at the corners (they're
                        // independent planes, not a real cube). The side
                        // quads live inside the cell so they never z-fight;
                        // top/bottom sit on the boundary and are culled when
                        // the neighbor's opposing face fully covers it —
                        // the same mesh_face_seethrough test phase 1 uses
                        // (Java's cullface), read from the halo cells the
                        // fence lookup already uses. Block scale/offset in
                        // the JSON describes the pick/collision box only.
                        if ( block->spine_shell ) {
                            const GPUBlockDef *mesh_defs = mesh_def_table( );
                            BlockCoords *qx = &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                            num_instances[ block->renderOrder ] += 1;
                            *qx = *blockCoord;
                            qx->scale_x = 14;
                            qx->scale_y = 16;
                            qx->scale_z = 16;
                            qx->offset_x = 1;
                            qx->offset_y = 0;
                            qx->offset_z = 0;
                            qx->tex_offset_x = 0;
                            qx->tex_offset_y = 0;
                            qx->tex_offset_z = 0;
                            qx->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
                            qx->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
                            qx->packed_lighting[ FACE_FRONT ] = NO_LIGHT_NO_DRAW;
                            qx->packed_lighting[ FACE_BACK ] = NO_LIGHT_NO_DRAW;

                            BlockCoords *qz = &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                            num_instances[ block->renderOrder ] += 1;
                            *qz = *blockCoord;
                            qz->scale_x = 16;
                            qz->scale_y = 16;
                            qz->scale_z = 14;
                            qz->offset_x = 0;
                            qz->offset_y = 0;
                            qz->offset_z = 1;
                            qz->tex_offset_x = 0;
                            qz->tex_offset_y = 0;
                            qz->tex_offset_z = 0;
                            qz->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
                            qz->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
                            qz->packed_lighting[ FACE_LEFT ] = NO_LIGHT_NO_DRAW;
                            qz->packed_lighting[ FACE_RIGHT ] = NO_LIGHT_NO_DRAW;

                            blockCoord->scale_x = 16;
                            blockCoord->scale_y = 16;
                            blockCoord->scale_z = 16;
                            blockCoord->offset_x = 0;
                            blockCoord->offset_y = 0;
                            blockCoord->offset_z = 0;
                            blockCoord->tex_offset_x = 0;
                            blockCoord->tex_offset_y = 0;
                            blockCoord->tex_offset_z = 0;
                            blockCoord->packed_lighting[ FACE_RIGHT ] = NO_LIGHT_NO_DRAW;
                            blockCoord->packed_lighting[ FACE_FRONT ] = NO_LIGHT_NO_DRAW;
                            blockCoord->packed_lighting[ FACE_LEFT ] = NO_LIGHT_NO_DRAW;
                            blockCoord->packed_lighting[ FACE_BACK ] = NO_LIGHT_NO_DRAW;
                            for ( int f = FACE_TOP; f <= FACE_BOTTOM; f++ ) {
                                const BlockCell ncell = this->cells[ get_index_from_coords( x, y + FACE_DIR_Y_OFFSETS[ f ], z ) ];
                                const int nrot = block_cell_rotation( ncell );
                                const int opposing_face = OPPOSITE_FACE[ mesh_rotated_face( f, nrot ) ];
                                if ( !mesh_face_seethrough( mesh_defs[ block_cell_id( ncell ) ], block_cell_powered( ncell ), nrot, opposing_face ) ) {
                                    blockCoord->packed_lighting[ f ] = NO_LIGHT_NO_DRAW;
                                }
                            }
                        }

                        // Multi-box models (fences, gates): box 0 overwrites
                        // the base instance; extra boxes copy it wholesale so
                        // they inherit its packed_lighting, then get their own
                        // scale/offset, tex_offset = -offset (the torch
                        // convention: texture position follows world
                        // position), and NO_LIGHT_NO_DRAW on skip_faces —
                        // rail ends buried in the post or a connected cell.
                        if ( num_boxes > 0 ) {
                            const unsigned char yaw = block_yaw( block, blockState.rotation );
                            for ( int i = 0; i < num_boxes; i++ ) {
                                BlockCoords *boxCoord = blockCoord;
                                if ( i > 0 ) {
                                    boxCoord = &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                                    num_instances[ block->renderOrder ] += 1;
                                    *boxCoord = *blockCoord;
                                }
                                boxCoord->scale_x = boxes[ i ].scale[ 0 ];
                                boxCoord->scale_y = boxes[ i ].scale[ 1 ];
                                boxCoord->scale_z = boxes[ i ].scale[ 2 ];
                                boxCoord->offset_x = boxes[ i ].offset[ 0 ];
                                boxCoord->offset_y = boxes[ i ].offset[ 1 ];
                                boxCoord->offset_z = boxes[ i ].offset[ 2 ];
                                boxCoord->tex_offset_x = -boxes[ i ].offset[ 0 ];
                                boxCoord->tex_offset_y = -boxes[ i ].offset[ 1 ];
                                boxCoord->tex_offset_z = -boxes[ i ].offset[ 2 ];
                                // The sign board sits at z 7-8 but its
                                // front/back faces span the full cell width —
                                // the depth-anchored window [7,23) would run
                                // off the tile, so pull it back to the
                                // full-width band at cols 0-15. The plate's
                                // top/bottom quads then sample rows 0-1 of
                                // the cap tile (planks cover them).
                                if ( block->is_standing_sign && i == 0 ) {
                                    boxCoord->tex_offset_z = 0;
                                }
                                boxCoord->face_shift = yaw;
                                // skip_faces is definition-space while
                                // packed_lighting indexes the world-space quad
                                // (faceType) the shader turns the box onto —
                                // a yawed box needs its skip bits rotated the
                                // same way. mesh_rotated_face maps world->def,
                                // so the def->world slot is its counter-yaw row
                                // (TOP/BOTTOM map to themselves at every yaw).
                                for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                                    if ( boxes[ i ].skip_faces & ( 1u << f ) ) {
                                        boxCoord->packed_lighting[ mesh_rotated_face( f, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                                    }
                                }
                            }
                        }

                    }

                    // A waterlogged block at the top of a water column (no
                    // liquid above it) still needs the water surface drawn on
                    // its cell — emit a water instance for it. This runs on the
                    // CPU for both meshers: the HIP kernel only fills
                    // workingSpace.
                    if ( block->waterloggable && blockState.waterlogged ) {
                        const BlockCell above_cell = this->cells[ get_index_from_coords( x, y + 1, z ) ];
                        if ( block_cell_id( above_cell ) != WATER && !block_cell_waterlogged( above_cell ) ) {
                            Block *water = block_definition_get_definition( WATER );
                            ensure_capacity( RenderOrder_Water, 1 );
                            if ( num_instances[ RenderOrder_Water ] + 1 > capacity[ RenderOrder_Water ] ) {
                                continue; // OOM
                            }
                            BlockCoords *waterCoord = &this->layers[ RenderOrder_Water ].populated_blocks[ num_instances[ RenderOrder_Water ] ];
                            num_instances[ RenderOrder_Water ] += 1;

                            waterCoord->x = this->chunk_pos.x * CHUNK_SIZE_X + x;
                            waterCoord->y = this->chunk_pos.y * CHUNK_SIZE_Y + y;
                            waterCoord->z = this->chunk_pos.z * CHUNK_SIZE_Z + z;

                            waterCoord->mesh_x = 1;
                            waterCoord->mesh_y = 1;
                            waterCoord->mesh_z = 1;
                            waterCoord->face_shift = BLOCK_COORDS_FLUID;
                            waterCoord->scale_x = water->scale.x;
                            waterCoord->scale_y = water->scale.y;
                            waterCoord->scale_z = water->scale.z;
                            waterCoord->offset_x = water->offset.x;
                            waterCoord->offset_y = water->offset.y;
                            waterCoord->offset_z = water->offset.z;
                            waterCoord->tex_offset_x = water->tex_offset.x;
                            waterCoord->tex_offset_y = water->tex_offset.y;
                            waterCoord->tex_offset_z = water->tex_offset.z;
                            // The cell behaves as a WATER source for the
                            // surface: corners + per-face visibility come
                            // from the same helpers the fluid cells use
                            // (the block's own instance is untouched).
                            const unsigned int corners = fluid_pack_corners( this->cells, x, y, z );
                            const bool fluid_flat = corners == FLUID_CORNERS_FLAT;
                            waterCoord->fluid_h0 = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( corners & 0xffu );
                            waterCoord->fluid_h[ 0 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 8 ) & 0xffu );
                            waterCoord->fluid_h[ 1 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 16 ) & 0xffu );
                            waterCoord->fluid_h[ 2 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 24 ) & 0xffu );
                            const unsigned int vis = fluid_face_vis_mask( this->cells, mesh_def_table( ), x, y, z, WATER );
                            for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                                waterCoord->face[ i ] = water->textures[ i ];
                                waterCoord->packed_lighting[ i ] = ( vis >> i ) & 1u ? NO_LIGHT_BRIGHT : NO_LIGHT_NO_DRAW;
                            }
                            if ( !fluid_flat ) {
                                waterCoord->face[ FACE_TOP ] = water->textures[ FACE_FRONT ];
                            }
                        }
                    }
                }
            }
        }
    }
    // workingSpace is thread-local and reused — do not free

#if TERRAIN_GEN_PROFILING
    t_phase2 = now_us( ) - t_ph1;
    profiling_mesh_greedy_us.fetch_add( t_phase2, std::memory_order_relaxed );
    long long total_instances = 0;
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        total_instances += num_instances[ renderOrder ];
    }
    profiling_instance_count.fetch_add( total_instances, std::memory_order_relaxed );
#endif

    if constexpr ( !REMEMBER_BLOCKS ) {
        this->free_block_storage( );
        free( this->edited );
        this->edited = nullptr;
        chunk_free_air_mask( this->light.air_mask );
        this->light.air_mask = nullptr;
        chunk_free_air_mask( this->light.shade_mask );
        this->light.shade_mask = nullptr;
    }
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        this->layers[ renderOrder ].num_instances = num_instances[ renderOrder ];
        // Copy the used instances out of the scratch buffer into a right-sized
        // allocation — populated_blocks must never point into the scratch past
        // this function, since program_terrain/unprogram_terrain free() it.
        if ( num_instances[ renderOrder ] == 0 ) {
            this->layers[ renderOrder ].populated_blocks = nullptr;
        } else {
            const int n = num_instances[ renderOrder ];
            BlockCoords *out = static_cast<BlockCoords *>( malloc( n * sizeof( BlockCoords ) ) );
            if ( out ) {
                memcpy( out, this->layers[ renderOrder ].populated_blocks, n * sizeof( BlockCoords ) );
            } else {
                pr_debug( "Error, out of memory copying populated_blocks" );
                this->layers[ renderOrder ].num_instances = 0;
            }
            this->layers[ renderOrder ].populated_blocks = out;
        }
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
                    bad |= ( b.face_shift & BLOCK_COORDS_ROTATION_MASK ) > 7u || ( b.face_shift & ~( BLOCK_COORDS_ROTATION_MASK | BLOCK_COORDS_INSIDE_OUT | BLOCK_COORDS_HALF_PIXEL | BLOCK_COORDS_FLUID ) ) != 0;
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
        free( renderLayer.populated_blocks );
        renderLayer.populated_blocks = nullptr;
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