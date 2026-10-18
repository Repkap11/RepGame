#include <string.h>
#include <chrono>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/map_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/structure_gen.hpp"
#if defined( REPGAME_BUILD_WITH_HIP )
#include "linux/hip/mesh_gen.hpp"
#endif

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
    // Water only has top/bottom faces and no reflect variant (the old path
    // left its ib_reflect empty, and the reflect pass skips water anyway).
    for ( unsigned int mask = 0; mask < 4; mask++ ) {
        s_ib_water_off[ mask ] = static_cast<unsigned int>( s_ib_data.size( ) ) * sizeof( unsigned int );
        unsigned int n = 0;
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
    chunk_ib_init_tables( );
    const unsigned int vis_top = this->chunk_pos.y <= center_next.y;
    const unsigned int vis_bottom = this->chunk_pos.y >= center_next.y;
    const unsigned int vis_right = this->chunk_pos.x <= center_next.x;
    const unsigned int vis_left = this->chunk_pos.x >= center_next.x;
    const unsigned int vis_front = this->chunk_pos.z <= center_next.z;
    const unsigned int vis_back = this->chunk_pos.z >= center_next.z;
    const unsigned int mask = vis_front | ( vis_right << 1 ) | ( vis_back << 2 ) | ( vis_left << 3 ) | ( vis_top << 4 ) | ( vis_bottom << 5 );
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
    const unsigned int water_mask = vis_top | ( vis_bottom << 1 );
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

void Chunk::init( const VertexBuffer &vb_block_solid, const VertexBuffer &vb_block_water, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords ) {
    s_vb_block_solid = &vb_block_solid;
    s_vb_block_water = &vb_block_water;
    s_vbl_block = &vbl_block;
    s_vbl_coords = &vbl_coords;
    // GL objects and the blocks array are created lazily (see ensure_gl_init /
    // load_terrain) so the startup loop over MAX_LOADED_CHUNKS doesn't block
    // the first frame with tens of thousands of glGen*/calloc calls.
    this->gl_initialized = 0;
}

void Chunk::ensure_gl_init( ) {
    if ( this->gl_initialized ) {
        return;
    }
    this->gl_initialized = 1;
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
    if constexpr ( REMEMBER_BLOCKS ) {
        chunk_free_blocks( this->blocks );
    }
    chunk_free_light( this->light );
    chunk_free_light_columns( this->light_columns );
    chunk_free_air_mask( this->light_air_mask );
    if ( this->light_texture ) {
        glDeleteTextures( 1, &this->light_texture );
        this->light_texture = 0;
    }

    for ( RenderLayer &layer : this->layers ) {
        if ( layer.populated_blocks ) {
            free( layer.populated_blocks );
        }
        RenderLayer &renderLayer = layer;
        renderLayer.populated_blocks = nullptr;
        renderLayer.vb_coords.destroy( );
        renderLayer.va.destroy( );
    }
}

void Chunk::draw( const Renderer &renderer, const Texture &texture, Shader &shader, RenderOrder renderOrder, bool draw_reflect, const glm::vec3 &render_origin, int light_base_loc ) {
    const RenderLayer &renderLayer = this->layers[ renderOrder ];
    if ( this->should_render && renderLayer.num_instances != 0 ) {
        if ( this->is_loading ) {
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
        if ( this->light_texture != 0 ) {
            // The light unit's glActiveTexture is hoisted to
            // ChunkLoader::draw — nothing between chunk draws reselects it.
            glBindTexture( GL_TEXTURE_3D, this->light_texture );
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
    if ( this->blocks == nullptr ) {
        // nullptr blocks means the whole chunk is AIR.
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
    return this->blocks[ get_index_from_coords( pos ) ];
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
    if ( this->blocks == nullptr ) {
        // All-air chunk: an AIR write changes nothing, so only materialize
        // the array when the new state actually differs from AIR.
        if ( BlockStates_equal( BLOCK_STATE_AIR, blockState ) ) {
            return;
        }
        this->blocks = chunk_alloc_blocks( );
        // The fresh blocks[] is calloc'd = all AIR = all non-opaque, so its
        // air mask starts fully set; the write below stamps its own cell.
        if ( light_mipmaps_enabled( ) ) {
            if ( this->light_air_mask == nullptr ) {
                this->light_air_mask = chunk_alloc_air_mask( );
            }
            memset( this->light_air_mask, 0xFF, CHUNK_BLOCK_SIZE / 8 );
        }
    }

    const int index = get_index_from_coords( pos );
    const BlockState old_state = this->blocks[ index ];
    if ( block_is_chest( old_state.id ) && !block_is_chest( blockState.id ) ) {
        this->chests.erase( index );
    }
    this->blocks[ index ] = block_state_normalize( old_state, blockState );
    light_air_mask_set( *this, index, this->blocks[ index ].id );
}

int Chunk::set_block_by_index_if_different( int index, const BlockState *blockState ) {
    // Defensive bounds check: index comes from the network (multiplayer chunk
    // diffs). An out-of-range value would write to arbitrary chunk memory and
    // corrupt the world. Drop the update silently rather than crash.
    if ( index < 0 || index >= CHUNK_BLOCK_SIZE ) {
        pr_debug( "Ignoring out-of-range block index:%d (max:%d)", index, CHUNK_BLOCK_SIZE );
        return 0;
    }
    if ( this->blocks == nullptr ) {
        // All-air chunk: an AIR write is a no-op; anything else allocates.
        if ( BlockStates_equal( BLOCK_STATE_AIR, *blockState ) ) {
            return 0;
        }
        this->blocks = chunk_alloc_blocks( );
        // Same as set_block: fresh calloc'd blocks[] = all non-opaque.
        if ( light_mipmaps_enabled( ) ) {
            if ( this->light_air_mask == nullptr ) {
                this->light_air_mask = chunk_alloc_air_mask( );
            }
            memset( this->light_air_mask, 0xFF, CHUNK_BLOCK_SIZE / 8 );
        }
    }
    BlockState &oldBlockState = this->blocks[ index ];
    if ( BlockStates_equal( oldBlockState, *blockState ) ) {
        return 0;
    }
    if ( block_is_chest( oldBlockState.id ) && !block_is_chest( blockState->id ) ) {
        this->chests.erase( index );
    }
    this->blocks[ index ] = *blockState;
    light_air_mask_set( *this, index, blockState->id );
    this->dirty = true;
    this->needs_repopulation = true;
    // Record the change for the light engine; converted to a world-space
    // recheck seed by the lighting thread (ChunkLoader::light_drain_pending).
    if ( this->light_pending_count < static_cast<int>( sizeof( this->light_pending ) / sizeof( this->light_pending[ 0 ] ) ) ) {
        this->light_pending[ this->light_pending_count++ ] = index;
    } else {
        this->light_reseed = 1;
    }
    light_pending_enqueue( *this );
    this->is_empty_chunk = false;
    // Ensure the chunk enters the drawable list so the remeshing loop visits
    // it. An empty chunk has should_render == 0; without this, a block placed
    // in it (e.g. via a multiplayer chunk diff) wouldn't render until reload.
    this->should_render = 1;
    return 1;
}

void Chunk::persist( MapStorage &map_storage ) const {
    if constexpr ( REMEMBER_BLOCKS ) {
        map_storage.persist_chunk( *this );
    }
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

void Chunk::load_terrain( MapStorage &map_storage ) {
    if constexpr ( !REMEMBER_BLOCKS ) {
        if ( this->blocks == nullptr ) {
            this->blocks = chunk_alloc_blocks( );
        }
    }
    // Under REMEMBER_BLOCKS, blocks stays null while the chunk is all-air:
    // MapStorage::load_chunk allocates it only when a save file exists, and
    // Chunk::set_block allocates on the first real write (every existing cell
    // is AIR anyway). light[] likewise stays unallocated for uniform volumes —
    // light_fill_chunk folds constant fills to Chunk::light_uniform instead.
    if ( this->light_columns == nullptr ) {
        this->light_columns = chunk_alloc_light_columns( );
    } else {
        memset( this->light_columns, 0, 3 * LIGHT_FLAGS_COUNT );
    }
    this->light_pending_count = 0;
    this->light_reseed = 0;
    this->light_stale = 0;
    this->light_pending_listed = 0;
    this->light_upload_listed = 0;
    this->light_write_tick.store( 0, std::memory_order_relaxed );
    this->light_upload_defers = 0;
    this->light_dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    this->light_uniform = 0;
    this->is_empty_chunk = false;
#if TERRAIN_GEN_PROFILING
    long long t_start = now_us( );
    long long t_load_disk = 0, t_map_gen = 0, t_struct = 0, t_pop = 0;
    long long t0 = now_us( );
#endif
    // pr_debug( "Loading chunk terrain x:%d y:%d z:%d", this->chunk_x, this->chunk_y, this->chunk_z );
    const int loaded = map_storage.load_chunk( *this );
#if TERRAIN_GEN_PROFILING
    t_load_disk = now_us( ) - t0;
    if ( loaded ) profiling_disk_load_count.fetch_add( 1, std::memory_order_relaxed );
#endif
    if constexpr ( REMEMBER_BLOCKS ) {
        if ( loaded && this->blocks != nullptr ) {
            // A saved chunk can decode to all-air (e.g. mined flat). Fold it to
            // the null representation so it skips meshing like a generated
            // air chunk — the file stays valid as-is (nothing rewrites it).
            bool all_air = true;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                if ( this->blocks[ i ].id != AIR ) {
                    all_air = false;
                    break;
                }
            }
            if ( all_air ) {
                chunk_free_blocks( this->blocks );
                this->blocks = nullptr;
                chunk_free_air_mask( this->light_air_mask );
                this->light_air_mask = nullptr;
                this->is_empty_chunk = true;
                goto skip_to_mesh;
            }
        }
    }
    if ( !loaded ) {
        // We haven't loaded this chunk before, map gen it.
        // Cheap CPU-side check: if the entire chunk is above the terrain surface,
        // every block is air. Skip GPU map gen, structure gen, and meshing entirely.
        const int min_y = this->chunk_pos.y * CHUNK_SIZE_Y - 1; // include -1 border
        if ( min_y >= WATER_LEVEL ) {
            // If the chunk is above the theoretical max terrain height + the
            // reach of a tree growing on it, it's guaranteed empty with zero
            // perlin noise evaluation. The bound is derived from the terrain
            // formulas in map_gen.cpp — see MapGen::maxTerrainHeight().
            if ( (float)min_y >= MapGen::maxTerrainHeight( ) + MAPGEN_MAX_TREE_REACH ) {
                // Entire chunk is air — keep blocks null instead of filling a
                // 262K-cell array with AIR.
                chunk_free_blocks( this->blocks );
                this->blocks = nullptr;
                chunk_free_air_mask( this->light_air_mask );
                this->light_air_mask = nullptr;
                this->is_empty_chunk = true;
                this->dirty = 0;
#if TERRAIN_GEN_PROFILING
                t_map_gen = now_us( ) - t0;
                profiling_map_gen_count.fetch_add( 1, std::memory_order_relaxed );
#endif
                goto skip_to_mesh;
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
                chunk_free_blocks( this->blocks );
                this->blocks = nullptr;
                chunk_free_air_mask( this->light_air_mask );
                this->light_air_mask = nullptr;
                this->is_empty_chunk = true;
                this->dirty = 0;
#if TERRAIN_GEN_PROFILING
                t_map_gen = now_us( ) - t0;
                profiling_map_gen_count.fetch_add( 1, std::memory_order_relaxed );
#endif
                goto skip_to_mesh;
            }
        }
#if TERRAIN_GEN_PROFILING
        t0 = now_us( );
#endif
        if ( this->blocks == nullptr ) {
            this->blocks = chunk_alloc_blocks( );
        }
        if ( LOAD_CHUNKS_SUPPORTS_CUDA && MapGen::supports_cuda( ) ) {
            if ( firstTime ) {
                firstTime = 0;
                pr_debug( "Using CUDA" );
            }
            MapGen::load_block_cuda( this );
        } else if ( LOAD_CHUNKS_SUPPORTS_HIP && MapGen::supports_hip( ) && getenv( "REPGAME_NO_HIP_MAPGEN" ) == nullptr ) {
            if ( firstTime ) {
                firstTime = 0;
                pr_debug( "Using HIP" );
            }
            if ( !MapGen::load_block_hip( this ) ) {
                // HIP failed (e.g. no code object for this GPU's arch); fall
                // back to the CPU path instead of leaving blocks uninitialized.
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
skip_to_mesh:
    // Chest contents live in a per-chunk sidecar file (world-pos keyed);
    // convert to the block-index keying used by chests[].
    this->chests.clear( );
    ChestContentsMap chest_world_map;
    this->chests_loaded = map_storage.load_chests( this->chunk_pos, chest_world_map ) ? 1 : 0;
    for ( const auto &pair : chest_world_map ) {
        const glm::ivec3 diff = pair.first - this->chunk_pos * CHUNK_SIZE_I;
        // Entries must fall inside this chunk (file corruption guard).
        if ( diff.x < 0 || diff.x >= CHUNK_SIZE_X || diff.y < 0 || diff.y >= CHUNK_SIZE_Y || diff.z < 0 || diff.z >= CHUNK_SIZE_Z ) {
            continue;
        }
        this->chests[ Chunk::get_index_from_coords( diff ) ] = pair.second;
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
    // if ( blockState.id == REDSTONE_CROSS ) {
    //     pr_debug( "Trying to mesh" );
    // }
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
                BlockState new_blockState = this->blocks[ index ];
                if ( !BlockStates_equal( new_blockState, blockState ) ) {
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


// Precomputed rotation table: ROTATED_FACE[rotation][face]
// FACE_ROTATE_90 = { FACE_TOP, FACE_BOTTOM, FACE_BACK, FACE_RIGHT, FACE_FRONT, FACE_LEFT }
static constexpr int ROTATED_FACE_TABLE[ 4 ][ NUM_FACES_IN_CUBE ] = {
    { FACE_TOP, FACE_BOTTOM, FACE_RIGHT, FACE_FRONT, FACE_LEFT, FACE_BACK }, // rotation 0
    { FACE_TOP, FACE_BOTTOM, FACE_BACK, FACE_RIGHT, FACE_FRONT, FACE_LEFT }, // rotation 1
    { FACE_TOP, FACE_BOTTOM, FACE_LEFT, FACE_BACK, FACE_RIGHT, FACE_FRONT }, // rotation 2
    { FACE_TOP, FACE_BOTTOM, FACE_FRONT, FACE_LEFT, FACE_BACK, FACE_RIGHT }, // rotation 3
};

inline int get_rotated_face( const int face, const int rotation ) {
    return ROTATED_FACE_TABLE[ rotation & 3 ][ face ];
}

// Whether `block`'s def-space face (post get_rotated_face) leaves its cell
// boundary partially open. Extended piston bases recess 4px at the head end,
// so every face except the covered back face lets neighbor faces show through.
// Only used for the covering-block lookups; the block's own faces keep the
// static table so a buried base doesn't z-fight its neighbors on the part of
// the boundary it still covers.
static inline bool face_is_seethrough( const Block *block, const BlockState &state, int face ) {
    if ( block->is_piston && state.current_redstone_power > 0 ) {
        return face != piston_base_covered_face( state.rotation );
    }
    return block->calculated.is_seethrough_face[ face ];
}

void Chunk::calculate_populated_blocks( ) {
    int num_instances[ LAST_RENDER_ORDER ] = { 0 };

    // Empty chunks (all air): skip meshing entirely — and skip the
    // populated_blocks mallocs + workingSpace memset, which are only needed
    // when real blocks exist.
    if ( this->is_empty_chunk ) {
        if constexpr ( !REMEMBER_BLOCKS ) {
            chunk_free_blocks( this->blocks );
            this->blocks = nullptr;
            chunk_free_air_mask( this->light_air_mask );
            this->light_air_mask = nullptr;
        }
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            this->layers[ renderOrder ].num_instances = 0;
        }
        return;
    }

    // populated_blocks are filled sequentially (only num_instances entries are read later),
    // so malloc suffices — no need to zero ~18.8MB per chunk.
    for ( RenderLayer &layer : this->layers ) {
        if ( layer.populated_blocks ) {
            pr_debug( "Error, populated blocks already populated" );
        }
        layer.populated_blocks = static_cast<BlockCoords *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockCoords ) ) );
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
    if ( LOAD_CHUNKS_SUPPORTS_HIP && MapGen::supports_hip( ) && getenv( "REPGAME_NO_HIP_MESH" ) == nullptr ) {
        used_gpu_mesh = mesh_gen_calculate_hip( this->blocks, workingSpace );
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
    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        if ( Chunk::get_coords_from_index( index, x, y, z ) ) {
            BlockState blockState = this->blocks[ index ];
            BlockID blockID = blockState.id;
            Block *block = block_definition_get_definition( blockID );

            workingSpace[ index ].visible = render_order_is_visible( block->renderOrder );

            if ( workingSpace[ index ].visible ) {
                int block_is_visiable = 0;
                int visible_from[ NUM_FACES_IN_CUBE ] = { 0, 0, 0, 0, 0, 0 };
                for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
                    int rotated_face = get_rotated_face( face, blockState.rotation );
                    BlockState block_next_to_state = this->blocks[ get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face ], y + FACE_DIR_Y_OFFSETS[ face ], z + FACE_DIR_Z_OFFSETS[ face ] ) ];

                    Block *block_next_to = block_definition_get_definition( block_next_to_state.id );

                    int opposing_face = get_rotated_face( face, block_next_to_state.rotation );
                    opposing_face = OPPOSITE_FACE[ opposing_face ];

                    bool is_seethrough = block->calculated.is_seethrough_face[ rotated_face ];
                    if ( is_seethrough && block->calculated.hides_self[ rotated_face ] ) { // water and glass
                        // Waterlogged cells count as water: water (and lava)
                        // draws no surface faces against them.
                        const bool liquid_boundary = block->renderOrder == RenderOrder_Water && block_next_to_state.waterlogged;
                        if ( face == FACE_TOP && block->renderOrder == RenderOrder_Water ) {
                            visible_from[ face ] = block_next_to->id != block->id && !liquid_boundary;
                        } else {
                            visible_from[ face ] = face_is_seethrough( block_next_to, block_next_to_state, opposing_face ) && block_next_to_state.id != blockID && !liquid_boundary;
                        }
                    } else if ( is_seethrough ) { // The current block is seethough on this face. Check all the directions not opposing it;
                        bool visable_through_opposing = false;
                        for ( int face_around = FACE_TOP; face_around < NUM_FACES_IN_CUBE; face_around++ ) {
                            if ( face_around == opposing_face ) {
                                // It's the non_opposing_face and parallel with this face, don't count it
                                continue;
                            }
                            BlockState block_around_state = this->blocks[ get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face_around ], y + FACE_DIR_Y_OFFSETS[ face_around ], z + FACE_DIR_Z_OFFSETS[ face_around ] ) ];
                            Block *block_around = block_definition_get_definition( block_around_state.id );
                            int around_face = get_rotated_face( face_around, block_around_state.rotation );
                            around_face = OPPOSITE_FACE[ around_face ];
                            if ( face_is_seethrough( block_around, block_around_state, around_face ) ) {
                                visable_through_opposing = true;
                                break;
                            }
                        }
                        visible_from[ face ] = visable_through_opposing;

                    } else {
                        bool visable_through_opposing = face_is_seethrough( block_next_to, block_next_to_state, opposing_face );
                        visible_from[ face ] = visable_through_opposing;
                    }
                    block_is_visiable |= visible_from[ face ];
                }
                {
                    // Per-vertex corner AO removed: the flood-fill light
                    // volume already darkens crevices, so visible faces are
                    // packed as fully lit (NO_LIGHT_BRIGHT). Hidden faces keep
                    // the block's no_light sentinel (NO_LIGHT_NO_DRAW clips the
                    // quad in the shader; leaf interiors use NO_LIGHT_DRAW).
                    const unsigned int lit = render_order_can_be_shaded( block->renderOrder ) ? NO_LIGHT_BRIGHT : block->no_light;
                    for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                        workingSpace[ index ].packed_lighting[ i ] = visible_from[ i ] ? lit : block->no_light;
                    }
                }
                workingSpace[ index ].can_be_seen = block_is_visiable;
            } else {
                workingSpace[ index ].can_be_seen = false;
            }
        }
    }
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
    for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                int index = get_index_from_coords( x, y, z );
                if ( !workingSpace[ index ].has_been_drawn ) {

                    BlockState blockState = this->blocks[ index ];
                    Block *block = block_definition_get_definition( blockState.display_id );
                    // if ( blockState.display_id == REDSTONE_CROSS ) {
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

                        if ( blockState.rotation == BLOCK_ROTATE_0 || blockState.rotation == BLOCK_ROTATE_180 ) {
                            block_can_mesh_x = block->calculated.can_mesh_x;
                            block_can_mesh_y = block->calculated.can_mesh_y;
                            block_can_mesh_z = block->calculated.can_mesh_z;
                        } else {
                            block_can_mesh_x = block->calculated.can_mesh_z;
                            block_can_mesh_y = block->calculated.can_mesh_y;
                            block_can_mesh_z = block->calculated.can_mesh_x;
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

                        BlockCoords *blockCoord;

                        blockCoord = &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                        num_instances[ block->renderOrder ] += 1;

                        blockCoord->x = this->chunk_pos.x * CHUNK_SIZE_X + x;
                        blockCoord->y = this->chunk_pos.y * CHUNK_SIZE_Y + y;
                        blockCoord->z = this->chunk_pos.z * CHUNK_SIZE_Z + z;

                        blockCoord->mesh_x = size_x;
                        blockCoord->mesh_y = size_y;
                        blockCoord->mesh_z = size_z;
                        blockCoord->face_shift = blockState.rotation;
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

                        // Render redstone dust on the sides of dust blocks for vertical connections.
                        // When dust has air to the side and dust below that air, the dust's side face
                        // is visible and shows a dust strip on the side of the solid block below.
                        if ( block->is_redstone_dust ) {
                            // Use LINE_2 (perpendicular to LINE_1) so the line appears vertical on side faces
                            BlockID side_texture = blockState.current_redstone_power > 0 ? REDSTONE_LINE_2_POWERED : REDSTONE_LINE_2_UNPOWERED;
                            const int horiz_dx[ 4 ] = { 1, -1, 0, 0 };
                            const int horiz_dz[ 4 ] = { 0, 0, 1, -1 };
                            const int horiz_face[ 4 ] = { FACE_RIGHT, FACE_LEFT, FACE_FRONT, FACE_BACK };
                            bool has_vertical_connection = false;
                            for ( int d = 0; d < 4; d++ ) {
                                int nx = x + horiz_dx[ d ];
                                int nz = z + horiz_dz[ d ];
                                BlockState neighbor = this->blocks[ get_index_from_coords( nx, y, nz ) ];
                                const Block *neighbor_block = block_definition_get_definition( neighbor.display_id );
                                // Only render side face when neighbor is non-solid and not dust (air)
                                if ( !neighbor_block->collides_with_player && !neighbor_block->is_redstone_dust ) {
                                    BlockState below = this->blocks[ get_index_from_coords( nx, y - 1, nz ) ];
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

                        // They are offset by 1 in the shader...
                        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                            blockCoord->face[ i ] = blockCoord->face[ i ] - 1;
                        }
                    }

                    // A waterlogged block at the top of a water column (no
                    // liquid above it) still needs the water surface drawn on
                    // its cell — emit a water instance for it. This runs on the
                    // CPU for both meshers: the HIP kernel only fills
                    // workingSpace.
                    if ( block->waterloggable && blockState.waterlogged ) {
                        const BlockState above_state = this->blocks[ get_index_from_coords( x, y + 1, z ) ];
                        if ( above_state.id != WATER && !above_state.waterlogged ) {
                            Block *water = block_definition_get_definition( WATER );
                            BlockCoords *waterCoord = &this->layers[ RenderOrder_Water ].populated_blocks[ num_instances[ RenderOrder_Water ] ];
                            num_instances[ RenderOrder_Water ] += 1;

                            waterCoord->x = this->chunk_pos.x * CHUNK_SIZE_X + x;
                            waterCoord->y = this->chunk_pos.y * CHUNK_SIZE_Y + y;
                            waterCoord->z = this->chunk_pos.z * CHUNK_SIZE_Z + z;

                            waterCoord->mesh_x = 1;
                            waterCoord->mesh_y = 1;
                            waterCoord->mesh_z = 1;
                            waterCoord->face_shift = BLOCK_ROTATE_0;
                            waterCoord->scale_x = water->scale.x;
                            waterCoord->scale_y = water->scale.y;
                            waterCoord->scale_z = water->scale.z;
                            waterCoord->offset_x = water->offset.x;
                            waterCoord->offset_y = water->offset.y;
                            waterCoord->offset_z = water->offset.z;
                            waterCoord->tex_offset_x = water->tex_offset.x;
                            waterCoord->tex_offset_y = water->tex_offset.y;
                            waterCoord->tex_offset_z = water->tex_offset.z;
                            for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                                waterCoord->face[ i ] = water->textures[ i ] - 1;
                                waterCoord->packed_lighting[ i ] = i == FACE_TOP ? NO_LIGHT_BRIGHT : NO_LIGHT_NO_DRAW;
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
        chunk_free_blocks( this->blocks );
        chunk_free_air_mask( this->light_air_mask );
        this->light_air_mask = nullptr;
    }
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        this->layers[ renderOrder ].num_instances = num_instances[ renderOrder ];
        // populated_blocks was allocated at full CHUNK_BLOCK_SIZE capacity but
        // only num_instances entries were used. Shrink it before the chunk
        // sits in the worker->render result queue, where the excess would be
        // held until program_terrain frees it.
        if ( num_instances[ renderOrder ] == 0 ) {
            free( this->layers[ renderOrder ].populated_blocks );
            this->layers[ renderOrder ].populated_blocks = nullptr;
        } else {
            BlockCoords *shrunk = static_cast<BlockCoords *>( realloc( this->layers[ renderOrder ].populated_blocks, num_instances[ renderOrder ] * sizeof( BlockCoords ) ) );
            if ( shrunk ) {
                this->layers[ renderOrder ].populated_blocks = shrunk;
            }
        }
    }
}

void Chunk::program_terrain( ) {
    // REPGAME_VALIDATE_INSTANCES=1: sanity-check every BlockCoords before
    // upload. Catches host-memory corruption (stray DMA writes from
    // concurrent GPU compute) before it reaches the GPU as garbage
    // instances / stray fragments.
    static const int validate_instances = getenv( "REPGAME_VALIDATE_INSTANCES" ) != nullptr;
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        RenderLayer &renderLayer = this->layers[ renderOrder ];
        if ( renderLayer.num_instances != 0 ) {
            if ( validate_instances ) {
                static int reported = 0;
                const int ox = this->chunk_pos.x * CHUNK_SIZE_X;
                const int oy = this->chunk_pos.y * CHUNK_SIZE_Y;
                const int oz = this->chunk_pos.z * CHUNK_SIZE_Z;
                for ( int i = 0; i < renderLayer.num_instances && reported < 40; i++ ) {
                    const BlockCoords &b = renderLayer.populated_blocks[ i ];
                    int bad = b.mesh_x < 1 || b.mesh_x > 32 || b.mesh_y < 1 || b.mesh_y > 32 || b.mesh_z < 1 || b.mesh_z > 32;
                    bad |= b.face_shift > 7u;
                    bad |= ( b.x < ox - 1.0f ) || ( b.x > ox + CHUNK_SIZE_X + 1.0f );
                    bad |= ( b.y < oy - 1.0f ) || ( b.y > oy + CHUNK_SIZE_Y + 1.0f );
                    bad |= ( b.z < oz - 1.0f ) || ( b.z > oz + CHUNK_SIZE_Z + 1.0f );
                    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                        bad |= b.face[ f ] > 1500;
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
            this->should_render = 1;
        }
        free( renderLayer.populated_blocks );
        renderLayer.populated_blocks = nullptr;
    }
}

void Chunk::unprogram_terrain( ) {
    this->should_render = 0;
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        RenderLayer &renderLayer = this->layers[ renderOrder ];
        if ( renderLayer.populated_blocks ) {
            free( renderLayer.populated_blocks );
        }
        renderLayer.populated_blocks = nullptr;
    }
}