// Nether fortress tests — grid decode, chunk-border mirroring, bounds, and
// the generated-chest loot path. The layout is a pure function of world
// coords, so independently generated neighbors must agree on every shared
// face exactly like test_structure_border does for trees.
#include "test_util.hpp"

// World ids stamped by the fortress passes (SHROOMLIGHT stays out — nether
// terrain/trees place it too, so it can't mark structure cells).
static int tf_is_fortress_id( const BlockID id ) {
    switch ( id ) {
        case NETHER_BRICK:
        case NETHER_BRICK_FENCE:
        case CHISELED_NETHER_BRICKS:
        case CRACKED_NETHER_BRICKS:
        case CHEST:
        case MOB_SPAWNER:
            return 1;
        default:
            return 0;
    }
}

static int tf_floor_div62( const int v ) {
    int q = v / 62;
    if ( v % 62 != 0 && v < 0 ) {
        q--;
    }
    return q;
}

static int tf_floor_div384( const int v ) {
    int q = v / 384;
    if ( v % 384 != 0 && v < 0 ) {
        q--;
    }
    return q;
}

int test_fortress( ) {
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
        c->light.air_mask = nullptr;
        c->light.shade_mask = nullptr;
        MapGen::load_block_c( c );
        StructureGen::place( *c );
        return c;
    };
    auto free_chunk = []( Chunk *c ) {
        chunk_free_cells( c->cells );
        chunk_free_extras( c->extras );
        chunk_free_air_mask( c->light.air_mask );
        chunk_free_air_mask( c->light.shade_mask );
        delete c;
    };

    // Find a fortress cell; ~9-in-10 hit rate makes the first few cells
    // enough, but scan a window so a salt tweak can't silently turn this
    // into a never-found skip.
    glm::ivec3 anchor( 0 );
    int deck = 0;
    bool found = false;
    for ( int cx = -12; cx <= 12 && !found; cx++ ) {
        for ( int cz = -12; cz <= 12 && !found; cz++ ) {
            found = StructureGen::fortress_cell_exists( cx, cz, &anchor, &deck ) != 0;
        }
    }
    check_bool( "a fortress cell exists near the origin", found );
    if ( !found ) {
        return failures;
    }

    // Nether band sanity: the deck must sit between the lava sea (local 60)
    // and the roof mass (~local 200+).
    check_bool( "deck is inside the nether cavern band",
                deck >= DIM_BY_MIN_N + 66 && deck <= DIM_BY_MIN_N + 135 );

    const int cxa = tf_floor_div62( anchor.x );
    const int cya = tf_floor_div62( deck );
    const int cza = tf_floor_div62( anchor.z );

    // 3x3x3 grid centred on the anchor chunk — covers the keep and a good
    // stretch of every arm, so structure cells cross several chunk borders.
    Chunk *grid[ 3 ][ 3 ][ 3 ] = {};
    for ( int gx = 0; gx < 3; gx++ ) {
        for ( int gy = 0; gy < 3; gy++ ) {
            for ( int gz = 0; gz < 3; gz++ ) {
                grid[ gx ][ gy ][ gz ] = gen_chunk( glm::ivec3( cxa - 1 + gx, cya - 1 + gy, cza - 1 + gz ) );
            }
        }
    }

    // Keep floor at the anchor is brick; the cell above the deck at the keep
    // centre is carved interior air.
    {
        const glm::ivec3 local = glm::ivec3( anchor.x, deck, anchor.z ) - grid[ 1 ][ 1 ][ 1 ]->chunk_pos * CHUNK_SIZE_I;
        check_bool( "keep floor under anchor is nether brick",
                    grid[ 1 ][ 1 ][ 1 ]->get_block( local ).id == NETHER_BRICK ||
                        grid[ 1 ][ 1 ][ 1 ]->get_block( local ).id == CRACKED_NETHER_BRICKS ||
                        grid[ 1 ][ 1 ][ 1 ]->get_block( local ).id == CHISELED_NETHER_BRICKS );
        check_bool( "keep interior at anchor is carved air", grid[ 1 ][ 1 ][ 1 ]->get_block( local + glm::ivec3( 0, 3, 0 ) ).id == AIR );
    }

    // Determinism: an independently generated copy of the anchor chunk must
    // be cell-identical — this is what makes client/server terrain agree.
    {
        Chunk *copy = gen_chunk( grid[ 1 ][ 1 ][ 1 ]->chunk_pos );
        int diffs = 0;
        for ( int i = 0; i < CHUNK_BLOCK_SIZE && diffs == 0; i++ ) {
            if ( !BlockStates_equal( grid[ 1 ][ 1 ][ 1 ]->state_at( i ), copy->state_at( i ) ) ) {
                diffs++;
            }
        }
        check_bool( "re-generated chunk is identical", diffs == 0 );
        free_chunk( copy );
    }

    // Halo mirroring: every shared face inside the grid, both directions —
    // a chunk's +-1 halo cell must equal the interior cell it mirrors.
    int mismatches = 0, shown = 0;
    auto diff_cell = [ & ]( const Chunk *a, const Chunk *b, const glm::ivec3 &pa, const glm::ivec3 &pb ) {
        const BlockID ia = a->get_block( pa ).id, ib = b->get_block( pb ).id;
        if ( ia == ib ) {
            return;
        }
        mismatches++;
        if ( shown++ < 24 ) {
            const glm::ivec3 wa = a->chunk_pos * CHUNK_SIZE_I + pa;
            pr_test( "    mismatch world(%d %d %d): id %d vs %d  a_chunk(%d %d %d) b_chunk(%d %d %d)",
                     wa.x, wa.y, wa.z, (int)ia, (int)ib,
                     a->chunk_pos.x, a->chunk_pos.y, a->chunk_pos.z,
                     b->chunk_pos.x, b->chunk_pos.y, b->chunk_pos.z );
        }
    };
    for ( int gx = 0; gx < 3; gx++ ) {
        for ( int gy = 0; gy < 3; gy++ ) {
            for ( int gz = 0; gz < 3; gz++ ) {
                const Chunk *a = grid[ gx ][ gy ][ gz ];
                if ( gx < 2 ) {
                    const Chunk *b = grid[ gx + 1 ][ gy ][ gz ];
                    for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                            diff_cell( a, b, glm::ivec3( CHUNK_SIZE_X, ly, lz ), glm::ivec3( 0, ly, lz ) );
                            diff_cell( a, b, glm::ivec3( CHUNK_SIZE_X - 1, ly, lz ), glm::ivec3( -1, ly, lz ) );
                        }
                    }
                }
                if ( gz < 2 ) {
                    const Chunk *b = grid[ gx ][ gy ][ gz + 1 ];
                    for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                        for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                            diff_cell( a, b, glm::ivec3( lx, ly, CHUNK_SIZE_Z ), glm::ivec3( lx, ly, 0 ) );
                            diff_cell( a, b, glm::ivec3( lx, ly, CHUNK_SIZE_Z - 1 ), glm::ivec3( lx, ly, -1 ) );
                        }
                    }
                }
                if ( gy < 2 ) {
                    const Chunk *b = grid[ gx ][ gy + 1 ][ gz ];
                    for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                            diff_cell( a, b, glm::ivec3( lx, CHUNK_SIZE_Y, lz ), glm::ivec3( lx, 0, lz ) );
                            diff_cell( a, b, glm::ivec3( lx, CHUNK_SIZE_Y - 1, lz ), glm::ivec3( lx, -1, lz ) );
                        }
                    }
                }
            }
        }
    }
    check_bool( "halo cells mirror neighbor interiors", mismatches == 0 );

    // Bounds + bedrock: complexes spill past cell edges and overlap, so a
    // stamped cell only has to sit inside SOME neighbouring anchor's
    // footprint — collect every anchor whose reach covers the grid.
    // (gen_block_id is the pure terrain view — a BEDROCK there must still
    // be bedrock in the chunk.)
    struct TFAnchor {
        int ax, az, deck;
    };
    TFAnchor tfa[ 48 ];
    int ntfa = 0;
    {
        const glm::ivec3 lo = grid[ 0 ][ 0 ][ 0 ]->chunk_pos * CHUNK_SIZE_I;
        const glm::ivec3 hi = grid[ 2 ][ 2 ][ 2 ]->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I;
        for ( int cx = tf_floor_div384( lo.x - 800 ); cx <= tf_floor_div384( hi.x + 800 ) && ntfa < 48; cx++ ) {
            for ( int cz = tf_floor_div384( lo.z - 800 ); cz <= tf_floor_div384( hi.z + 800 ) && ntfa < 48; cz++ ) {
                glm::ivec3 an;
                int dk;
                if ( StructureGen::fortress_cell_exists( cx, cz, &an, &dk ) ) {
                    tfa[ ntfa++ ] = { an.x, an.z, dk };
                }
            }
        }
    }
    int fortress_cells = 0, border_cells = 0, out_of_bounds = 0, bedrock_eaten = 0;
    for ( int gx = 0; gx < 3; gx++ ) {
        for ( int gy = 0; gy < 3; gy++ ) {
            for ( int gz = 0; gz < 3; gz++ ) {
                const Chunk *c = grid[ gx ][ gy ][ gz ];
                if ( c->cells == nullptr ) {
                    continue;
                }
                const glm::ivec3 origin = c->chunk_pos * CHUNK_SIZE_I;
                for ( int lx = -1; lx <= CHUNK_SIZE_X; lx++ ) {
                    for ( int ly = -1; ly <= CHUNK_SIZE_Y; ly++ ) {
                        for ( int lz = -1; lz <= CHUNK_SIZE_Z; lz++ ) {
                            const BlockID id = c->get_block( glm::ivec3( lx, ly, lz ) ).id;
                            if ( !tf_is_fortress_id( id ) ) {
                                continue;
                            }
                            const glm::ivec3 w = origin + glm::ivec3( lx, ly, lz );
                            fortress_cells++;
                            if ( lx == -1 || lx == CHUNK_SIZE_X || ly == -1 || ly == CHUNK_SIZE_Y || lz == -1 || lz == CHUNK_SIZE_Z ) {
                                border_cells++;
                            }
                            bool in_bounds = false;
                            for ( int t = 0; t < ntfa && !in_bounds; t++ ) {
                                in_bounds = abs( w.x - tfa[ t ].ax ) <= 361 && abs( w.z - tfa[ t ].az ) <= 361 &&
                                            w.y >= tfa[ t ].deck - 165 && w.y <= tfa[ t ].deck + 49;
                            }
                            if ( !in_bounds ) {
                                out_of_bounds++;
                            }
                            if ( MapGen::gen_block_id( w.x, w.y, w.z ) == BEDROCK ) {
                                bedrock_eaten++;
                            }
                        }
                    }
                }
            }
        }
    }
    check_bool( "fortress cells exist in the anchor grid", fortress_cells > 0 );
    check_bool( "fortress reaches chunk borders", border_cells > 0 );
    check_bool( "fortress cells stay inside the footprint bound", out_of_bounds == 0 );
    check_bool( "fortress never overwrites bedrock", bedrock_eaten == 0 );

    // Chest blocks + generated loot. place_fortress_loot fills every layout
    // chest slot that landed in the chunk — the keep guarantees two.
    int chest_blocks = 0, loot_entries = 0, empty_loot = 0;
    int first_loot_index = -1;
    glm::ivec3 first_loot_pos( 0 );
    ChestContents first_loot;
    for ( int gx = 0; gx < 3; gx++ ) {
        for ( int gy = 0; gy < 3; gy++ ) {
            for ( int gz = 0; gz < 3; gz++ ) {
                Chunk *c = grid[ gx ][ gy ][ gz ];
                if ( c->cells == nullptr ) {
                    continue;
                }
                for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                    if ( block_is_chest( c->id_at( i ) ) ) {
                        chest_blocks++;
                    }
                }
                StructureGen::place_fortress_loot( *c );
                for ( const auto &e : c->containers.chests ) {
                    loot_entries++;
                    int filled = 0;
                    for ( int s = 0; s < CHEST_SLOTS; s++ ) {
                        if ( e.second.slots[ s ].item_id != LAST_BLOCK_ID ) {
                            filled++;
                        }
                    }
                    if ( filled == 0 ) {
                        empty_loot++;
                    }
                    if ( first_loot_index < 0 ) {
                        first_loot_index = e.first;
                        first_loot_pos = c->chunk_pos;
                        first_loot = e.second;
                    }
                }
            }
        }
    }
    check_bool( "fortress contains chest blocks", chest_blocks > 0 );
    check_bool( "generated chests get loot", loot_entries > 0 );
    check_bool( "generated loot is never empty", empty_loot == 0 );

    // Loot determinism: a freshly generated copy of the same chunk rolls the
    // same chest contents at the same slot.
    if ( first_loot_index >= 0 ) {
        Chunk *copy = gen_chunk( first_loot_pos );
        StructureGen::place_fortress_loot( *copy );
        const auto it = copy->containers.chests.find( first_loot_index );
        bool same = it != copy->containers.chests.end( );
        if ( same ) {
            for ( int s = 0; s < CHEST_SLOTS; s++ ) {
                if ( it->second.slots[ s ].item_id != first_loot.slots[ s ].item_id ||
                     it->second.slots[ s ].quantity != first_loot.slots[ s ].quantity ) {
                    same = false;
                }
            }
        }
        check_bool( "loot rolls deterministically", same );
        // A pre-existing entry (sidecar/player contents) is never clobbered.
        ChestContents sentinel;
        sentinel.slots[ 0 ].item_id = DIAMOND_BLOCK;
        sentinel.slots[ 0 ].quantity = 64;
        copy->containers.chests[ first_loot_index ] = sentinel;
        StructureGen::place_fortress_loot( *copy );
        check_bool( "existing chest contents are preserved",
                    copy->containers.chests[ first_loot_index ].slots[ 0 ].item_id == DIAMOND_BLOCK &&
                        copy->containers.chests[ first_loot_index ].slots[ 0 ].quantity == 64 );
        free_chunk( copy );
    }

    for ( int gx = 0; gx < 3; gx++ ) {
        for ( int gy = 0; gy < 3; gy++ ) {
            for ( int gz = 0; gz < 3; gz++ ) {
                free_chunk( grid[ gx ][ gy ][ gz ] );
            }
        }
    }
    return failures;
}
