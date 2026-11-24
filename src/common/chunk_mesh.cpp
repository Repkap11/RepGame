// Worker-side chunk meshing: the phase-1 visibility/packed-lighting pass
// (CPU body of mesh_vis_logic.hpp, or GPU via HIP/WebGPU readback), fluid
// corner heights, the greedy-merge rect extension, per-cell instance
// emission through block_mesh_emit, and the async-remesh job wrappers.
// Split out of chunk.cpp; build_mesh stays pure over a cells/extras
// snapshot so several workers can mesh concurrently.
#include <string.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>

#include "common/chunk.hpp"
#include "common/chunk_render.hpp"
#include "common/constants.hpp"
#include "common/fluid.hpp"
#include "common/block.hpp"
#include "common/map_gen.hpp"
#include "common/mesh_gen.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/terrain_loading_thread.hpp"
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

#if TERRAIN_GEN_PROFILING
// Mesh-phase counters — defined here (the writer) and extern'd by
// chunk_load.cpp, whose load_terrain stats print reads them. Same pattern
// as terrain_persist_us.
std::atomic<long long> profiling_mesh_vis_light_us{ 0 };
std::atomic<long long> profiling_mesh_greedy_us{ 0 };
std::atomic<long long> profiling_instance_count{ 0 };

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}
#endif

// ---- Fluid corner heights -------------------------------------------------
// Milestone-3 sloped fluids: per-cell corner heights in 1/64 units, packed
// as four bytes in BlockCoords::fluid_h0/fluid_h order — corner byte i is
// the corner (sx, sz) with sx = i&1, sz = i>>1, i.e. (x=0,z=0), (x=1,z=0),
// (x=0,z=1), (x=1,z=1). Filled once per chunk mesh so emission and the
// greedy-merge check stay cheap.
// FLAT = all four corners at FLUID_SOURCE_HEIGHT — the mergeable case;
// NOT_FLUID marks non-fluid cells (never read, but keeps the array defined).
// The chunk mesh sink for the shared emitter (block_mesh_emit.cpp): writes
// sequentially into the render order's scratch buffer the capacity check
// just guaranteed.
struct ChunkEmitCtx {
    BlockCoords *buf;
    int *n;
};
static BlockCoords *chunk_emit_alloc( void *vctx ) {
    ChunkEmitCtx *ctx = static_cast<ChunkEmitCtx *>( vctx );
    return &ctx->buf[ ( *ctx->n )++ ];
}
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
static unsigned int fluid_face_vis_mask( const BlockCell *cells, const GPUBlockDef *defs, int x, int y, int z, const BlockID fluid, unsigned int *boundary_out = nullptr ) {
    unsigned int vis = 0;
    unsigned int boundary = 0;
    for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
        const BlockCell nc = cells[ Chunk::get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face ], y + FACE_DIR_Y_OFFSETS[ face ], z + FACE_DIR_Z_OFFSETS[ face ] ) ];
        const BlockID nid = block_cell_id( nc );
        const int nrot = block_cell_rotation( nc );
        const GPUBlockDef &ndef = defs[ nid ];
        // Same liquid_boundary rule as mesh_vis_logic.hpp: waterlogged
        // cells and translucent neighbours both hide the water-pass face —
        // the fluid index buffer draws both windings, so a quad there
        // would render its mirror on the neighbour side and its depth
        // write would eat the coplanar glass face. film_vis flags the
        // face for a single-sided copy in the order-0 bucket instead
        // (emit_boundary_films), drawn after water but before the
        // translucent pass.
        int liquid_boundary = block_cell_waterlogged( nc ) != 0;
        const bool translucent = ndef.renderOrder == RenderOrder_Translucent;
        liquid_boundary |= translucent;
        int face_vis;
        int film_vis;
        if ( face == FACE_TOP ) {
            face_vis = nid != fluid && !liquid_boundary;
            film_vis = nid != fluid && translucent;
        } else {
            const int opposing_face = OPPOSITE_FACE[ mesh_rotated_face( face, nrot ) ];
            const int see_through = mesh_face_seethrough( ndef, block_cell_powered( nc ), nrot, opposing_face ) != 0 && nid != fluid;
            face_vis = see_through && !liquid_boundary;
            film_vis = see_through && translucent;
        }
        vis |= static_cast<unsigned int>( face_vis != 0 ) << face;
        boundary |= static_cast<unsigned int>( film_vis != 0 ) << face;
    }
    if ( boundary_out != nullptr ) {
        *boundary_out = boundary;
    }
    return vis;
}

int Chunk::can_extend_rect( const BlockCell *cells, const BlockExtra *extras, const BlockState &blockState, const unsigned int *packed_lighting, const WorkingSpace *workingSpace, const glm::ivec3 &starting, const glm::ivec3 &size, const glm::ivec3 &dir ) {
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
                const BlockState new_blockState = block_state_unpack( cells[ index ], extras ? &extras[ index ] : nullptr );
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
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        if ( this->layers[ renderOrder ].populated_blocks ) {
            pr_debug( "Error, populated blocks already populated" );
        }
    }
    ChunkMeshResult result;
    Chunk::build_mesh( this->cells, this->extras, this->chunk_pos, result );
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
        this->layers[ renderOrder ].num_instances = result.num[ renderOrder ];
        this->layers[ renderOrder ].populated_blocks = result.blocks[ renderOrder ];
    }
}

// The pure half of meshing: identical output to what
// calculate_populated_blocks used to write into layers[], but reading a
// caller-provided cells/extras snapshot and returning malloc'd arrays in
// `out` so async server-diff remesh jobs can run on the terrain workers.
void Chunk::build_mesh( const BlockCell *cells, const BlockExtra *extras, const glm::ivec3 &chunk_pos, ChunkMeshResult &out ) {
    PROFILE_SCOPE( "chunk_mesh" );
    // Cell access matching Chunk::state_at against the snapshot arrays.
    const auto state_of = [ cells, extras ]( int index ) {
        return block_state_unpack( cells[ index ], extras ? &extras[ index ] : nullptr );
    };
    // cells == nullptr meshes an all-air chunk: every order empty.
    if ( cells == nullptr ) {
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            out.num[ renderOrder ] = 0;
            out.blocks[ renderOrder ] = nullptr;
        }
        return;
    }
    // Meshing writes instances into a per-render-order thread-local scratch
    // buffer (same lifetime model as tls_workingSpace below): lazily malloc'd
    // at CHUNK_BLOCK_SIZE on first use and never freed, so chunks don't pay
    // a multi-megabyte malloc + page faults per mesh. The used instances are
    // memcpy'd into a right-sized malloc at the end.
    static thread_local BlockCoords *tls_mesh_scratch[ LAST_RENDER_ORDER ] = { nullptr };
    static thread_local int tls_mesh_scratch_cap[ LAST_RENDER_ORDER ] = { 0 };
    int num_instances[ LAST_RENDER_ORDER ] = { 0 };
    // The scratch pointer currently emitting into each order (== the
    // populated_blocks role the old in-place code used).
    BlockCoords *emit_buf[ LAST_RENDER_ORDER ];
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
        BlockCoords *grown = static_cast<BlockCoords *>( realloc( emit_buf[ renderOrder ], new_capacity * sizeof( BlockCoords ) ) );
        if ( grown == nullptr ) {
            pr_debug( "Error, out of memory growing populated_blocks" );
            return;
        }
        emit_buf[ renderOrder ] = grown;
        capacity[ renderOrder ] = new_capacity;
        tls_mesh_scratch[ renderOrder ] = grown;
        tls_mesh_scratch_cap[ renderOrder ] = new_capacity;
    };

    // populated_blocks are filled sequentially (only num_instances entries are read
    // later) into the scratch buffer — no need to zero ~18.8MB per chunk.
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        if ( !tls_mesh_scratch[ renderOrder ] ) {
            tls_mesh_scratch[ renderOrder ] = static_cast<BlockCoords *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockCoords ) ) );
            tls_mesh_scratch_cap[ renderOrder ] = CHUNK_BLOCK_SIZE;
        }
        emit_buf[ renderOrder ] = tls_mesh_scratch[ renderOrder ];
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
        used_gpu_mesh = mesh_gen_calculate_hip( cells, workingSpace );
    }
#endif
#if defined( REPGAME_WASM )
    static const bool no_webgpu_mesh = env_is_set( "REPGAME_NO_WEBGPU_MESH" );
    if ( !used_gpu_mesh && LOAD_CHUNKS_SUPPORTS_WEBGPU && !no_webgpu_mesh && MapGen::supports_webgpu( ) ) {
        used_gpu_mesh = mesh_gen_calculate_webgpu( cells, workingSpace );
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
#define MESH_CELL_AT( i ) ( (unsigned)( cells[ i ] ) )
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
    // Water-boundary films: a fluid face against a translucent block is
    // culled from the water pass (liquid_boundary) — the fluid index
    // buffer draws both windings, so a quad at the boundary would render
    // its mirror on the neighbour side, and its depth write would eat the
    // coplanar glass face. The replacement is a single-sided copy in the
    // order-0 bucket — meshed terrain never uses it — which world.cpp
    // draws after water but before the translucent pass with depth writes
    // off, so the glass still alpha-blends over it like anything else seen
    // through a pane. The film index table (chunk_render.cpp) carries one
    // winding per face, so from the water side nothing renders and the
    // glass's own face shows — matching the opaque-glass view. Every
    // would-be-visible face gets a film — water on both sides of a pane
    // shows the boundary surface through it, same as regular glass.
    Block *water_block = block_definition_get_definition( WATER );
    auto emit_boundary_films = [ & ]( int x, int y, int z, unsigned int boundary, unsigned int corners ) {
        const bool fluid_flat = corners == FLUID_CORNERS_FLAT;
        for ( int f = FACE_TOP; f < NUM_FACES_IN_CUBE; f++ ) {
            if ( !( boundary & ( 1u << f ) ) ) {
                continue;
            }
            ensure_capacity( RenderOrder_Transparent, 1 );
            if ( num_instances[ RenderOrder_Transparent ] + 1 > capacity[ RenderOrder_Transparent ] ) {
                break; // OOM
            }
            BlockCoords *film = &emit_buf[ RenderOrder_Transparent ][ num_instances[ RenderOrder_Transparent ] ];
            num_instances[ RenderOrder_Transparent ] += 1;
            film->x = chunk_pos.x * CHUNK_SIZE_X + x;
            film->y = chunk_pos.y * CHUNK_SIZE_Y + y;
            film->z = chunk_pos.z * CHUNK_SIZE_Z + z;
            film->mesh_x = 1;
            film->mesh_y = 1;
            film->mesh_z = 1;
            film->face_shift = BLOCK_COORDS_FLUID;
            film->scale_x = water_block->scale.x;
            film->scale_y = water_block->scale.y;
            film->scale_z = water_block->scale.z;
            film->offset_x = water_block->offset.x;
            film->offset_y = water_block->offset.y;
            film->offset_z = water_block->offset.z;
            film->tex_offset_x = water_block->tex_offset.x;
            film->tex_offset_y = water_block->tex_offset.y;
            film->tex_offset_z = water_block->tex_offset.z;
            film->fluid_h0 = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( corners & 0xffu );
            film->fluid_h[ 0 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 8 ) & 0xffu );
            film->fluid_h[ 1 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 16 ) & 0xffu );
            film->fluid_h[ 2 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 24 ) & 0xffu );
            for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                film->face[ i ] = water_block->textures[ i ];
                film->packed_lighting[ i ] = i == f ? NO_LIGHT_BRIGHT : NO_LIGHT_NO_DRAW;
            }
            if ( !fluid_flat ) {
                film->face[ FACE_TOP ] = water_block->textures[ FACE_FRONT ];
            }
        }
    };

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
                    const BlockCell cell = cells[ index ];
                    s_fluid_corners[ index ] = FLUID_CORNERS_NOT_FLUID;
                    const BlockID id = block_cell_id( cell );
                    if ( id >= LAST_BLOCK_ID || fluid_defs[ id ].renderOrder != RenderOrder_Water ) {
                        continue;
                    }
                    s_fluid_corners[ index ] = fluid_pack_corners( cells, x, y, z );
                    unsigned int boundary = 0;
                    const unsigned int vis = fluid_face_vis_mask( cells, fluid_defs, x, y, z, id, &boundary );
                    for ( int f = FACE_TOP; f < NUM_FACES_IN_CUBE; f++ ) {
                        workingSpace[ index ].packed_lighting[ f ] = ( vis >> f ) & 1u ? NO_LIGHT_BRIGHT : NO_LIGHT_NO_DRAW;
                    }
                    if ( boundary != 0 ) {
                        emit_boundary_films( x, y, z, boundary, s_fluid_corners[ index ] );
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

                    const BlockState blockState = state_of( index );
                    if ( blockState.id == MOVING_PISTON ) {
                        // The cell itself is invisible — its carried block is
                        // drawn by the sliding body — and its display_id
                        // encodes the carried block for the landing
                        // fallback, not for meshing.
                        continue;
                    }
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
                        if ( block->is_repeater ) {
                            // Every repeater cell emits its own two torch
                            // sticks below — no merged runs.
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
                                can_extend_x = can_extend_rect( cells, extras, blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_x ) {
                                    size_x++;
                                }
                            }
                            if ( block_can_mesh_z ) {
                                glm::ivec3 starting = glm::ivec3( x, y, z + size_z - 1 );
                                glm::ivec3 size = glm::ivec3( size_x, size_y, 1 );
                                glm::ivec3 dir = glm::ivec3( 0, 0, 1 );
                                can_extend_z = can_extend_rect( cells, extras, blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_z ) {
                                    size_z++;
                                }
                            }
                            if ( block_can_mesh_y ) {
                                glm::ivec3 starting = glm::ivec3( x, y + size_y - 1, z );
                                glm::ivec3 size = glm::ivec3( size_x, 1, size_z );
                                glm::ivec3 dir = glm::ivec3( 0, 1, 0 );
                                can_extend_y = can_extend_rect( cells, extras, blockState, packed_lighting, workingSpace, starting, size, dir );
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

                        // Per-cell instance emission — shared with block
                        // bodies (block_mesh_emit.cpp), which draw their
                        // sliding/falling blocks through this exact
                        // instance set + the chunk shader.
                        BlockEmitParams emit;
                        emit.block = block;
                        emit.state = &blockState;
                        emit.origin = glm::vec3( static_cast<float>( chunk_pos.x * CHUNK_SIZE_X + x ),
                                                 static_cast<float>( chunk_pos.y * CHUNK_SIZE_Y + y ),
                                                 static_cast<float>( chunk_pos.z * CHUNK_SIZE_Z + z ) );
                        emit.mesh_x = size_x;
                        emit.mesh_y = size_y;
                        emit.mesh_z = size_z;
                        memcpy( emit.packed_lighting, packed_lighting, sizeof( emit.packed_lighting ) );
                        emit.fluid_corners = s_fluid_corners[ index ];
                        emit.cull_against_neighbors = true;
                        if ( block_emit_needs_neighbors( block ) ) {
                            for ( int dy = -1; dy <= 1; dy++ ) {
                                for ( int dz = -1; dz <= 1; dz++ ) {
                                    for ( int dx = -1; dx <= 1; dx++ ) {
                                        emit.nb.at( dx, dy, dz ) = state_of( get_index_from_coords( x + dx, y + dy, z + dz ) );
                                    }
                                }
                            }
                        }
                        int emit_count2 = 0;
                        const int emit_count = block_emit_count( emit, &emit_count2 );
                        ensure_capacity( block->renderOrder, emit_count );
                        // The second channel is bamboo's leaf crosses in
                        // RenderOrder_Flowers.
                        ensure_capacity( RenderOrder_Flowers, emit_count2 );
                        if ( num_instances[ block->renderOrder ] + emit_count > capacity[ block->renderOrder ] ||
                             num_instances[ RenderOrder_Flowers ] + emit_count2 > capacity[ RenderOrder_Flowers ] ) {
                            continue; // OOM — drop this cell's instances.
                        }
                        ChunkEmitCtx emit_ctx = { emit_buf[ block->renderOrder ], &num_instances[ block->renderOrder ] };
                        ChunkEmitCtx emit_ctx2 = { emit_buf[ RenderOrder_Flowers ], &num_instances[ RenderOrder_Flowers ] };
                        const BlockEmitSink sink = { &emit_ctx, chunk_emit_alloc, &emit_ctx2, chunk_emit_alloc };
                        block_emit_cell( emit, sink );

                    }

                    // A waterlogged block at the top of a water column (no
                    // liquid above it) still needs the water surface drawn on
                    // its cell — emit a water instance for it. This runs on the
                    // CPU for both meshers: the HIP kernel only fills
                    // workingSpace.
                    if ( block->waterloggable && blockState.waterlogged ) {
                        const BlockCell above_cell = cells[ get_index_from_coords( x, y + 1, z ) ];
                        if ( block_cell_id( above_cell ) != WATER && !block_cell_waterlogged( above_cell ) ) {
                            Block *water = block_definition_get_definition( WATER );
                            ensure_capacity( RenderOrder_Water, 1 );
                            if ( num_instances[ RenderOrder_Water ] + 1 > capacity[ RenderOrder_Water ] ) {
                                continue; // OOM
                            }
                            BlockCoords *waterCoord = &emit_buf[ RenderOrder_Water ][ num_instances[ RenderOrder_Water ] ];
                            num_instances[ RenderOrder_Water ] += 1;

                            waterCoord->x = chunk_pos.x * CHUNK_SIZE_X + x;
                            waterCoord->y = chunk_pos.y * CHUNK_SIZE_Y + y;
                            waterCoord->z = chunk_pos.z * CHUNK_SIZE_Z + z;

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
                            const unsigned int corners = fluid_pack_corners( cells, x, y, z );
                            const bool fluid_flat = corners == FLUID_CORNERS_FLAT;
                            waterCoord->fluid_h0 = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( corners & 0xffu );
                            waterCoord->fluid_h[ 0 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 8 ) & 0xffu );
                            waterCoord->fluid_h[ 1 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 16 ) & 0xffu );
                            waterCoord->fluid_h[ 2 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 24 ) & 0xffu );
                            unsigned int boundary = 0;
                            const unsigned int vis = fluid_face_vis_mask( cells, mesh_def_table( ), x, y, z, WATER, &boundary );
                            for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                                waterCoord->face[ i ] = water->textures[ i ];
                                waterCoord->packed_lighting[ i ] = ( vis >> i ) & 1u ? NO_LIGHT_BRIGHT : NO_LIGHT_NO_DRAW;
                            }
                            if ( !fluid_flat ) {
                                waterCoord->face[ FACE_TOP ] = water->textures[ FACE_FRONT ];
                            }
                            if ( boundary != 0 ) {
                                emit_boundary_films( x, y, z, boundary, corners );
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

    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        out.num[ renderOrder ] = num_instances[ renderOrder ];
        // Copy the used instances out of the scratch buffer into a right-sized
        // allocation — the returned arrays must never point into the scratch
        // past this function, since program_terrain/unprogram_terrain free()
        // them after publish.
        if ( num_instances[ renderOrder ] == 0 ) {
            out.blocks[ renderOrder ] = nullptr;
        } else {
            const int n = num_instances[ renderOrder ];
            BlockCoords *copy = static_cast<BlockCoords *>( malloc( n * sizeof( BlockCoords ) ) );
            if ( copy ) {
                memcpy( copy, emit_buf[ renderOrder ], n * sizeof( BlockCoords ) );
            } else {
                pr_debug( "Error, out of memory copying populated_blocks" );
                out.num[ renderOrder ] = 0;
            }
            out.blocks[ renderOrder ] = copy;
        }
    }
}

// Worker-side entry for async server-diff remesh jobs: runs build_mesh on
// each member's cells/extras snapshot (TerrainLoadingThread pops these ahead
// of terrain work). Free function so tests can run a job inline.
void mesh_job_run( MeshJob &job ) {
    for ( MeshJobMember &m : job.members ) {
        Chunk::build_mesh( m.cells, m.extras, m.chunk_pos, m.result );
    }
}

// Frees every buffer a job still owns: the pooled cells/extras snapshots and
// any result arrays not yet moved into a layer. Render thread only (buffers
// are always allocated AND freed there).
void mesh_job_free_buffers( MeshJob &job ) {
    for ( MeshJobMember &m : job.members ) {
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            free( m.result.blocks[ renderOrder ] );
            m.result.blocks[ renderOrder ] = nullptr;
        }
        chunk_free_cells( m.cells );
        chunk_free_extras( m.extras );
        m.cells = nullptr;
        m.extras = nullptr;
    }
    job.members.clear( );
}
