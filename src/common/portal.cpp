// Nether portal frames: detection, ignition, the survive rule, and building.
// portal.hpp describes the model; portal_travel.cpp holds the per-tick
// player side.

#include "common/portal.hpp"
#include "common/game/game_state.hpp"
#include "common/world.hpp"
#include "common/block_definitions.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp" // queue_neighbor_block_updates
#include "common/Logging.hpp"

// Cells a portal interior (or an ignition click) may hold.
static bool portal_interior_id( const BlockID id ) {
    return id == AIR || id == FIRE || id == SOUL_FIRE || id == PORTAL;
}
static bool portal_interior_at( World &world, const glm::ivec3 &pos ) {
    const BlockState bs = world.get_loaded_block( pos );
    return bs.id != LAST_BLOCK_ID && portal_interior_id( bs.id );
}
static bool portal_obsidian_at( World &world, const glm::ivec3 &pos ) {
    return world.get_loaded_block( pos ).id == OBSIDIAN;
}
static int portal_axis_coord( const glm::ivec3 &pos, const int axis ) {
    return axis ? pos.z : pos.x;
}
static glm::ivec3 portal_axis_set( glm::ivec3 pos, const int axis, const int value ) {
    ( axis ? pos.z : pos.x ) = value;
    return pos;
}

// Detect the ring when the interior spans `axis` (0: x, 1: z). The seed must
// be interior; the cavity must be a clean rectangle walled by obsidian on all
// four sides. y_lo/y_hi must be identical across every interior column — an
// L-shaped cavity just fails (light the cell in a different spot).
static bool portal_try_axis( World &world, const glm::ivec3 &seed, const int axis, PortalFrame &frame ) {
    if ( !portal_interior_at( world, seed ) ) {
        return false;
    }
    // Interior extent along the plane axis, ending on obsidian both ways.
    int lo = portal_axis_coord( seed, axis );
    int hi = lo;
    const int su = portal_axis_coord( seed, axis );
    while ( su - lo < PORTAL_MAX_INTERIOR_W && portal_interior_at( world, portal_axis_set( seed, axis, lo - 1 ) ) ) {
        lo--;
    }
    while ( hi - su < PORTAL_MAX_INTERIOR_W && portal_interior_at( world, portal_axis_set( seed, axis, hi + 1 ) ) ) {
        hi++;
    }
    if ( !portal_obsidian_at( world, portal_axis_set( seed, axis, lo - 1 ) ) ||
         !portal_obsidian_at( world, portal_axis_set( seed, axis, hi + 1 ) ) ) {
        return false;
    }
    // Every interior column needs the same y extent, both ends obsidian.
    int y_lo = 0, y_hi = 0;
    for ( int u = lo; u <= hi; u++ ) {
        const glm::ivec3 base = portal_axis_set( seed, axis, u );
        int cy_lo = base.y;
        while ( base.y - cy_lo < PORTAL_MAX_INTERIOR_H && portal_interior_at( world, glm::ivec3( base.x, cy_lo - 1, base.z ) ) ) {
            cy_lo--;
        }
        int cy_hi = base.y;
        while ( cy_hi - base.y < PORTAL_MAX_INTERIOR_H && portal_interior_at( world, glm::ivec3( base.x, cy_hi + 1, base.z ) ) ) {
            cy_hi++;
        }
        if ( !portal_obsidian_at( world, glm::ivec3( base.x, cy_lo - 1, base.z ) ) ||
             !portal_obsidian_at( world, glm::ivec3( base.x, cy_hi + 1, base.z ) ) ) {
            return false;
        }
        if ( u == lo ) {
            y_lo = cy_lo;
            y_hi = cy_hi;
        } else if ( cy_lo != y_lo || cy_hi != y_hi ) {
            return false;
        }
    }
    if ( y_hi - y_lo + 1 < 2 || y_hi - y_lo + 1 > PORTAL_MAX_INTERIOR_H ) {
        return false;
    }
    // The two side columns frame the interior for its full height + the
    // corner rows already checked by the column scan above.
    for ( int y = y_lo; y <= y_hi; y++ ) {
        if ( !portal_obsidian_at( world, portal_axis_set( glm::ivec3( seed.x, y, seed.z ), axis, lo - 1 ) ) ||
             !portal_obsidian_at( world, portal_axis_set( glm::ivec3( seed.x, y, seed.z ), axis, hi + 1 ) ) ) {
            return false;
        }
    }
    frame.lo = portal_axis_set( glm::ivec3( seed.x, y_lo, seed.z ), axis, lo );
    frame.hi = portal_axis_set( glm::ivec3( seed.x, y_hi, seed.z ), axis, hi );
    frame.axis = axis;
    return true;
}

bool portal_find_frame( World &world, const glm::ivec3 &seed, PortalFrame &frame ) {
    return portal_try_axis( world, seed, 0, frame ) || portal_try_axis( world, seed, 1, frame );
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
    const unsigned char rot = static_cast<unsigned char>( frame.axis );
    for ( int y = frame.lo.y; y <= frame.hi.y; y++ ) {
        for ( int u = portal_axis_coord( frame.lo, frame.axis ); u <= portal_axis_coord( frame.hi, frame.axis ); u++ ) {
            const glm::ivec3 pos = portal_axis_set( glm::ivec3( frame.lo.x, y, frame.lo.z ), frame.axis, u );
            const BlockState bs = { PORTAL, rot, 0, PORTAL, 0 };
            portal_write_cell( state, pos, bs );
        }
    }
    return true;
}

// Scan from `pos` along `dir` through PORTAL cells to OBSIDIAN within `max`
// steps. An unloaded edge counts as success — a portal at the torus rim
// shouldn't pop because the frame's far side hasn't loaded.
static bool portal_run_to_obsidian( World &world, glm::ivec3 pos, const glm::ivec3 &dir, const int max ) {
    for ( int d = 0; d < max; d++ ) {
        pos += dir;
        const BlockID id = world.get_loaded_block( pos ).id;
        if ( id == OBSIDIAN ) {
            return true;
        }
        if ( id == LAST_BLOCK_ID ) {
            return true;
        }
        if ( id != PORTAL ) {
            return false;
        }
    }
    return false;
}

bool portal_cell_survives( World &world, const glm::ivec3 &pos, const BlockState &block_state ) {
    // The cell sits inside a complete ring iff a PORTAL run bounded by
    // obsidian reaches both ways along its plane's horizontal axis and
    // vertically. Breaking a frame block ends the run through it, so the
    // neighbor-update kill pops the adjacent cell and the pop cascades
    // through the interior.
    const glm::ivec3 up( 0, 1, 0 ), down( 0, -1, 0 );
    for ( int axis = 0; axis < 2; axis++ ) {
        const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
        if ( portal_run_to_obsidian( world, pos, du, PORTAL_MAX_INTERIOR_W ) && //
             portal_run_to_obsidian( world, pos, -du, PORTAL_MAX_INTERIOR_W ) && //
             portal_run_to_obsidian( world, pos, up, PORTAL_MAX_INTERIOR_H ) && //
             portal_run_to_obsidian( world, pos, down, PORTAL_MAX_INTERIOR_H ) ) {
            return true;
        }
    }
    return false;
}

// Build a (w+2)x(h+2) obsidian ring (interior w×h) with its bottom row at
// `center.y` and the interior over `center` along `axis`, then fill it.
// Cells are direct writes — the destination was picked for a clear
// footprint, and each write syncs to the server as a normal block edit.
void portal_build_frame( RepGameState &state, const glm::ivec3 &center, const int axis, const int w, const int h ) {
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    const unsigned char rot = static_cast<unsigned char>( axis );
    for ( int u = -1; u <= w; u++ ) {
        for ( int y = 0; y <= h + 1; y++ ) {
            const glm::ivec3 pos = center + du * u + glm::ivec3( 0, y, 0 );
            const bool edge = u == -1 || u == w || y == 0 || y == h + 1;
            const BlockState bs = edge ? BlockState{ OBSIDIAN, 0, 0, OBSIDIAN, 0 } : BlockState{ PORTAL, rot, 0, PORTAL, 0 };
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
                if ( cur.id == LAST_BLOCK_ID || cur.id == AIR || cur.id == OBSIDIAN ) {
                    continue;
                }
                portal_write_cell( state, pos, air );
            }
        }
    }
}

// Nearest PORTAL cell to `center` within a (rx, ry) box of loaded blocks —
// the "find an existing destination portal" half of travel. Spiral-free:
// iterate the box, keep the best manhattan distance. One-shot per travel.
bool portal_find_nearest( World &world, const glm::ivec3 &center, const int rx, const int ry, glm::ivec3 &out ) {
    bool found = false;
    int best = 0;
    for ( int dy = -ry; dy <= ry; dy++ ) {
        for ( int dx = -rx; dx <= rx; dx++ ) {
            for ( int dz = -rx; dz <= rx; dz++ ) {
                const glm::ivec3 pos( center.x + dx, center.y + dy, center.z + dz );
                if ( world.get_loaded_block( pos ).id != PORTAL ) {
                    continue;
                }
                const int dist = abs( dx ) + abs( dy ) + abs( dz );
                if ( !found || dist < best ) {
                    found = true;
                    best = dist;
                    out = pos;
                }
            }
        }
    }
    return found;
}
