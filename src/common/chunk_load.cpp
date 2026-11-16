// Worker-side chunk loading: terrain generation dispatch, saved-edit
// overlay, sidecar records (chests/furnaces/signs/mobs/items/vehicles),
// the deterministic natural-spawn roll, and the offline inspect_load
// helper. Runs on the terrain workers (or inline on WASM); split out of
// chunk.cpp so the loader pipeline is separate from slot lifecycle.
#include <string.h>
#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include <vector>

#include "common/chunk.hpp"
#include "common/chunk_render.hpp"
#include "common/constants.hpp"
#include "common/dimension_profile.hpp"
#include "common/light.hpp"
#include "common/map_gen.hpp"
#include "common/mob_types.hpp"
#include "common/net/packet.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/map_storage.hpp"
#include "common/structure_gen.hpp"

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

// Log-once flag for the mapgen backend pick; terrain workers race on it.
static std::atomic<int> firstTime{ 1 };

#if TERRAIN_GEN_PROFILING
#include <chrono>
#include <atomic>
static std::atomic<int> profiling_chunk_count{ 0 };
static std::atomic<long long> profiling_total_us{ 0 };
static std::atomic<long long> profiling_load_disk_us{ 0 };
static std::atomic<long long> profiling_map_gen_us{ 0 };
static std::atomic<long long> profiling_structure_gen_us{ 0 };
static std::atomic<long long> profiling_populated_blocks_us{ 0 };
// Mesh-phase counters live in chunk_mesh.cpp (the writer).
extern std::atomic<long long> profiling_mesh_vis_light_us;
extern std::atomic<long long> profiling_mesh_greedy_us;
extern std::atomic<long long> profiling_instance_count;
static std::atomic<int> profiling_map_gen_count{ 0 };
static std::atomic<int> profiling_disk_load_count{ 0 };
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
    const DimensionId dim = dimension_of_chunk( this->chunk_pos );
    if ( !dimension_profile( dim )->natural_spawns ) {
        return false;
    }
    bool spawned = false;
    // Roll index of the next emitted record; skipped indices still consume
    // one so tombstones match the deterministic ordering.
    int roll_index = 0;
    const glm::ivec3 origin = this->chunk_pos * CHUNK_SIZE_I;
    for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
        const MobTypeDef &def = mob_types[ t ];
        if ( def.spawn_chance <= 0.0f || !( def.spawn_dims & ( 1u << dim ) ) ) {
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
                if ( top < 0 || top + headroom >= CHUNK_SIZE_Y ) {
                    continue;
                }
                const BlockID floor_id = this->id_at( Chunk::get_index_from_coords( x, top, z ) );
                bool floor_ok = false;
                for ( int si = 0; si < 4 && def.spawn_on[ si ] != LAST_BLOCK_ID; si++ ) {
                    if ( def.spawn_on[ si ] == floor_id ) {
                        floor_ok = true;
                        break;
                    }
                }
                if ( !floor_ok ) {
                    continue;
                }
                bool fits = true;
                for ( int y = top + 1; y <= top + headroom; y++ ) {
                    const BlockID id = this->id_at( Chunk::get_index_from_coords( x, y, z ) );
                    // Liquids veto too: non-colliding water/lava passes the
                    // collides check, so without it herds spawn submerged
                    // (under ocean water or on the nether lava-sea floor).
                    if ( id == WATER || id == LAVA || ( id != AIR && block_definition_get_definition( id )->collides_with_player ) ) {
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
    // pending_listed / upload_listed stay whatever they were — see
    // release_terrain. Clearing them here while the slot's entry still sits
    // in light_pending_list / a dirty list lets the next enqueue push a
    // duplicate; the drain loops own those flags.
    this->light.write_tick.store( 0, std::memory_order_relaxed );
    this->light.upload_defers = 0;
    this->light.uploaded_once = 0;
    // The light texture survives a slot reuse (only release_terrain frees
    // it, and parked re-entry can beat the deferred release) while this
    // load just replaced the field it mirrors — flag the GPU copy stale so
    // draw falls back to the sky volume until a full upload lands. Can't
    // glDeleteTextures here: this runs on a terrain worker.
    this->light.tex_stale.store( 1, std::memory_order_relaxed );
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
                        this->cells[ index ] = flat_world ? static_cast<BlockCell>( MapGen::flat_world_id( x, y, z ) ) : MapGen::block_world_cell( x, y, z );
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
            if ( firstTime.exchange( 0 ) ) {
                pr_debug( "Using CUDA" );
            }
            MapGen::load_block_cuda( this );
        } else if ( LOAD_CHUNKS_SUPPORTS_HIP && MapGen::supports_hip( ) && !no_hip_mapgen ) {
            if ( firstTime.exchange( 0 ) ) {
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
            static std::atomic<int> firstWebGPU{ 1 };
            if ( firstWebGPU.exchange( 0 ) ) {
                pr_debug( "Using WebGPU" );
            }
            if ( !MapGen::load_block_webgpu( this ) ) {
                // WebGPU failed (init failure, device loss, readback error);
                // fall back to the CPU path for this chunk.
                MapGen::load_block_c( this );
            }
        } else {
            if ( firstTime.exchange( 0 ) ) {
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
                    if ( block_is_sign( old_state.id ) && !block_is_sign( run.state.id ) ) {
                        this->containers.signs.erase( i );
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
    // Sign text: same sidecar pattern, loaded after the block edits above
    // so records over cells that are no longer signs drop here.
    this->containers.signs.clear( );
    SignTextMap sign_world_map;
    this->containers.signs_loaded = map_storage.load_signs( this->chunk_pos, sign_world_map ) ? 1 : 0;
    for ( const auto &pair : sign_world_map ) {
        const glm::ivec3 diff = pair.first - this->chunk_pos * CHUNK_SIZE_I;
        // Entries must fall inside this chunk (file corruption guard).
        if ( diff.x < 0 || diff.x >= CHUNK_SIZE_X || diff.y < 0 || diff.y >= CHUNK_SIZE_Y || diff.z < 0 || diff.z >= CHUNK_SIZE_Z ) {
            continue;
        }
        const int index = Chunk::get_index_from_coords( diff );
        if ( this->cells == nullptr || !block_is_sign( this->id_at( index ) ) ) {
            continue;
        }
        if ( pair.second.empty( ) ) {
            continue;
        }
        this->containers.signs[ index ] = pair.second;
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
    this->sidecars.vehicles.clear( );
    this->sidecars.vehicles_file_present = 0;
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
        // Vehicles follow the same test-mode rules.
        this->sidecars.vehicles_file_present = map_storage.load_vehicles( this->chunk_pos, this->sidecars.vehicles ) ? 1 : 0;
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
    // The mesh above was built from the final cells — bump the content
    // generation last so an async remesh job snapshotted just before this
    // load still lands (its version matches unless a later write intervened).
    this->cell_version.fetch_add( 1u, std::memory_order_relaxed );
}
