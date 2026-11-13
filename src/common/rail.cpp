#include <queue>
#include <unordered_map>

#include <glm/gtx/hash.hpp>

#include "common/rail.hpp"
#include "common/world.hpp"
#include "common/block.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"

// Horizontal directions, index 0-3 = N(-z), E(+x), S(+z), W(-x).
static const glm::ivec3 RAIL_HDIRS[ 4 ] = {
    glm::ivec3( 0, 0, -1 ), //
    glm::ivec3( 1, 0, 0 ),  //
    glm::ivec3( 0, 0, 1 ),  //
    glm::ivec3( -1, 0, 0 ), //
};

static int rail_dir_index( const glm::ivec3 &dir ) {
    for ( int d = 0; d < 4; d++ ) {
        if ( RAIL_HDIRS[ d ] == dir ) {
            return d;
        }
    }
    return 0;
}

// Endpoint direction indices and per-end rise, indexed by shape — the table
// rail_endpoints serves out of. The rising end of an ascending shape comes
// first.
static const signed char RAIL_END_DIRS[ RAIL_SHAPE_COUNT ][ 2 ] = {
    { 0, 2 }, // RAIL_NS
    { 1, 3 }, // RAIL_EW
    { 1, 3 }, // RAIL_ASC_E
    { 3, 1 }, // RAIL_ASC_W
    { 0, 2 }, // RAIL_ASC_N
    { 2, 0 }, // RAIL_ASC_S
    { 2, 1 }, // RAIL_SE
    { 2, 3 }, // RAIL_SW
    { 0, 3 }, // RAIL_NW
    { 0, 1 }, // RAIL_NE
};
static const signed char RAIL_END_DY[ RAIL_SHAPE_COUNT ][ 2 ] = {
    { 0, 0 }, // RAIL_NS
    { 0, 0 }, // RAIL_EW
    { 1, 0 }, // RAIL_ASC_E
    { 1, 0 }, // RAIL_ASC_W
    { 1, 0 }, // RAIL_ASC_N
    { 1, 0 }, // RAIL_ASC_S
    { 0, 0 }, // RAIL_SE
    { 0, 0 }, // RAIL_SW
    { 0, 0 }, // RAIL_NW
    { 0, 0 }, // RAIL_NE
};

void rail_endpoints( RailShape shape, glm::ivec3 dirs[ 2 ], int dy[ 2 ] ) {
    const int s = shape < RAIL_SHAPE_COUNT ? shape : 0;
    for ( int i = 0; i < 2; i++ ) {
        dirs[ i ] = RAIL_HDIRS[ (int)RAIL_END_DIRS[ s ][ i ] ];
        dy[ i ] = RAIL_END_DY[ s ][ i ];
    }
}

// The rise (0 flat, 1 climbing) of `shape`'s endpoint facing direction d
// (0-3 in the N/E/S/W index space), or -1 when the shape doesn't reach that
// side — i.e. "the shape points at the cell in direction d".
static int rail_end_rise( RailShape shape, int d ) {
    const int s = shape < RAIL_SHAPE_COUNT ? shape : 0;
    for ( int i = 0; i < 2; i++ ) {
        if ( RAIL_END_DIRS[ s ][ i ] == d ) {
            return RAIL_END_DY[ s ][ i ];
        }
    }
    return -1;
}

// Curve rotation stored in cells (save/wire format — never change it).
static const unsigned char RAIL_CURVE_ROT[ 4 ] = { 3, 0, 1, 2 };           // shape - RAIL_SE -> rotation
static const unsigned char RAIL_CURVE_SHAPE[ 4 ] = { 1, 2, 3, 0 };         // rotation -> shape - RAIL_SE
// Mesher yaw per curve, independent of the stored rotation. Measured from
// top-down captures: the turned tile at yaw 0 draws an N+W arc, and each
// yaw step turns it 90° clockwise seen from above (NW -> NE -> SE -> SW).
static const unsigned char RAIL_CURVE_RENDER_YAW[ 4 ] = { 2, 3, 0, 1 };    // shape - RAIL_SE -> face_shift yaw

RailShape rail_shape_of( const BlockState &state ) {
    if ( state.display_id == RAIL_CORNER ) {
        return static_cast<RailShape>( RAIL_SE + RAIL_CURVE_SHAPE[ state.rotation & 3 ] );
    }
    // Shapes 0-5 live in the rotation bits directly; anything outside is an
    // old/foreign rotation — read it as the MC default straight rail.
    static const RailShape by_rotation[ 8 ] = {
        RAIL_NS, RAIL_EW, RAIL_ASC_E, RAIL_ASC_W, RAIL_ASC_N, RAIL_ASC_S, RAIL_NS, RAIL_NS,
    };
    return by_rotation[ state.rotation & 7 ];
}

BlockState rail_state_with_shape( BlockState state, RailShape shape ) {
    if ( shape >= RAIL_SE && state.id == RAIL ) {
        state.display_id = RAIL_CORNER;
        state.rotation = RAIL_CURVE_ROT[ shape - RAIL_SE ];
    } else {
        if ( shape >= RAIL_SE ) {
            shape = RAIL_NS; // golden rails never curve
        }
        state.display_id = state.id;
        state.rotation = static_cast<unsigned char>( shape );
    }
    return state;
}

unsigned char rail_render_yaw( RailShape shape ) {
    // The turned tile's arc lands on a different corner per yaw — see
    // RAIL_CURVE_RENDER_YAW above.
    if ( shape >= RAIL_SE ) {
        return RAIL_CURVE_RENDER_YAW[ shape - RAIL_SE ];
    }
    // Straights and ascends share the straight tile: yaw 0/2 lays it N-S,
    // yaw 1/3 E-W. For ascends the nibble is also the uphill direction
    // (0=-z, 1=+x, 2=+z, 3=-x) that BLOCK_COORDS_SLOPE reads.
    switch ( shape ) {
        case RAIL_NS:
        case RAIL_ASC_N:
            return 0;
        case RAIL_EW:
        case RAIL_ASC_E:
            return 1;
        case RAIL_ASC_S:
            return 2;
        case RAIL_ASC_W:
            return 3;
        default:
            return 0;
    }
}

bool rail_is_ascending( RailShape shape ) {
    return shape == RAIL_ASC_E || shape == RAIL_ASC_W || shape == RAIL_ASC_N || shape == RAIL_ASC_S;
}

int rail_pick_height_px( const BlockState &state ) {
    if ( rail_is_ascending( rail_shape_of( state ) ) ) {
        return 8;
    }
    return block_definition_get_definition( state.id )->scale.y;
}

// Whether `state` (already known to be a rail) at `npos` accepts a link
// from direction dir_to_us (the direction it would point at us): its
// current shape already reaches that side, or it still has a free end
// (fewer than 2 geometric links of its own). Mirrors MC's "points at me or
// has room" rule — without it tracks re-shape into neighbours they never
// connected to.
static bool rail_accepts( World &world, const glm::ivec3 &npos, const BlockState &nstate, int dir_to_us ) {
    if ( rail_end_rise( rail_shape_of( nstate ), dir_to_us ) >= 0 ) {
        return true;
    }
    int links = 0;
    for ( int d2 = 0; d2 < 4 && links < 2; d2++ ) {
        if ( d2 == dir_to_us ) {
            // The asker itself doesn't count against its own acceptance: a
            // line-end rail has exactly one real connection, not two.
            continue;
        }
        const glm::ivec3 dir2 = RAIL_HDIRS[ d2 ];
        const BlockState s2 = world.get_loaded_block( npos + dir2 );
        if ( s2.id != LAST_BLOCK_ID && block_definition_get_definition( s2.id )->is_rail ) {
            links++;
            continue;
        }
        const BlockState u2 = world.get_loaded_block( npos + dir2 + glm::ivec3( 0, 1, 0 ) );
        if ( u2.id != LAST_BLOCK_ID && block_definition_get_definition( u2.id )->is_rail ) {
            links++;
            continue;
        }
        const BlockState d2s = world.get_loaded_block( npos + dir2 - glm::ivec3( 0, 1, 0 ) );
        if ( d2s.id != LAST_BLOCK_ID && block_definition_get_definition( d2s.id )->is_rail &&
             rail_end_rise( rail_shape_of( d2s ), ( d2 + 2 ) & 3 ) == 1 ) {
            links++;
        }
    }
    return links < 2;
}

// One direction's link scan for a rail at `pos`: the neighbour it would
// connect toward. out_rise = 1 when the link climbs (rail at pos+d+up — we
// ascend toward it), 0 for a level link (same-y rail, or a rail at
// pos+d-up ascending toward us). Sets *not_ready and returns false when a
// probed cell is unloaded.
static bool rail_dir_link( World &world, const glm::ivec3 &pos, int d, int *out_rise, bool *not_ready ) {
    const glm::ivec3 dir = RAIL_HDIRS[ d ];
    const int back = ( d + 2 ) & 3;
    const BlockState same = world.get_loaded_block( pos + dir );
    const BlockState up = world.get_loaded_block( pos + dir + glm::ivec3( 0, 1, 0 ) );
    const BlockState down = world.get_loaded_block( pos + dir - glm::ivec3( 0, 1, 0 ) );
    if ( same.id == LAST_BLOCK_ID || up.id == LAST_BLOCK_ID || down.id == LAST_BLOCK_ID ) {
        *not_ready = true;
        return false;
    }
    if ( block_definition_get_definition( same.id )->is_rail && rail_accepts( world, pos + dir, same, back ) ) {
        *out_rise = 0;
        return true;
    }
    if ( block_definition_get_definition( up.id )->is_rail && rail_accepts( world, pos + dir + glm::ivec3( 0, 1, 0 ), up, back ) ) {
        *out_rise = 1;
        return true;
    }
    // A rail below in direction d only links when it climbs toward us —
    // its high end must face back (dir -d, rise 1).
    if ( block_definition_get_definition( down.id )->is_rail && rail_end_rise( rail_shape_of( down ), back ) == 1 ) {
        *out_rise = 0;
        return true;
    }
    return false;
}

BlockState rail_resolve_shape( World &world, const glm::ivec3 &pos, const BlockState &state, int placer_yaw ) {
    int rise[ 4 ];
    bool not_ready = false;
    for ( int d = 0; d < 4; d++ ) {
        rise[ d ] = -1;
        rail_dir_link( world, pos, d, &rise[ d ], &not_ready );
    }
    if ( not_ready ) {
        // Chunk-edge reads would guess wrong — keep the current shape until
        // the neighbours load (same verdict as dust's "Not ready").
        return state;
    }

    const RailShape cur = rail_shape_of( state );
    {
        // Sticky: while both ends of the current shape still link (with the
        // same rises), keep it — T-junctions hold their first pick and a
        // resolved cell doesn't oscillate as neighbours settle.
        glm::ivec3 dirs[ 2 ];
        int dy[ 2 ];
        rail_endpoints( cur, dirs, dy );
        bool still_linked = true;
        for ( int i = 0; i < 2; i++ ) {
            if ( rise[ rail_dir_index( dirs[ i ] ) ] != dy[ i ] ) {
                still_linked = false;
            }
        }
        if ( still_linked ) {
            return state;
        }
    }

    int shape = -1;
    // A straight pair beats everything (N+S first, then E+W).
    if ( rise[ 0 ] >= 0 && rise[ 2 ] >= 0 ) {
        shape = RAIL_NS;
    } else if ( rise[ 1 ] >= 0 && rise[ 3 ] >= 0 ) {
        shape = RAIL_EW;
    } else {
        // A curve needs two level links; golden rails never curve.
        if ( state.id == RAIL ) {
            if ( rise[ 2 ] == 0 && rise[ 1 ] == 0 ) {
                shape = RAIL_SE;
            } else if ( rise[ 2 ] == 0 && rise[ 3 ] == 0 ) {
                shape = RAIL_SW;
            } else if ( rise[ 0 ] == 0 && rise[ 3 ] == 0 ) {
                shape = RAIL_NW;
            } else if ( rise[ 0 ] == 0 && rise[ 1 ] == 0 ) {
                shape = RAIL_NE;
            }
        }
        if ( shape < 0 ) {
            if ( rise[ 0 ] >= 0 || rise[ 2 ] >= 0 ) {
                shape = RAIL_NS;
            } else if ( rise[ 1 ] >= 0 || rise[ 3 ] >= 0 ) {
                shape = RAIL_EW;
            } else if ( cur >= RAIL_SE ) {
                // No links left on a curve — keep it rather than guessing.
                return state;
            } else if ( placer_yaw >= 0 ) {
                // Fresh placement with no neighbours: lie along the
                // player's facing axis (same yaw buckets as
                // getPlacedRotation).
                const int a = ( ( placer_yaw % 360 ) + 360 ) % 360;
                const int rot = a < 45 ? 0 : a < 135 ? 1 : a < 225 ? 2 : a < 315 ? 3 : 0;
                shape = ( rot & 1 ) ? RAIL_EW : RAIL_NS;
            } else {
                // Keep the current straight axis.
                shape = cur == RAIL_EW || cur == RAIL_ASC_E || cur == RAIL_ASC_W ? RAIL_EW : RAIL_NS;
            }
        }
    }

    // A straight whose only uphill end is one side becomes ascending.
    if ( shape == RAIL_NS ) {
        if ( rise[ 0 ] == 1 && rise[ 2 ] != 1 ) {
            shape = RAIL_ASC_N;
        } else if ( rise[ 2 ] == 1 && rise[ 0 ] != 1 ) {
            shape = RAIL_ASC_S;
        }
    } else if ( shape == RAIL_EW ) {
        if ( rise[ 1 ] == 1 && rise[ 3 ] != 1 ) {
            shape = RAIL_ASC_E;
        } else if ( rise[ 3 ] == 1 && rise[ 1 ] != 1 ) {
            shape = RAIL_ASC_W;
        }
    }
    return rail_state_with_shape( state, static_cast<RailShape>( shape ) );
}

bool golden_rail_chain_powered( World &world, const glm::ivec3 &pos ) {
    std::unordered_map<glm::ivec3, int> distances;
    std::queue<glm::ivec3> pending;
    distances.emplace( pos, 0 );
    pending.push( pos );
    while ( !pending.empty( ) ) {
        const glm::ivec3 cur = pending.front( );
        pending.pop( );
        const int dist = distances[ cur ];
        if ( dist > 0 && mechanism_direct_power( world, cur ) > 0 ) {
            return true;
        }
        if ( dist >= 8 ) {
            continue;
        }
        const BlockState st = world.get_loaded_block( cur );
        if ( st.id != GOLDEN_RAIL ) {
            continue;
        }
        glm::ivec3 dirs[ 2 ];
        int dy[ 2 ];
        rail_endpoints( rail_shape_of( st ), dirs, dy );
        for ( int i = 0; i < 2; i++ ) {
            const int d = rail_dir_index( dirs[ i ] );
            const int back = ( d + 2 ) & 3;
            if ( dy[ i ] ) {
                // Our high end: the rail it climbs toward must face back.
                const glm::ivec3 npos = cur + dirs[ i ] + glm::ivec3( 0, 1, 0 );
                const BlockState nst = world.get_loaded_block( npos );
                if ( nst.id == GOLDEN_RAIL && rail_end_rise( rail_shape_of( nst ), back ) == 0 && distances.emplace( npos, dist + 1 ).second ) {
                    pending.push( npos );
                }
            } else {
                // Level end: a same-y rail facing back at us, or a rail one
                // down that climbs toward us (its high end faces back).
                const glm::ivec3 npos = cur + dirs[ i ];
                const BlockState nst = world.get_loaded_block( npos );
                if ( nst.id == GOLDEN_RAIL && rail_end_rise( rail_shape_of( nst ), back ) == 0 && distances.emplace( npos, dist + 1 ).second ) {
                    pending.push( npos );
                }
                const glm::ivec3 lpos = cur + dirs[ i ] - glm::ivec3( 0, 1, 0 );
                const BlockState lst = world.get_loaded_block( lpos );
                if ( lst.id == GOLDEN_RAIL && rail_end_rise( rail_shape_of( lst ), back ) == 1 && distances.emplace( lpos, dist + 1 ).second ) {
                    pending.push( lpos );
                }
            }
        }
    }
    return false;
}
