// Portal frames: detection, ignition, the survive rule, and building.
// portal.hpp describes the model; portal_travel.cpp holds the per-tick
// player side.

#include "common/portal.hpp"
#include "common/game/game_state.hpp"
#include "common/world.hpp"
#include "common/block_definitions.hpp"
#include "common/fluid.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp" // queue_neighbor_block_updates
#include "common/Logging.hpp"

// The portal kinds: obsidian ring + PORTAL interior bridges the overworld
// and nether; gold-block ring + REPHEIM_PORTAL interior (the repgate —
// a 1x2 hole is enough) bridges the overworld and repheim.
const PortalKind PORTAL_KINDS[PORTAL_KIND_COUNT] = {
    { OBSIDIAN, PORTAL, 2, 3 },
    { GOLD_BLOCK, REPHEIM_PORTAL, 1, 2 },
};

// Cells a portal interior (or an ignition click) may hold.
static bool portal_interior_id( const BlockID id, const BlockID interior ) {
    return id == AIR || id == FIRE || id == SOUL_FIRE || id == interior;
}
static bool portal_interior_at( World &world, const glm::ivec3 &pos, const BlockID interior ) {
    const BlockState bs = world.get_loaded_block( pos );
    return bs.id != LAST_BLOCK_ID && portal_interior_id( bs.id, interior );
}
static bool portal_ring_at( World &world, const glm::ivec3 &pos, const BlockID ring ) {
    return world.get_loaded_block( pos ).id == ring;
}
static int portal_axis_coord( const glm::ivec3 &pos, const int axis ) {
    return axis ? pos.z : pos.x;
}
static glm::ivec3 portal_axis_set( glm::ivec3 pos, const int axis, const int value ) {
    ( axis ? pos.z : pos.x ) = value;
    return pos;
}

// Detect the ring when the interior spans `axis` (0: x, 1: z). The seed must
// be interior; the cavity must be a clean rectangle walled by the kind's
// ring block on all four sides. y_lo/y_hi must be identical across every
// interior column — an L-shaped cavity just fails (light the cell in a
// different spot).
static bool portal_try_axis( World &world, const glm::ivec3 &seed, const int axis, const PortalKind &kind, PortalFrame &frame ) {
    if ( !portal_interior_at( world, seed, kind.interior ) ) {
        return false;
    }
    // Interior extent along the plane axis, ending on the ring both ways.
    int lo = portal_axis_coord( seed, axis );
    int hi = lo;
    const int su = portal_axis_coord( seed, axis );
    while ( su - lo < PORTAL_MAX_INTERIOR_W && portal_interior_at( world, portal_axis_set( seed, axis, lo - 1 ), kind.interior ) ) {
        lo--;
    }
    while ( hi - su < PORTAL_MAX_INTERIOR_W && portal_interior_at( world, portal_axis_set( seed, axis, hi + 1 ), kind.interior ) ) {
        hi++;
    }
    if ( hi - lo + 1 < kind.min_w ) {
        return false;
    }
    if ( !portal_ring_at( world, portal_axis_set( seed, axis, lo - 1 ), kind.ring ) ||
         !portal_ring_at( world, portal_axis_set( seed, axis, hi + 1 ), kind.ring ) ) {
        return false;
    }
    // Every interior column needs the same y extent, both ends ring blocks.
    int y_lo = 0, y_hi = 0;
    for ( int u = lo; u <= hi; u++ ) {
        const glm::ivec3 base = portal_axis_set( seed, axis, u );
        int cy_lo = base.y;
        while ( base.y - cy_lo < PORTAL_MAX_INTERIOR_H && portal_interior_at( world, glm::ivec3( base.x, cy_lo - 1, base.z ), kind.interior ) ) {
            cy_lo--;
        }
        int cy_hi = base.y;
        while ( cy_hi - base.y < PORTAL_MAX_INTERIOR_H && portal_interior_at( world, glm::ivec3( base.x, cy_hi + 1, base.z ), kind.interior ) ) {
            cy_hi++;
        }
        if ( !portal_ring_at( world, glm::ivec3( base.x, cy_lo - 1, base.z ), kind.ring ) ||
             !portal_ring_at( world, glm::ivec3( base.x, cy_hi + 1, base.z ), kind.ring ) ) {
            return false;
        }
        if ( u == lo ) {
            y_lo = cy_lo;
            y_hi = cy_hi;
        } else if ( cy_lo != y_lo || cy_hi != y_hi ) {
            return false;
        }
    }
    if ( y_hi - y_lo + 1 < kind.min_h || y_hi - y_lo + 1 > PORTAL_MAX_INTERIOR_H ) {
        return false;
    }
    // The two side columns frame the interior for its full height + the
    // corner rows already checked by the column scan above.
    for ( int y = y_lo; y <= y_hi; y++ ) {
        if ( !portal_ring_at( world, portal_axis_set( glm::ivec3( seed.x, y, seed.z ), axis, lo - 1 ), kind.ring ) ||
             !portal_ring_at( world, portal_axis_set( glm::ivec3( seed.x, y, seed.z ), axis, hi + 1 ), kind.ring ) ) {
            return false;
        }
    }
    frame.lo = portal_axis_set( glm::ivec3( seed.x, y_lo, seed.z ), axis, lo );
    frame.hi = portal_axis_set( glm::ivec3( seed.x, y_hi, seed.z ), axis, hi );
    frame.axis = axis;
    return true;
}

bool portal_find_frame( World &world, const glm::ivec3 &seed, PortalFrame &frame ) {
    for ( int kind = 0; kind < PORTAL_KIND_COUNT; kind++ ) {
        if ( portal_try_axis( world, seed, 0, PORTAL_KINDS[kind], frame ) ||
             portal_try_axis( world, seed, 1, PORTAL_KINDS[kind], frame ) ) {
            frame.kind = kind;
            return true;
        }
    }
    return false;
}

// One portal cell write: world + wire + neighbor wake, the same shape
// repgame_convert_block_impl uses (bypasses the placement survive check —
// the caller already validated the ring).
static void portal_write_cell( RepGameState &state, const glm::ivec3 &pos, const BlockState &bs ) {
    const BlockState cur = state.world.get_loaded_block( pos );
    if ( cur.id == LAST_BLOCK_ID || BlockStates_equal( cur, bs ) ) {
        return;
    }
    state.world.set_loaded_block( pos, bs );
    state.multiplayer.set_block( pos, bs, NET_BU_FLAG_NO_DEBRIS );
    queue_neighbor_block_updates( state, pos );
}

bool portal_ignite( RepGameState &state, const glm::ivec3 &seed ) {
    PortalFrame frame;
    if ( !portal_find_frame( state.world, seed, frame ) ) {
        return false;
    }
    if ( frame.kind == PORTAL_KIND_NETHER && dimension_of_pos( seed ) == DIM_REPHEIM ) {
        return false;
    }
    const BlockID interior = PORTAL_KINDS[frame.kind].interior;
    const unsigned char rot = static_cast<unsigned char>( frame.axis );
    for ( int y = frame.lo.y; y <= frame.hi.y; y++ ) {
        for ( int u = portal_axis_coord( frame.lo, frame.axis ); u <= portal_axis_coord( frame.hi, frame.axis ); u++ ) {
            const glm::ivec3 pos = portal_axis_set( glm::ivec3( frame.lo.x, y, frame.lo.z ), frame.axis, u );
            const BlockState bs = { interior, rot, 0, interior, 0 };
            portal_write_cell( state, pos, bs );
        }
    }
    return true;
}

// Scan from `pos` along `dir` through `interior` cells to the kind's ring
// block within `max` steps. An unloaded edge counts as success — a portal
// at the torus rim shouldn't pop because the frame's far side hasn't loaded.
static bool portal_run_to_ring( World &world, glm::ivec3 pos, const glm::ivec3 &dir, const int max, const PortalKind &kind ) {
    for ( int d = 0; d < max; d++ ) {
        pos += dir;
        const BlockID id = world.get_loaded_block( pos ).id;
        if ( id == kind.ring ) {
            return true;
        }
        if ( id == LAST_BLOCK_ID ) {
            return true;
        }
        if ( id != kind.interior ) {
            return false;
        }
    }
    return false;
}

bool portal_cell_survives( World &world, const glm::ivec3 &pos, const BlockState &block_state ) {
    // The cell sits inside a complete ring iff a run through its own portal
    // cells bounded by its kind's ring block reaches both ways along its
    // plane's horizontal axis and vertically. Breaking a frame block ends
    // the run through it, so the neighbor-update kill pops the adjacent
    // cell and the pop cascades through the interior.
    int kind = -1;
    for ( int k = 0; k < PORTAL_KIND_COUNT; k++ ) {
        if ( PORTAL_KINDS[k].interior == block_state.id ) {
            kind = k;
            break;
        }
    }
    if ( kind < 0 ) {
        return false;
    }
    const PortalKind &pk = PORTAL_KINDS[kind];
    const glm::ivec3 up( 0, 1, 0 ), down( 0, -1, 0 );
    for ( int axis = 0; axis < 2; axis++ ) {
        const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
        if ( portal_run_to_ring( world, pos, du, PORTAL_MAX_INTERIOR_W, pk ) && //
             portal_run_to_ring( world, pos, -du, PORTAL_MAX_INTERIOR_W, pk ) && //
             portal_run_to_ring( world, pos, up, PORTAL_MAX_INTERIOR_H, pk ) && //
             portal_run_to_ring( world, pos, down, PORTAL_MAX_INTERIOR_H, pk ) ) {
            return true;
        }
    }
    return false;
}

// Build a (w+2)x(h+2) ring of the kind's material (interior w×h filled with
// its portal cell) with its bottom row at `center.y` and the interior over
// `center` along `axis`. Cells are direct writes — the destination was
// picked for a clear footprint, and each write syncs to the server as a
// normal block edit.
void portal_build_frame( RepGameState &state, const glm::ivec3 &center, const int axis, const int w, const int h, const int kind ) {
    const PortalKind &pk = PORTAL_KINDS[kind];
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    const unsigned char rot = static_cast<unsigned char>( axis );
    for ( int u = -1; u <= w; u++ ) {
        for ( int y = 0; y <= h + 1; y++ ) {
            const glm::ivec3 pos = center + du * u + glm::ivec3( 0, y, 0 );
            const bool edge = u == -1 || u == w || y == 0 || y == h + 1;
            const BlockState bs = edge ? BlockState{ pk.ring, 0, 0, pk.ring, 0 } : BlockState{ pk.interior, rot, 0, pk.interior, 0 };
            portal_write_cell( state, pos, bs );
        }
    }
}

// Embedded landings (portal_find_spot's pass B) drop the frame inside solid
// rock: clear a room so the player can actually walk out. Covers the ring
// footprint in u, 3 deep on both open faces, and one above the lintel —
// the floor row (y=0) stays solid to stand on.
void portal_carve_pocket( RepGameState &state, const glm::ivec3 &center, const int axis, const int w, const int h ) {
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    const glm::ivec3 dn = axis ? glm::ivec3( 1, 0, 0 ) : glm::ivec3( 0, 0, 1 );
    const BlockState air = { AIR, 0, 0, AIR, 0 };
    for ( int u = -1; u <= w; u++ ) {
        for ( int v = -3; v <= 3; v++ ) {
            for ( int y = 1; y <= h + 2; y++ ) {
                const glm::ivec3 pos = center + du * u + dn * v + glm::ivec3( 0, y, 0 );
                const BlockState cur = state.world.get_loaded_block( pos );
                // Never write over an existing portal, an old frame's ring,
                // or unbreakable ground — a new frame landing beside one can
                // carve into its footprint, but must leave those cells intact.
                if ( cur.id == LAST_BLOCK_ID || cur.id == AIR || cur.id == PORTAL || cur.id == REPHEIM_PORTAL ||
                     cur.id == OBSIDIAN || cur.id == GOLD_BLOCK || cur.id == BEDROCK ) {
                    continue;
                }
                portal_write_cell( state, pos, air );
            }
        }
    }
}

// Floating sites (open lava/water under the frame): a slab of the kind's
// ring material one block wider than the ring's bottom row on every side —
// u in [-2, w+1], v in [-1, 1] at center.y. Only air-like and fluid cells
// are filled; solids (a shore the slab overlaps) and portal cells are left
// alone.
void portal_build_platform( RepGameState &state, const glm::ivec3 &center, const int axis, const int w, const int kind ) {
    const PortalKind &pk = PORTAL_KINDS[kind];
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    const glm::ivec3 dn = axis ? glm::ivec3( 1, 0, 0 ) : glm::ivec3( 0, 0, 1 );
    const BlockState slab = { pk.ring, 0, 0, pk.ring, 0 };
    for ( int u = -2; u <= w + 1; u++ ) {
        for ( int v = -1; v <= 1; v++ ) {
            const glm::ivec3 pos = center + du * u + dn * v;
            const BlockID id = state.world.get_loaded_block( pos ).id;
            if ( id == LAST_BLOCK_ID || id == PORTAL || id == REPHEIM_PORTAL ) {
                continue;
            }
            const Block *def = block_definition_get_definition( id );
            if ( def->collides_with_player && !block_is_fluid( def ) ) {
                continue;
            }
            portal_write_cell( state, pos, slab );
        }
    }
}

// ---------------------------------------------------------------------------
// End portals: a horizontal 5x5 ring of END_PORTAL_FRAME cells (BEDROCK
// counts — the exit fountain's ring is bedrock) around a 3x3 interior of
// AIR/FIRE/SOUL_FIRE/END_PORTAL; the four corner cells are ignored. Eyes of
// ender fill the frames one at a time (end_portal_insert_eye); once every
// frame of a complete ring holds an eye the interior ignites into
// END_PORTAL. Standing inside runs the END_PORTAL branch of
// repgame_portal_tick: in the End it drops the player on generated
// overworld ground; anywhere else it lands them on the central island's
// obsidian arrival pad beside the fountain.
// ---------------------------------------------------------------------------

static bool end_portal_interior_id( const BlockID id ) {
    return id == AIR || id == FIRE || id == SOUL_FIRE || id == END_PORTAL;
}
static bool end_portal_ring_id( const BlockID id ) {
    return id == END_PORTAL_FRAME || id == BEDROCK;
}

// Detect a complete 5x5 ring whose 3x3 interior holds `seed` — tries the
// nine interiors containing it. `lo` gets the interior's min x/z corner.
bool end_portal_find_frame( World &world, const glm::ivec3 &seed, glm::ivec3 &lo ) {
    if ( !end_portal_interior_id( world.get_loaded_block( seed ).id ) ) {
        return false;
    }
    for ( int ox = seed.x - 2; ox <= seed.x; ox++ ) {
        for ( int oz = seed.z - 2; oz <= seed.z; oz++ ) {
            bool ok = true;
            for ( int dx = -1; dx <= 3 && ok; dx++ ) {
                for ( int dz = -1; dz <= 3 && ok; dz++ ) {
                    const bool interior = dx >= 0 && dx <= 2 && dz >= 0 && dz <= 2;
                    const bool corner = ( dx == -1 || dx == 3 ) && ( dz == -1 || dz == 3 );
                    const BlockID id = world.get_loaded_block( glm::ivec3( ox + dx, seed.y, oz + dz ) ).id;
                    if ( interior ) {
                        ok = end_portal_interior_id( id );
                    } else if ( !corner ) {
                        ok = end_portal_ring_id( id );
                    }
                }
            }
            if ( ok ) {
                lo = glm::ivec3( ox, seed.y, oz );
                return true;
            }
        }
    }
    return false;
}

bool end_portal_ignite( RepGameState &state, const glm::ivec3 &seed ) {
    glm::ivec3 lo;
    if ( !end_portal_find_frame( state.world, seed, lo ) ) {
        return false;
    }
    for ( int dx = 0; dx < 3; dx++ ) {
        for ( int dz = 0; dz < 3; dz++ ) {
            const BlockState bs = { END_PORTAL, 0, 0, END_PORTAL, 0 };
            portal_write_cell( state, lo + glm::ivec3( dx, 0, dz ), bs );
        }
    }
    return true;
}

// A ring cell ready for ignition: a frame already holding its eye, or
// bedrock — the fountain ring's material can't hold an eye, but a bedrock
// ring is the pre-lit fountain anyway.
static bool end_portal_ring_ready( const BlockState &bs ) {
    if ( bs.id == BEDROCK ) {
        return true;
    }
    return bs.id == END_PORTAL_FRAME && bs.display_id == END_PORTAL_FRAME_EYE;
}

// Scan every 5x5 window whose ring holds `pos` (a non-corner perimeter
// cell): twelve eye-filled frames (or bedrock) around an open interior
// ignites — the 3x3 fills with END_PORTAL. True when a ring completed.
bool end_portal_complete_ring( RepGameState &state, const glm::ivec3 &pos ) {
    // Interior origins (ox, oz) put pos at ring offset (rx, rz) in [-1, 3].
    for ( int ox = pos.x - 3; ox <= pos.x + 1; ox++ ) {
        for ( int oz = pos.z - 3; oz <= pos.z + 1; oz++ ) {
            const int rx = pos.x - ox, rz = pos.z - oz;
            const bool edge_x = rx == -1 || rx == 3;
            const bool edge_z = rz == -1 || rz == 3;
            if ( !( edge_x || edge_z ) || ( edge_x && edge_z ) ) {
                continue; // pos is the window's interior or a corner — not a ring cell
            }
            bool ok = true;
            for ( int dx = -1; dx <= 3 && ok; dx++ ) {
                for ( int dz = -1; dz <= 3 && ok; dz++ ) {
                    const bool interior = dx >= 0 && dx <= 2 && dz >= 0 && dz <= 2;
                    const bool corner = ( dx == -1 || dx == 3 ) && ( dz == -1 || dz == 3 );
                    if ( corner ) {
                        continue;
                    }
                    const BlockState bs = state.world.get_loaded_block( glm::ivec3( ox + dx, pos.y, oz + dz ) );
                    ok = interior ? end_portal_interior_id( bs.id ) : end_portal_ring_ready( bs );
                }
            }
            if ( !ok ) {
                continue;
            }
            for ( int dx = 0; dx < 3; dx++ ) {
                for ( int dz = 0; dz < 3; dz++ ) {
                    const BlockState bs = { END_PORTAL, 0, 0, END_PORTAL, 0 };
                    portal_write_cell( state, glm::ivec3( ox + dx, pos.y, oz + dz ), bs );
                }
            }
            return true;
        }
    }
    return false;
}

// Right-clicking an END_PORTAL_FRAME with an eye of ender seats the eye —
// a display-state write: the cell id stays END_PORTAL_FRAME (picks, drops
// and ring detection all keep working) and only display_id flips to the
// hidden END_PORTAL_FRAME_EYE render variant. False when the cell isn't an
// eyeless frame. The caller runs end_portal_complete_ring afterwards.
bool end_portal_insert_eye( RepGameState &state, const glm::ivec3 &pos ) {
    const BlockState cur = state.world.get_loaded_block( pos );
    if ( cur.id != END_PORTAL_FRAME || cur.display_id == END_PORTAL_FRAME_EYE ) {
        return false;
    }
    BlockState next = cur;
    next.display_id = END_PORTAL_FRAME_EYE;
    portal_write_cell( state, pos, next );
    return true;
}

// Scan from `pos` along `dir` through END_PORTAL cells to a ring cell
// within `max` steps — same unloaded-edge rule as portal_run_to_obsidian.
static bool end_portal_run_to_frame( World &world, glm::ivec3 pos, const glm::ivec3 &dir, const int max ) {
    for ( int d = 0; d < max; d++ ) {
        pos += dir;
        const BlockID id = world.get_loaded_block( pos ).id;
        if ( end_portal_ring_id( id ) ) {
            return true;
        }
        if ( id == LAST_BLOCK_ID ) {
            return true;
        }
        if ( id != END_PORTAL ) {
            return false;
        }
    }
    return false;
}

bool end_portal_cell_survives( World &world, const glm::ivec3 &pos, const BlockState &block_state ) {
    // The pool is a flat ring interior: bounded END_PORTAL runs reaching a
    // ring cell both ways along x and z, nothing vertical. Breaking a ring
    // cell pops the adjacent cell and the cascade follows the runs.
    const glm::ivec3 dx( 1, 0, 0 ), dz( 0, 0, 1 );
    // Interior is 3x3: an edge cell is 3 cells from the far ring, so the
    // bound is 3, not the interior's own width minus one.
    return end_portal_run_to_frame( world, pos, dx, 3 ) &&  //
           end_portal_run_to_frame( world, pos, -dx, 3 ) && //
           end_portal_run_to_frame( world, pos, dz, 3 ) &&  //
           end_portal_run_to_frame( world, pos, -dz, 3 );
}

// Nearest `interior` portal cell to `center`'s x/z inside (x/z +- portal_search_radius,
// y in [y_min, y_max]), scanned per live chunk — the "find an existing
// destination portal" half of travel. Reads cells through the Chunk rather
// than get_loaded_block per cell: one pass over each chunk's array, no
// per-cell chunk lookup. Ranking: horizontal distance^2, tie-break |dy|
// to center.y. One-shot per resolve; the pending gate already promised
// every in-window box chunk is CHUNK_LIVE.
bool portal_find_nearest( World &world, const DimensionId dim, const glm::ivec3 &center, const int y_min, const int y_max, glm::ivec3 &out, const BlockID interior ) {
    const int sr = portal_search_radius( dim );
    const int x_min = center.x - sr, x_max = center.x + sr;
    const int z_min = center.z - sr, z_max = center.z + sr;
    const glm::ivec3 cs( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const glm::ivec3 cmin = glm::floor( glm::vec3( x_min, y_min, z_min ) / glm::vec3( cs ) );
    const glm::ivec3 cmax = glm::floor( glm::vec3( x_max, y_max, z_max ) / glm::vec3( cs ) );
    bool found = false;
    long best_h2 = 0;
    int best_dy = 0;
    for ( int cy = cmin.y; cy <= cmax.y; cy++ ) {
        // `dim` guards the wrap: repheim's y span covers the whole torus,
        // so a portal in another band must never be adopted.
        if ( dimension_of_chunk( cy ) != dim ) {
            continue;
        }
        for ( int cx = cmin.x; cx <= cmax.x; cx++ ) {
            for ( int cz = cmin.z; cz <= cmax.z; cz++ ) {
                const Chunk *chunk = world.get_loaded_chunk( glm::ivec3( cx, cy, cz ) * cs );
                if ( chunk == nullptr || !chunk->is_live( ) || !chunk->has_cells( ) ) {
                    continue;
                }
                const glm::ivec3 base = chunk->get_chunk_pos( ) * cs;
                const int lx0 = glm::max( x_min - base.x, 0 ), lx1 = glm::min( x_max - base.x, CHUNK_SIZE_X - 1 );
                const int ly0 = glm::max( y_min - base.y, 0 ), ly1 = glm::min( y_max - base.y, CHUNK_SIZE_Y - 1 );
                const int lz0 = glm::max( z_min - base.z, 0 ), lz1 = glm::min( z_max - base.z, CHUNK_SIZE_Z - 1 );
                for ( int ly = ly0; ly <= ly1; ly++ ) {
                    for ( int lx = lx0; lx <= lx1; lx++ ) {
                        for ( int lz = lz0; lz <= lz1; lz++ ) {
                            if ( chunk->id_at( Chunk::get_index_from_coords( lx, ly, lz ) ) != interior ) {
                                continue;
                            }
                            const int dx = base.x + lx - center.x;
                            const int dz = base.z + lz - center.z;
                            const long h2 = static_cast<long>( dx ) * dx + static_cast<long>( dz ) * dz;
                            const int dy = abs( base.y + ly - center.y );
                            if ( !found || h2 < best_h2 || ( h2 == best_h2 && dy < best_dy ) ) {
                                found = true;
                                best_h2 = h2;
                                best_dy = dy;
                                out = glm::ivec3( base.x + lx, base.y + ly, base.z + lz );
                            }
                        }
                    }
                }
            }
        }
    }
    return found;
}
