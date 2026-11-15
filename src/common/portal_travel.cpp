// The per-tick portal side: dwell counting inside a PORTAL cell, dimension
// travel, and the deferred destination-portal materialization. portal.cpp
// holds the world helpers; this file owns PortalState.

#include "common/portal.hpp"
#include "common/game/game_state.hpp"
#include "common/world.hpp"
#include "common/block_definitions.hpp"
#include "common/map_gen.hpp" // map_gen_consts.hpp: MGN_LAVA_SEA ledge fallback
#include "common/dimension_profile.hpp"
#include "common/harness/debug_commands.hpp" // dbgcmd::goto_
#include "common/Logging.hpp"

// How far from the drop point to look for an already-built portal before
// making a new frame (loaded area only — the destination torus).
#define PORTAL_SEARCH_RX 48
#define PORTAL_SEARCH_RY 40
// Spiral radius for the generated-ground column scan.
#define PORTAL_SCAN_RADIUS 24

static bool portal_gen_collides( const int x, const int y, const int z ) {
    const BlockID id = MapGen::gen_block_id( x, y, z );
    return id != LAST_BLOCK_ID && block_definition_get_definition( id )->collides_with_player;
}

// The band's landing-scan y range plus the open-void ledge height — inside
// the nether cavern (below the roof base, over the lava sea) or the
// overworld's terrain band (mountain cap to lava layer).
static void portal_band_bounds( const DimensionId dim, int &y_top, int &y_bottom, int &ledge_y ) {
    if ( dim == DIM_NETHER ) {
        y_top = DIM_BY_MIN_N + 170;
        y_bottom = DIM_BY_MIN_N + 34;
        ledge_y = DIM_BY_MIN_N + MGN_LAVA_SEA + 1;
    } else {
        y_top = 300;
        y_bottom = -90;
        ledge_y = WATER_LEVEL + 2;
    }
}

// A column at (x,z) is a portal site when the w+2 ring-floor cells at y
// collide and the w×(h+1) interior+lintel cavity above them is clear of
// solids and lava. `y` is the floor cell — the ring's bottom obsidian row
// replaces it. The footprint runs along `du` (the frame axis).
static bool portal_column_try( const DimensionId dim, const int x, const int z, const glm::ivec3 &du, const int w, const int h, int &floor_y ) {
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    for ( int y = y_top; y >= y_bottom; y-- ) {
        bool floor_ok = true;
        for ( int u = -1; u <= w && floor_ok; u++ ) {
            floor_ok = portal_gen_collides( x + du.x * u, y, z + du.z * u );
        }
        if ( !floor_ok ) {
            continue;
        }
        bool cavity_ok = true;
        for ( int u = 0; u < w && cavity_ok; u++ ) {
            for ( int k = 1; k <= h + 1 && cavity_ok; k++ ) {
                const BlockID id = MapGen::gen_block_id( x + du.x * u, y + k, z + du.z * u );
                cavity_ok = id != LAST_BLOCK_ID && id != LAVA && !block_definition_get_definition( id )->collides_with_player;
            }
        }
        if ( !cavity_ok ) {
            continue;
        }
        floor_y = y;
        return true;
    }
    return false;
}

// The pass-B fallback: bottom-up, the first solid cell in the band is the
// cavern floor — no cavity required, the frame embeds and a pocket is
// carved around it on arrival.
static bool portal_column_surface( const DimensionId dim, const int x, const int z, int &floor_y ) {
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    for ( int y = y_bottom; y <= y_top; y++ ) {
        if ( portal_gen_collides( x, y, z ) ) {
            floor_y = y;
            return true;
        }
    }
    return false;
}

// Feet placement from an eye target — the chat_teleport shape: goto_ snaps
// the camera (and the fly pin), then motion state is zeroed so the jump
// doesn't carry a velocity.
static void portal_teleport_feet( RepGameState &state, const glm::dvec3 &feet ) {
    const glm::dvec3 eye( feet.x, feet.y + state.camera.eye_height, feet.z );
    dbgcmd::goto_( state, eye );
    state.camera.y_speed = 0.0;
    state.camera.horizontal_vel = glm::dvec2( 0.0 );
    state.vitals.fall_dist = 0.0;
}

// Find the frame origin (ring corner: interior spans u in [0,w), floor row
// at center.y) near (bx,bz) in `dim`, for a frame spanning `axis` with a
// w×h interior. Spirals outward column by column.
// `embedded` reports a pass-B/ledge site — the arrival carve digs the
// pocket. Never fails: the last resort is an obsidian ledge over the lava
// sea (the overworld fallback is unreachable — every column has bedrock).
static bool portal_find_spot( const DimensionId dim, const int bx, const int bz, const int axis, const int w, const int h, glm::ivec3 &out, int &embedded ) {
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    embedded = 0;
    int floor_y;
    for ( int r = 0; r <= PORTAL_SCAN_RADIUS; r++ ) {
        for ( int dx = -r; dx <= r; dx++ ) {
            for ( int dz = -r; dz <= r; dz++ ) {
                if ( r != 0 && abs( dx ) != r && abs( dz ) != r ) {
                    continue;
                }
                if ( portal_column_try( dim, bx + dx, bz + dz, du, w, h, floor_y ) ) {
                    out = glm::ivec3( bx + dx, floor_y, bz + dz );
                    return true;
                }
            }
        }
    }
    for ( int r = 0; r <= PORTAL_SCAN_RADIUS * 2; r++ ) {
        for ( int dx = -r; dx <= r; dx++ ) {
            for ( int dz = -r; dz <= r; dz++ ) {
                if ( r != 0 && abs( dx ) != r && abs( dz ) != r ) {
                    continue;
                }
                if ( portal_column_surface( dim, bx + dx, bz + dz, floor_y ) ) {
                    embedded = 1;
                    out = glm::ivec3( bx + dx, floor_y, bz + dz );
                    return true;
                }
            }
        }
    }
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    embedded = 1;
    out = glm::ivec3( bx, ledge_y, bz );
    return true;
}

// Arrive inside an existing portal's plate: `found` is a PORTAL cell, so
// stand on the sill at the bottom of its interior column. must_exit keeps
// the plate from re-triggering until the player steps out.
static void portal_arrive_inside( RepGameState &state, const glm::ivec3 &found ) {
    glm::ivec3 base = found;
    while ( state.world.get_loaded_block( glm::ivec3( base.x, base.y - 1, base.z ) ).id == PORTAL ) {
        base.y--;
    }
    portal_teleport_feet( state, glm::dvec3( base.x + 0.5, base.y, base.z + 0.5 ) );
}

// Travel: drop the player at generated ground in the destination band, then
// let portal_pending_tick find or build the actual portal once the torus
// catches up. A built destination frame copies `src`'s axis and interior
// size (null → the default 2x3 x-plate).
static void portal_travel( RepGameState &state, const DimensionId dst, const PortalFrame *src ) {
    const int bx = static_cast<int>( floor( state.camera.pos.x ) );
    const int bz = static_cast<int>( floor( state.camera.pos.z ) );
    int axis = 0, w = 2, h = 3;
    if ( src ) {
        axis = src->axis;
        w = ( axis ? src->hi.z - src->lo.z : src->hi.x - src->lo.x ) + 1;
        h = src->hi.y - src->lo.y + 1;
        if ( w > PORTAL_MAX_INTERIOR_W ) {
            w = PORTAL_MAX_INTERIOR_W;
        }
        if ( h > PORTAL_MAX_INTERIOR_H ) {
            h = PORTAL_MAX_INTERIOR_H;
        }
    }
    glm::ivec3 center;
    int embedded;
    portal_find_spot( dst, bx, bz, axis, w, h, center, embedded );
    const double mid = w / 2.0;
    const glm::dvec3 feet = axis ? glm::dvec3( center.x + 0.5, center.y + 1.0, center.z + mid )
                                 : glm::dvec3( center.x + mid, center.y + 1.0, center.z + 0.5 );
    portal_teleport_feet( state, feet );
    PortalState &p = state.portal;
    p.cooldown = PORTAL_COOLDOWN_TICKS;
    p.must_exit = 1;
    p.pending_build = 1;
    p.pending_carve = embedded;
    p.pending_axis = axis;
    p.pending_w = w;
    p.pending_h = h;
    p.pending_pos = feet;
}

// Once the destination chunks are live: adopt the nearest existing PORTAL as
// the exit (stand inside its plate), else carve the pocket (embedded drops)
// and/or build the frame at the drop point.
static void portal_pending_tick( RepGameState &state ) {
    PortalState &p = state.portal;
    const glm::ivec3 c( static_cast<int>( floor( p.pending_pos.x ) ), static_cast<int>( floor( p.pending_pos.y ) ),
                        static_cast<int>( floor( p.pending_pos.z ) ) );
    if ( state.world.get_loaded_block( c ).id == LAST_BLOCK_ID ) {
        return;
    }
    const int want_build = p.pending_build;
    const int want_carve = p.pending_carve;
    const int axis = p.pending_axis;
    const int w = p.pending_w, h = p.pending_h;
    p.pending_build = 0;
    p.pending_carve = 0;
    glm::ivec3 found;
    if ( want_build && portal_find_nearest( state.world, c, PORTAL_SEARCH_RX, PORTAL_SEARCH_RY, found ) ) {
        portal_arrive_inside( state, found );
        p.must_exit = 1;
        return;
    }
    // frame origin: the interior centres on the drop point along `axis`.
    glm::ivec3 frame( c.x, c.y - 1, c.z );
    ( axis ? frame.z : frame.x ) = static_cast<int>( floor( ( axis ? p.pending_pos.z : p.pending_pos.x ) - w / 2.0 + 0.5 ) );
    if ( want_carve ) {
        portal_carve_pocket( state, frame, axis, w, h );
    }
    if ( want_build ) {
        portal_build_frame( state, frame, axis, w, h );
        // The player stands inside the new plate — without this a
        // must_exit lost to slow chunk loads would bounce them back.
        p.must_exit = 1;
    }
}

void repgame_portal_tick( RepGameState &state ) {
    PortalState &p = state.portal;
    if ( p.pending_build || p.pending_carve ) {
        portal_pending_tick( state );
    }
    if ( p.cooldown > 0 ) {
        p.cooldown--;
    }
    const glm::dvec3 &eye = state.camera.pos;
    const double feet_y = eye.y - state.camera.eye_height;
    const glm::ivec3 feet_cell( glm::round( glm::dvec3( eye.x, feet_y + 0.05, eye.z ) - glm::dvec3( 0.5 ) ) );
    const glm::ivec3 body_cell( glm::round( glm::dvec3( eye.x, feet_y + PLAYER_HEIGHT / 2.0, eye.z ) - glm::dvec3( 0.5 ) ) );
    const BlockID feet_id = state.world.get_loaded_block( feet_cell ).id;
    const BlockID body_id = state.world.get_loaded_block( body_cell ).id;
    if ( feet_id != PORTAL && body_id != PORTAL ) {
        p.ticks_inside = 0;
        // Only release must_exit on confirmed loaded ground: LAST_BLOCK_ID
        // means the destination torus hasn't caught up yet, and a pending
        // frame may still materialize around the player.
        if ( feet_id != LAST_BLOCK_ID && body_id != LAST_BLOCK_ID ) {
            p.must_exit = 0;
        }
        return;
    }
    if ( p.must_exit || p.cooldown > 0 ) {
        return;
    }
    if ( ++p.ticks_inside >= PORTAL_TRAVEL_TICKS ) {
        p.ticks_inside = 0;
        const DimensionId cur = dimension_of_pos( feet_cell );
        const DimensionId dst = cur == DIM_OVERWORLD ? DIM_NETHER : cur == DIM_NETHER ? DIM_OVERWORLD : cur;
        if ( dst != cur ) {
            // The destination frame copies this portal's axis and interior
            // size — detect the ring around whichever cell is lit.
            PortalFrame src;
            const glm::ivec3 seed = feet_id == PORTAL ? feet_cell : body_cell;
            portal_travel( state, dst, portal_find_frame( state.world, seed, src ) ? &src : nullptr );
        }
    }
}

// /dimension: same landing logic without a portal — drop on generated ground
// in `dim`; embedded landings still get their pocket carved (no frame).
bool portal_travel_to_dimension( RepGameState &state, const DimensionId dim ) {
    if ( dim != DIM_OVERWORLD && dim != DIM_NETHER ) {
        return false;
    }
    if ( dimension_of_pos( state.camera.pos ) == dim ) {
        return false;
    }
    const int bx = static_cast<int>( floor( state.camera.pos.x ) );
    const int bz = static_cast<int>( floor( state.camera.pos.z ) );
    glm::ivec3 center;
    int embedded;
    portal_find_spot( dim, bx, bz, 0, 2, 3, center, embedded );
    const glm::dvec3 feet( center.x + 1.0, center.y + 1.0, center.z + 0.5 );
    portal_teleport_feet( state, feet );
    PortalState &p = state.portal;
    p.cooldown = PORTAL_COOLDOWN_TICKS;
    p.must_exit = 1;
    p.pending_carve = embedded;
    p.pending_axis = 0;
    p.pending_w = 2;
    p.pending_h = 3;
    p.pending_pos = feet;
    return true;
}
