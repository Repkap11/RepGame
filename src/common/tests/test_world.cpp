// World/lighting generation tests — split out of rep_tests.cpp.
#include "test_util.hpp"
// Terrain generator invariants, checked through the public single-cell API
// (MapGen::gen_block_id is the same code the CUDA/HIP kernels share via
// map_gen_fields.hpp). Seed is fixed so every check is deterministic.
int test_mapgen( ) {
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    const float max_h = MapGen::maxTerrainHeight( );
    int lava = 0, ores = 0, overhangs = 0, above_max = 0, bedrock_gaps = 0;
    int grass = 0, sand_like = 0, snow_like = 0;
    for ( int x = -256; x < 256; x += 2 ) {
        for ( int z = -256; z < 256; z += 2 ) {
            const float h = MapGen::calculateTerrainHeight( x, z );
            int top_solid = -10000;
            for ( int y = -110; y <= (int)max_h; y++ ) {
                const BlockID b = MapGen::gen_block_id( x, y, z );
                if ( y <= BEDROCK_LEVEL - 4 && b != BEDROCK ) {
                    bedrock_gaps++;
                }
                if ( b == LAVA ) {
                    lava++;
                }
                if ( b == COAL_ORE || b == IRON_ORE || b == GOLD_ORE ) {
                    ores++;
                }
                if ( b != AIR && b != WATER ) {
                    if ( (float)y > max_h ) {
                        above_max++;
                    }
                    top_solid = y;
                }
            }
            if ( top_solid > (int)ceilf( h ) + 2 &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 1, z ) == AIR &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 2, z ) == AIR ) {
                overhangs++;
            }
            if ( top_solid > -10000 ) {
                const BlockID surf = MapGen::gen_block_id( x, top_solid, z );
                if ( surf == GRASS || surf == PODZOL || surf == MYCELIUM ) grass++;
                if ( surf == SAND || surf == RED_SAND || surf == GRAVEL ) sand_like++;
                if ( surf == SNOW || surf == SNOWY_GRASS ) snow_like++;
            }
        }
    }
    // Mountains are sparse, so the snowline needs a wider (cheap) survey: the
    // heightmap alone finds tall columns, then a short y-scan finds the top
    // block. No per-y scan over the whole area. The same pass verifies
    // overhangs: they intentionally only form in rough/mountain terrain now
    // (flat land stays walkably smooth), so mountain columns are where they
    // must exist.
    for ( int x = -2048; x < 2048; x += 8 ) {
        for ( int z = -2048; z < 2048; z += 8 ) {
            const float h = MapGen::calculateTerrainHeight( x, z );
            if ( h <= MOUNTAIN_ROCK_LINE ) {
                continue;
            }
            int top_solid = -10000;
            for ( int y = (int)h + (int)OVERHANG_MAX_RISE; y >= (int)h - 5; y-- ) {
                const BlockID b = MapGen::gen_block_id( x, y, z );
                if ( b != AIR && b != WATER ) {
                    top_solid = y;
                    if ( b == SNOW || b == SNOWY_GRASS ) {
                        snow_like++;
                    }
                    break;
                }
            }
            if ( top_solid > (int)ceilf( h ) + 2 &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 1, z ) == AIR &&
                 MapGen::gen_block_id( x, (int)ceilf( h ) + 2, z ) == AIR ) {
                overhangs++;
            }
        }
    }

    check_bool( "nothing solid above maxTerrainHeight", above_max == 0 );
    check_bool( "bedrock floor has no gaps", bedrock_gaps == 0 );
#if REPGAME_MAP_GEN_LEGACY
    // The legacy generator has no lava layer or overhangs, and snow only
    // appears on rare tall mountain caps — none are guaranteed in a survey.
    (void)lava;
    (void)overhangs;
    (void)snow_like;
#else
    check_bool( "lava layer exists below LAVA_LEVEL", lava > 0 );
    check_bool( "terrain overhangs exist", overhangs > 0 );
    check_bool( "snowy surfaces exist", snow_like > 0 );
#endif
    check_bool( "ores generate", ores > 0 );
    check_bool( "grassy surfaces exist", grass > 0 );
    check_bool( "sandy surfaces exist", sand_like > 0 );
    return failures;
}

// Flood-fill lighting (src/common/light.cpp): torch emission + decay/removal
// on the block channel, and the column cascade + occlusion on the sky
// channel. Runs without GL — propagation works purely on Chunk::light[].
int test_lighting( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    // Heap, not stack: RepGameState embeds megabytes of light queues (the
    // game keeps it in static storage via globalGameState) — a stack local
    // overflows the default 8MB stack. Leaked on purpose: the process exits
    // right after the tests and destructors would touch GL state.
    RepGameState &gs = *new RepGameState( );
    gs.tick_number = 0;
    gs.camera.pos = glm::dvec3( 0, 0, 0 );
    test_setup_world( gs.world );
    World &world = gs.world;
    ChunkLoader &cl = world.chunkLoader;
    cl.chunk_center = glm::ivec3( 0, 0, 0 );
    Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) ];
    chunk.light = static_cast<unsigned char *>( calloc( CHUNK_BLOCK_SIZE, 1 ) );

    int failures = 0;
    auto check_light = [ & ]( const char *name, const glm::ivec3 &pos, int channel, int expected ) {
        const int v = cl.light_get( pos, channel );
        const bool ok = v == expected;
        pr_test( "  %-55s light=%-2d expected=%-2d  %s", name, v, expected, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    // --- Block channel: torch emits 14, decays 1 per BFS step.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    cl.light_process_queue( 1000000 );
    check_light( "torch cell", glm::ivec3( 10, 10, 10 ), LIGHT_CHANNEL_BLOCK, 14 );
    check_light( "adjacent to torch", glm::ivec3( 11, 10, 10 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "above torch", glm::ivec3( 10, 11, 10 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "diagonal two steps", glm::ivec3( 11, 11, 10 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "past light range", glm::ivec3( 25, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );

    // An opaque wall blocks the direct path; light detours over it.
    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    world.set_loaded_block( glm::ivec3( 12, 10, 10 ), stone );
    cl.light_process_queue( 1000000 );
    check_light( "behind wall detours (5 steps)", glm::ivec3( 13, 10, 10 ), LIGHT_CHANNEL_BLOCK, 9 );

    // Breaking the torch removes its light entirely.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_light( "torch cell after break", glm::ivec3( 10, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );
    check_light( "adjacent after break", glm::ivec3( 11, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );
    check_light( "detoured cell after break", glm::ivec3( 13, 10, 10 ), LIGHT_CHANNEL_BLOCK, 0 );

    // Block stacked directly on a torch: the air cells beside the block
    // (what its +/-z faces sample) must stay lit around the obstruction —
    // two steps from the torch via the cells beside it.
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), stone );
    cl.light_process_queue( 1000000 );
    check_light( "beside block +z over torch", glm::ivec3( 10, 11, 11 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "beside block -z over torch", glm::ivec3( 10, 11, 9 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "beside block +x over torch", glm::ivec3( 11, 11, 10 ), LIGHT_CHANNEL_BLOCK, 12 );
    check_light( "above block over torch", glm::ivec3( 10, 12, 10 ), LIGHT_CHANNEL_BLOCK, 10 );
    world.set_loaded_block( glm::ivec3( 10, 11, 10 ), BLOCK_STATE_AIR );
    world.set_loaded_block( glm::ivec3( 10, 10, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Leaves are light-permeable but absorbing: entering one costs 1 +
    // light_absorb (7 at absorb 6), so torchlight inside a leaf dims hard,
    // and the far side settles at the best wraparound path (glass costs -1).
    world.set_loaded_block( glm::ivec3( 10, 20, 10 ), { TORCH, 0, 0, TORCH, 0 } );
    world.set_loaded_block( glm::ivec3( 12, 20, 10 ), { OAK_LEAVES, 0, 0, OAK_LEAVES, 0 } );
    cl.light_process_queue( 1000000 );
    check_light( "air cell beside torch", glm::ivec3( 11, 20, 10 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "leaf cell holds light", glm::ivec3( 12, 20, 10 ), LIGHT_CHANNEL_BLOCK, 6 );
    check_light( "past leaf is dimmer", glm::ivec3( 13, 20, 10 ), LIGHT_CHANNEL_BLOCK, 9 );
    world.set_loaded_block( glm::ivec3( 12, 20, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_light( "past cleared leaf recovers", glm::ivec3( 13, 20, 10 ), LIGHT_CHANNEL_BLOCK, 11 );
    world.set_loaded_block( glm::ivec3( 10, 20, 10 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Regression: an overhang present BEFORE finalize must still get lateral
    // skylight — the interior band scan seeds lit cells facing deeper-dark
    // neighbor columns. Writing the block directly (not set_loaded_block)
    // keeps it out of the recheck path, which would seed around it and mask
    // the scan; light_finalize_chunk is the path terrain features take.
    chunk.store_state( chunk.get_index_from_coords( 5, 30, 5 ), stone );
    cl.light_ensure_columns( chunk );
    cl.light_compute_one_column( chunk, 5, 5 );

    // --- Sky channel: finalize runs the per-column cascade. All-air chunk
    // opens every column, so skylight reaches the bottom at full strength.
    cl.light_finalize_chunk( chunk );
    cl.light_process_queue( 1000000 );
    check_light( "open column top", glm::ivec3( 20, 31, 20 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "open column bottom", glm::ivec3( 20, 0, 20 ), LIGHT_CHANNEL_SKY, 15 );

    // A blocker near the top of a column closes it: the column loses its
    // vertical sky feed and re-settles at side-lit 14, while the neighbor
    // column keeps its full 15. (y=30, not 31: the top interior cell needs
    // the chunk above for halo fixup, which this test world lacks.)
    world.set_loaded_block( glm::ivec3( 20, 30, 20 ), stone );
    cl.light_process_queue( 1000000 );
    check_light( "under sky blocker", glm::ivec3( 20, 29, 20 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "deep under sky blocker", glm::ivec3( 20, 0, 20 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "open neighbor column", glm::ivec3( 21, 30, 20 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under finalize-time canopy", glm::ivec3( 5, 29, 5 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "deep under finalize-time canopy", glm::ivec3( 5, 0, 5 ), LIGHT_CHANNEL_SKY, 14 );

    // Finalize-path light floor: a 9x9 slab floor written via store_state
    // (terrain-style, no recheck) caps its columns — the interior boundary
    // scan must re-seed above each cap or the slab cells stay dim.
    const BlockState slab = { BRICK_SLAB, 0, 0, BRICK_SLAB, 0 };
    cl.light_ensure_columns( chunk );
    for ( int fx = 22; fx <= 30; fx++ ) {
        for ( int fz = 22; fz <= 30; fz++ ) {
            chunk.store_state( chunk.get_index_from_coords( fx, 25, fz ), slab );
            cl.light_compute_one_column( chunk, fx, fz );
        }
    }

    // Regression: the worker's optimistic prefill sets sky_open_above=1
    // before finalize, so the cascade fills nothing (cells_filled==0) — the
    // interior boundary scan must still seed canopy-shaded cells or they
    // stay dark until an unrelated edit. light_fill_chunk is the path the
    // terrain worker takes (the canopy stone is still in cells[]).
    light_fill_chunk( chunk );
    cl.light_finalize_chunk( chunk );
    cl.light_process_queue( 1000000 );
    check_light( "under canopy after optimistic refill", glm::ivec3( 5, 29, 5 ), LIGHT_CHANNEL_SKY, 14 );
    check_light( "deep under canopy after optimistic refill", glm::ivec3( 5, 0, 5 ), LIGHT_CHANNEL_SKY, 14 );

    // --- Light floors (blocks_light_down): bottom-aligned partial blocks
    // hold light but never pass it downward and cap the sky column, so a
    // slab roof shades while its sides and top stay lit.

    // Finalize-path floor (the 9x9 above): the interior slab cell reaches
    // full sky via the cap reseed; the cell under the floor center is
    // shaded to 15 - (distance to the nearest open column) = 15 - 5 = 10.
    check_light( "finalize slab floor interior sky", glm::ivec3( 26, 25, 26 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under finalize slab floor", glm::ivec3( 26, 24, 26 ), LIGHT_CHANNEL_SKY, 10 );

    // Shade mask (contact shadows): light_fill_chunk stamps bit=1 only for
    // blocks_light_down cells — set on the slab floor, clear on opaque
    // stone and air. (Mask exists only under light_mipmaps_enabled; tests
    // run with it on.)
    auto shade_bit = [ & ]( const glm::ivec3 &p ) -> int {
        if ( !chunk.light_shade_mask ) {
            return -1;
        }
        const int i = chunk.get_index_from_coords( p );
        return ( chunk.light_shade_mask[ i >> 3 ] >> ( i & 7 ) ) & 1;
    };
    check_bool( "shade mask built by fill", chunk.light_shade_mask != nullptr );
    check_bool( "shade bit set on slab floor cell", shade_bit( glm::ivec3( 26, 25, 26 ) ) == 1 );
    check_bool( "shade bit clear on stone", shade_bit( glm::ivec3( 5, 30, 5 ) ) == 0 );
    check_bool( "shade bit clear on air", shade_bit( glm::ivec3( 28, 28, 28 ) ) == 0 );
    for ( int fx = 22; fx <= 30; fx++ ) {
        for ( int fz = 22; fz <= 30; fz++ ) {
            chunk.store_state( chunk.get_index_from_coords( fx, 25, fz ), BLOCK_STATE_AIR );
            cl.light_compute_one_column( chunk, fx, fz );
        }
    }
    cl.light_process_queue( 1000000 );

    // Edit path: a slab placed into an open-sky column caps the column fill
    // but still holds full sky itself; the cell below is lit laterally.
    // Placing into the lit column also proves the remove pass clears the 15
    // that used to live under it.
    world.set_loaded_block( glm::ivec3( 16, 25, 16 ), slab );
    cl.light_process_queue( 1000000 );
    check_light( "placed slab cell holds sky", glm::ivec3( 16, 25, 16 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "above placed slab stays sky", glm::ivec3( 16, 26, 16 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under placed slab is lateral-lit", glm::ivec3( 16, 24, 16 ), LIGHT_CHANNEL_SKY, 14 );
    check_bool( "shade bit set on placed slab", shade_bit( glm::ivec3( 16, 25, 16 ) ) == 1 );
    world.set_loaded_block( glm::ivec3( 16, 25, 16 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_light( "under cleared slab is sky again", glm::ivec3( 16, 24, 16 ), LIGHT_CHANNEL_SKY, 15 );
    check_bool( "shade bit cleared on slab break", shade_bit( glm::ivec3( 16, 25, 16 ) ) == 0 );

    // Stale-alpha guard: an edit that flips only the shade bit — slab onto
    // open-sky stone, every light value stays the same — must still mark
    // its texel in the dirty box or the GPU keeps a stale A: no contact
    // shadow appears on place, and a ghost one stays after a break.
    world.set_loaded_block( glm::ivec3( 8, 24, 28 ), stone );
    cl.light_process_queue( 1000000 );
    chunk.light_dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    world.set_loaded_block( glm::ivec3( 8, 25, 28 ), slab );
    cl.light_process_queue( 1000000 );
    {
        const uint64_t box = chunk.light_dirty_box.load( std::memory_order_relaxed );
        const glm::ivec3 texel( 8 + 1, 25 + 1, 28 + 1 ); // block local + halo
        check_bool( "slab-on-stone edit marks its texel",
                    box != LIGHT_BOX_EMPTY && static_cast<int>( box & 0xFF ) <= texel.x &&
                        texel.x <= static_cast<int>( ( box >> 24 ) & 0xFF ) &&
                        static_cast<int>( ( box >> 8 ) & 0xFF ) <= texel.y &&
                        texel.y <= static_cast<int>( ( box >> 32 ) & 0xFF ) &&
                        static_cast<int>( ( box >> 16 ) & 0xFF ) <= texel.z &&
                        texel.z <= static_cast<int>( ( box >> 40 ) & 0xFF ) );
    }
    chunk.light_dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    world.set_loaded_block( glm::ivec3( 8, 25, 28 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );
    check_bool( "slab break also marks its texel", chunk.light_dirty_box.load( std::memory_order_relaxed ) != LIGHT_BOX_EMPTY );
    world.set_loaded_block( glm::ivec3( 8, 24, 28 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Wide 7x7 roof: the center has no lateral path shorter than 4 steps
    // to an open column, so the cell below it settles at 15 - 4 = 11.
    for ( int rx = 24; rx <= 30; rx++ ) {
        for ( int rz = 10; rz <= 16; rz++ ) {
            world.set_loaded_block( glm::ivec3( rx, 25, rz ), slab );
        }
    }
    cl.light_process_queue( 1000000 );
    check_light( "roof center slab cell sky", glm::ivec3( 27, 25, 13 ), LIGHT_CHANNEL_SKY, 15 );
    check_light( "under roof center", glm::ivec3( 27, 24, 13 ), LIGHT_CHANNEL_SKY, 11 );
    for ( int rx = 24; rx <= 30; rx++ ) {
        for ( int rz = 10; rz <= 16; rz++ ) {
            world.set_loaded_block( glm::ivec3( rx, 25, rz ), BLOCK_STATE_AIR );
        }
    }
    cl.light_process_queue( 1000000 );

    // Block channel: a torch directly above a slab lights the slab cell to
    // 13, but the cell under the slab can only be reached around the side —
    // torch 14 -> side 13 -> down 12 -> under-side 11 -> under-slab 10.
    world.set_loaded_block( glm::ivec3( 16, 22, 16 ), { TORCH, 0, 0, TORCH, 0 } );
    world.set_loaded_block( glm::ivec3( 16, 21, 16 ), slab );
    cl.light_process_queue( 1000000 );
    check_light( "slab cell over-detoured torch", glm::ivec3( 16, 21, 16 ), LIGHT_CHANNEL_BLOCK, 13 );
    check_light( "under slab only gets side path", glm::ivec3( 16, 20, 16 ), LIGHT_CHANNEL_BLOCK, 10 );
    world.set_loaded_block( glm::ivec3( 16, 21, 16 ), BLOCK_STATE_AIR );
    world.set_loaded_block( glm::ivec3( 16, 22, 16 ), BLOCK_STATE_AIR );
    cl.light_process_queue( 1000000 );

    // Derived flag sanity: bottom-aligned partials are light floors; full
    // blocks, absorbing leaves, and plates are not.
    check_bool( "BRICK_SLAB is a light floor", block_definition_get_definition( BRICK_SLAB )->blocks_light_down && !block_definition_get_definition( BRICK_SLAB )->casts_shadow );
    check_bool( "GRASS_PATH is a light floor", block_definition_get_definition( GRASS_PATH )->blocks_light_down && !block_definition_get_definition( GRASS_PATH )->casts_shadow );
    check_bool( "STONE is not a light floor", !block_definition_get_definition( STONE )->blocks_light_down );
    check_bool( "OAK_LEAVES is not a light floor", !block_definition_get_definition( OAK_LEAVES )->blocks_light_down );
    check_bool( "pressure plate is not a light floor", !block_definition_get_definition( STONE_PRESSURE_PLATE )->blocks_light_down );
    return failures;
}

// Structure-gen border mirroring: a chunk's +-1 halo cells must equal the
// interior cells they mirror in the neighbor. Terrain trivially agrees (both
// sides derive it from the same pure function); tree parts only agree when
// the placement rules are chunk-independent — on the old legacy generator a
// border-spanning tree was clipped to the trunk's owning chunk and the halo
// stayed bare. Two y slabs are generated so y-border mirroring is checked
// the same way.
int test_structure_border( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };

    auto gen_chunk = []( const glm::ivec3 &pos ) {
        Chunk *c = new Chunk( ); // freed by the caller's loop
        c->chunk_pos = pos;
        c->cells = chunk_alloc_cells( );
        c->extras = nullptr;
        c->light_air_mask = nullptr;
        c->light_shade_mask = nullptr;
        MapGen::load_block_c( c );
        StructureGen::place( *c );
        return c;
    };
    auto free_chunk = []( Chunk *c ) {
        chunk_free_cells( c->cells );
        chunk_free_extras( c->extras );
        chunk_free_air_mask( c->light_air_mask );
        chunk_free_air_mask( c->light_shade_mask );
        delete c;
    };

    // A tree part in a halo plane can only have come from structure gen —
    // terrain never emits OAK_LEAVES/OAK_LOG — so counting them proves trees reach
    // borders, and the face comparisons then prove they mirror correctly.
    int border_tree_cells = 0;
    int mismatches = 0;
    for ( int attempt = 0; attempt < 10 && border_tree_cells == 0; attempt++ ) {
        const int rcx = -40 + attempt * 19;
        const int rcz = -40 + attempt * 13;
        const int wx0 = rcx * CHUNK_SIZE_X + CHUNK_SIZE_X / 2;
        const int wz0 = rcz * CHUNK_SIZE_Z + CHUNK_SIZE_Z / 2;
        const float h = MapGen::calculateTerrainHeight( wx0, wz0 );
#if REPGAME_MAP_GEN_LEGACY
        // Target the legacy forest field (perlin_noise2d > 0.7 somewhere in
        // the region) so the grid is actually in tree country.
        bool forest = false;
        for ( int x = wx0 - CHUNK_SIZE_X; x < wx0 + 2 * CHUNK_SIZE_X && !forest; x += 5 ) {
            for ( int z = wz0 - CHUNK_SIZE_Z; z < wz0 + 2 * CHUNK_SIZE_Z && !forest; z += 5 ) {
                if ( perlin_noise2d( x, z, 0.02f, 3, MAP_SEED + 6 ) > 0.7f ) {
                    forest = true;
                }
            }
        }
        if ( !forest ) {
            continue;
        }
#else
        if ( h < WATER_LEVEL + 3.0f ) {
            continue; // open water — trees need land
        }
#endif
        const int cy = (int)floorf( h / (float)CHUNK_SIZE_Y );

        Chunk *grid[ 3 ][ 2 ][ 3 ] = {};
        for ( int gx = 0; gx < 3; gx++ ) {
            for ( int gy = 0; gy < 2; gy++ ) {
                for ( int gz = 0; gz < 3; gz++ ) {
                    grid[ gx ][ gy ][ gz ] = gen_chunk( glm::ivec3( rcx + gx, cy + gy, rcz + gz ) );
                }
            }
        }

        // Every shared face, both directions: the outer halo cell of each
        // chunk vs the mirrored interior edge of its neighbor.
        for ( int gx = 0; gx < 3; gx++ ) {
            for ( int gy = 0; gy < 2; gy++ ) {
                for ( int gz = 0; gz < 3; gz++ ) {
                    const Chunk *a = grid[ gx ][ gy ][ gz ];
                    if ( gx < 2 ) {
                        const Chunk *b = grid[ gx + 1 ][ gy ][ gz ];
                        for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                            for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                                if ( a->get_block( glm::ivec3( CHUNK_SIZE_X, ly, lz ) ).id != b->get_block( glm::ivec3( 0, ly, lz ) ).id ) {
                                    mismatches++;
                                }
                                if ( a->get_block( glm::ivec3( CHUNK_SIZE_X - 1, ly, lz ) ).id != b->get_block( glm::ivec3( -1, ly, lz ) ).id ) {
                                    mismatches++;
                                }
                            }
                        }
                    }
                    if ( gz < 2 ) {
                        const Chunk *b = grid[ gx ][ gy ][ gz + 1 ];
                        for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                            for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                                if ( a->get_block( glm::ivec3( lx, ly, CHUNK_SIZE_Z ) ).id != b->get_block( glm::ivec3( lx, ly, 0 ) ).id ) {
                                    mismatches++;
                                }
                                if ( a->get_block( glm::ivec3( lx, ly, CHUNK_SIZE_Z - 1 ) ).id != b->get_block( glm::ivec3( lx, ly, -1 ) ).id ) {
                                    mismatches++;
                                }
                            }
                        }
                    }
                    if ( gy < 1 ) {
                        const Chunk *b = grid[ gx ][ gy + 1 ][ gz ];
                        for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                            for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                                if ( a->get_block( glm::ivec3( lx, CHUNK_SIZE_Y, lz ) ).id != b->get_block( glm::ivec3( lx, 0, lz ) ).id ) {
                                    mismatches++;
                                }
                                if ( a->get_block( glm::ivec3( lx, CHUNK_SIZE_Y - 1, lz ) ).id != b->get_block( glm::ivec3( lx, -1, lz ) ).id ) {
                                    mismatches++;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Count tree parts sitting on x/z halo planes.
        for ( int gx = 0; gx < 3; gx++ ) {
            for ( int gy = 0; gy < 2; gy++ ) {
                for ( int gz = 0; gz < 3; gz++ ) {
                    const Chunk *c = grid[ gx ][ gy ][ gz ];
                    for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                            for ( int lx : { -1, CHUNK_SIZE_X } ) {
                                const BlockID id = c->get_block( glm::ivec3( lx, ly, lz ) ).id;
                                if ( id == OAK_LEAVES || id == OAK_LOG || id == GLOWSTONE ) {
                                    border_tree_cells++;
                                }
                            }
                            for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                                for ( int lz2 : { -1, CHUNK_SIZE_Z } ) {
                                    const BlockID id = c->get_block( glm::ivec3( lx, ly, lz2 ) ).id;
                                    if ( id == OAK_LEAVES || id == OAK_LOG || id == GLOWSTONE ) {
                                        border_tree_cells++;
                                    }
                                }
                            }
                        }
                    }
                    free_chunk( grid[ gx ][ gy ][ gz ] );
                }
            }
        }
    }
    check_bool( "trees reach chunk borders", border_tree_cells > 0 );
    check_bool( "halo cells mirror neighbor interiors", mismatches == 0 );
    return failures;
}

