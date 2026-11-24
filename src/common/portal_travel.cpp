// The per-tick portal side: dwell counting inside a PORTAL cell, dimension
// travel, and the deferred destination-portal materialization. portal.cpp
// holds the world helpers; this file owns PortalState.

#include <chrono>

#include "common/portal.hpp"
#include "common/audio.hpp"
#include "common/game/game_state.hpp"
#include "common/world.hpp"
#include "common/block_definitions.hpp"
#include "common/fluid.hpp"
#include "common/map_gen.hpp" // map_gen_consts.hpp: MGN_LAVA_SEA ledge fallback
#include "common/multiplayer.hpp"
#include "common/dimension_profile.hpp"
#include "common/harness/debug_commands.hpp" // dbgcmd::goto_
#include "common/Logging.hpp"
#include "common/RepGame.hpp" // repgame_goto_respawn_point

// Spiral radius for the generated-ground column scan.
#define PORTAL_SCAN_RADIUS 24

// The block source for the site scan: nullptr reads untouched generated
// terrain (MapGen::gen_block_id — the hold-position pick);
// a World reads the loaded chunks (the resolve-time build-site pick). A
// LAST_BLOCK_ID cell (unloaded) skips that scan row, never poisons the
// column — the loaded window is narrower than the band's scan range.
static BlockID portal_site_block( World *world, const int x, const int y, const int z ) {
    return world ? world->get_loaded_block( glm::ivec3( x, y, z ) ).id : MapGen::gen_block_id( x, y, z );
}

// Scan-frame y -> encoded block y. The repheim band wraps the signed
// chunk-y field, so its scan bounds are rep-frame values ([-90,300] —
// rep frame 0 is sea level) that must be mapped row by row: rep [0,300]
// encodes [-7936,-7636] and rep [-90,-1] encodes [7846,7935]. Every other
// dimension's scan frame is the encoded block y itself.
static int portal_scan_block_y( const DimensionId dim, const int sy ) {
    return dim == DIM_REPHEIM ? repheim_encoded_block_y( sy ) : sy;
}

// The band's landing-scan y range plus the open-void ledge height — inside
// the nether cavern (below the roof base, over the lava sea) or the
// overworld's terrain band (mountain cap to lava layer). y_top/y_bottom are
// scan-frame values (see portal_scan_block_y); ledge_y is an encoded block y.
static void portal_band_bounds( const DimensionId dim, int &y_top, int &y_bottom, int &ledge_y ) {
    if ( dim == DIM_NETHER ) {
        // Scan the open cavern: from just over the lava sea up to a hair
        // under the lowest possible roof base. The ledge sits on the sea.
        y_top = DIM_BY_MIN_N + static_cast<int>( MGN_ROOF_MID - 20.0f );
        y_bottom = DIM_BY_MIN_N + MGN_LAVA_SEA + 2;
        ledge_y = DIM_BY_MIN_N + MGN_LAVA_SEA + 1;
    } else if ( dim == DIM_END ) {
        // The island layer: island tops (plus chorus) live in this window;
        // the ledge falls back to a platform at the island's level.
        y_top = DIM_BY_MIN_E + MGE_SCAN_TOP;
        y_bottom = DIM_BY_MIN_E + MGE_SCAN_BOT;
        ledge_y = DIM_BY_MIN_E + MGE_PAD_Y;
    } else if ( dim == DIM_REPHEIM ) {
        // The overworld's terrain window in the rep frame — including the
        // below-sea half (ocean floors), which the wrapped band encodes
        // in [7846,7935]. The ledge floats at sea level.
        y_top = 300;
        y_bottom = -90;
        ledge_y = DIM_BY_MIN_3_LO + WATER_LEVEL + 2;
    } else {
        y_top = 300;
        y_bottom = -90;
        ledge_y = WATER_LEVEL + 2;
    }
}

// The full block-y span of a dimension band — the existing-portal search
// box's y extent (a shelf far above/below the drop can hold the exit).
// The repheim band's two encoded halves aren't one contiguous range, so it
// returns the wrapping span instead — consumers filter chunks to the band.
static void portal_band_span( const DimensionId dim, int &y_min, int &y_max ) {
    if ( dim == DIM_NETHER ) {
        y_min = DIM_BY_MIN_N;
        y_max = DIM_BY_MAX_N;
    } else if ( dim == DIM_END ) {
        y_min = DIM_BY_MIN_E;
        y_max = DIM_BY_MAX_E;
    } else if ( dim == DIM_REPHEIM ) {
        y_min = DIM_BY_MIN_3_LO;
        y_max = DIM_BY_MAX_3_HI;
    } else {
        y_min = DIM_BY_MIN_O;
        y_max = DIM_BY_MAX_O;
    }
}

// Loaded-world-only site veto: the frame's clearance volume — u in
// [-2, w+1] along the axis, v in [-3, 3] normal to it, y in [0, h+2] above
// the ring's bottom row — must hold no portal cell, ring block or BEDROCK.
// Keeps a fresh build from overlapping an existing frame (lit or not) or
// unbreakable ground.
static bool portal_clearance_ok( World &world, const glm::ivec3 &origin, const int axis, const int w, const int h ) {
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    const glm::ivec3 dn = axis ? glm::ivec3( 1, 0, 0 ) : glm::ivec3( 0, 0, 1 );
    for ( int u = -2; u <= w + 1; u++ ) {
        for ( int v = -3; v <= 3; v++ ) {
            for ( int y = 0; y <= h + 2; y++ ) {
                const BlockID id = world.get_loaded_block( origin + du * u + dn * v + glm::ivec3( 0, y, 0 ) ).id;
                if ( id == PORTAL || id == REPHEIM_PORTAL || id == OBSIDIAN || id == GOLD_BLOCK || id == BEDROCK ) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool portal_is_fluid( const BlockID id ) {
    return block_is_fluid( block_definition_get_definition( id ) );
}

// A column at (x,z) is a portal site when the w+2 ring-floor cells at y
// collide and the w×(h+1) interior+lintel cavity above them is clear of
// solids and lava (`dry`: of any fluid — no frame on a sea floor). `y` is
// the floor cell — the ring's bottom obsidian row replaces it. The
// footprint runs along `du` (the frame axis).
static bool portal_column_try( const DimensionId dim, const int x, const int z, const glm::ivec3 &du, const int w, const int h, int &floor_y, World *world, const bool dry = false ) {
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    for ( int sy = y_top; sy >= y_bottom; sy-- ) {
        const int y = portal_scan_block_y( dim, sy );
        // Unloaded cells are "unknown at this y", not "no site": the
        // overworld scan range is wider than the loaded window, so a
        // LAST_BLOCK_ID just skips the row — it must not poison the
        // whole column (every overworld build would fall to the ledge).
        bool floor_ok = true;
        bool column_loaded = true;
        for ( int u = -1; u <= w && floor_ok && column_loaded; u++ ) {
            const BlockID id = portal_site_block( world, x + du.x * u, y, z + du.z * u );
            column_loaded = id != LAST_BLOCK_ID;
            floor_ok = column_loaded && block_definition_get_definition( id )->collides_with_player;
        }
        if ( !column_loaded || !floor_ok ) {
            continue;
        }
        bool cavity_ok = true;
        for ( int u = 0; u < w && cavity_ok && column_loaded; u++ ) {
            for ( int k = 1; k <= h + 1 && cavity_ok && column_loaded; k++ ) {
                const BlockID id = portal_site_block( world, x + du.x * u, y + k, z + du.z * u );
                column_loaded = id != LAST_BLOCK_ID;
                cavity_ok = column_loaded && id != LAVA && !block_definition_get_definition( id )->collides_with_player &&
                            !( dry && portal_is_fluid( id ) );
            }
        }
        if ( !column_loaded || !cavity_ok ) {
            continue;
        }
        floor_y = y;
        return true;
    }
    return false;
}

// A floating site: the column at (x,z) is open down to a fluid surface
// (the first non-air cell below the scan top is lava/water — the nether's
// lava sea, an ocean), and the frame floats one block above it. `floor_y`
// is the slab row (surface + 1): the ring's bottom row plus the standing
// ring around it (portal_build_platform), u in [-2, w+1], v in [-1, 1].
// Every slab cell must be known, with air (no solid, no fluid) above it —
// 2 cells of headroom on the ring, the full frame height on the frame row.
static bool portal_column_float( const DimensionId dim, const int x, const int z, const glm::ivec3 &du, const glm::ivec3 &dn, const int w, const int h, int &floor_y, World *world ) {
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    // The nether's y_bottom sits 2 over the lava sea — reach the surface.
    int sy = y_top;
    for ( ; sy >= y_bottom - 2; sy-- ) {
        const BlockID id = portal_site_block( world, x, portal_scan_block_y( dim, sy ), z );
        if ( id == LAST_BLOCK_ID ) {
            continue;
        }
        if ( portal_is_fluid( id ) ) {
            break;
        }
        if ( block_definition_get_definition( id )->collides_with_player ) {
            return false;
        }
    }
    if ( sy < y_bottom - 2 ) {
        return false;
    }
    const int fsy = sy + 1;
    for ( int u = -2; u <= w + 1; u++ ) {
        for ( int v = -1; v <= 1; v++ ) {
            const int cx = x + du.x * u + dn.x * v, cz = z + du.z * u + dn.z * v;
            if ( portal_site_block( world, cx, portal_scan_block_y( dim, fsy ), cz ) == LAST_BLOCK_ID ) {
                return false;
            }
            const int k_max = v == 0 && u >= -1 && u <= w ? h + 1 : 2;
            for ( int k = 1; k <= k_max; k++ ) {
                const BlockID id = portal_site_block( world, cx, portal_scan_block_y( dim, fsy + k ), cz );
                if ( id == LAST_BLOCK_ID || portal_is_fluid( id ) || block_definition_get_definition( id )->collides_with_player ) {
                    return false;
                }
            }
        }
    }
    floor_y = portal_scan_block_y( dim, fsy );
    return true;
}

// The pass-B fallback: the first solid cell in the band is the cavern
// floor — no cavity required, the frame embeds and a pocket is carved
// around it on arrival. Bottom-up for the solid masses (overworld/nether),
// top-down for the End: its islands hang in a void, so the first solid
// from above is the island TOP, not its belly.
static bool portal_column_surface( const DimensionId dim, const int x, const int z, int &floor_y, World *world ) {
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    const bool top_down = dim == DIM_END;
    for ( int d = 0; d <= y_top - y_bottom; d++ ) {
        const int sy = top_down ? y_top - d : y_bottom + d;
        const int y = portal_scan_block_y( dim, sy );
        const BlockID id = portal_site_block( world, x, y, z );
        if ( id == LAST_BLOCK_ID ) {
            continue; // skip unknown rows — same rule as portal_column_try
        }
        if ( !block_definition_get_definition( id )->collides_with_player ) {
            continue;
        }
        // A solid cell on the far side of an unloaded one is the window's
        // edge, not a real floor — demand the approach-side cell is known.
        // (approach cell is also scan-frame mapped: repheim's -1/0 seam is
        // encoded 7935/-7936, not adjacent in encoded space.)
        if ( world != nullptr && portal_site_block( world, x, portal_scan_block_y( dim, sy + ( top_down ? 1 : -1 ) ), z ) == LAST_BLOCK_ID ) {
            continue;
        }
        floor_y = y;
        return true;
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

// Square-ring spiral out from (bx,bz) to radius r_max: the first column
// `try_column` accepts ends the walk.
template <typename F>
static bool portal_spiral( const int bx, const int bz, const int r_max, F &&try_column ) {
    for ( int r = 0; r <= r_max; r++ ) {
        for ( int dx = -r; dx <= r; dx++ ) {
            for ( int dz = -r; dz <= r; dz++ ) {
                if ( ( r == 0 || abs( dx ) == r || abs( dz ) == r ) && try_column( bx + dx, bz + dz ) ) {
                    return true;
                }
            }
        }
    }
    return false;
}

// How far a nether-bound build may drift off the anchor and still link
// both ways: the next trip from the same overworld portal searches only
// portal_search_radius( DIM_NETHER ) around the anchor, and the return
// trip multiplies the drift by PORTAL_COORD_SCALE against the overworld's
// PORTAL_SEARCH_R box (the source's own /8 rounding eats the last block).
// Overworld-bound drift divides by 8 on the way back, so its widest scan
// (PORTAL_SCAN_RADIUS * 2) already fits — no cap there.
static int portal_max_drift( const DimensionId dim ) {
    return dim == DIM_NETHER ? portal_search_radius( DIM_NETHER ) - 1 : PORTAL_SCAN_RADIUS * 2;
}

// Find the frame origin (ring corner: interior spans u in [0,w), floor row
// at center.y) near (bx,bz) in `dim`, for a frame spanning `axis` with a
// w×h interior. Spirals outward column by column.
// `embedded` reports a pass-B/ledge site — the arrival carve digs the
// pocket. Never fails: the last resort is an obsidian ledge over the lava
// sea (the overworld fallback is unreachable — every column has bedrock).
// `world` != nullptr reads the loaded chunks (LAST_BLOCK_ID rows are
// skipped as unknown) and vetoes each candidate on portal_clearance_ok; nullptr reads
// generated terrain (hold position — no clearance check; the generated
// world never holds portals).
// `floating` != nullptr enables the link-preserving passes (portal
// travel): a dry site near the anchor wins, then a frame floated over
// open lava/water — before the old wet-floor/embedded scans — and every
// pass is clamped to portal_max_drift, so the exit never lands where the
// return trip (or the next trip from the same source) can't find it.
// `*floating` reports a floated (or ledge) site: the resolve lays
// portal_build_platform under it.
static bool portal_find_spot( const DimensionId dim, const int bx, const int bz, const int axis, const int w, const int h, glm::ivec3 &out, int &embedded, World *world = nullptr, int *floating = nullptr ) {
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    const glm::ivec3 dn = axis ? glm::ivec3( 1, 0, 0 ) : glm::ivec3( 0, 0, 1 );
    embedded = 0;
    if ( floating ) {
        *floating = 0;
    }
    int floor_y;
    // A column whose pass test set floor_y becomes the site unless the
    // loaded world's clearance check vetoes it.
    const auto accept = [ & ]( const int x, const int z ) {
        const glm::ivec3 origin( x, floor_y, z );
        if ( world != nullptr && !portal_clearance_ok( *world, origin, axis, w, h ) ) {
            return false;
        }
        out = origin;
        return true;
    };
    const int max_r = floating ? portal_max_drift( dim ) : PORTAL_SCAN_RADIUS * 2;
    const int scan_r = glm::min( PORTAL_SCAN_RADIUS, max_r );
    if ( floating ) {
        if ( portal_spiral( bx, bz, scan_r, [ & ]( int x, int z ) { return portal_column_try( dim, x, z, du, w, h, floor_y, world, true ) && accept( x, z ); } ) ) {
            return true;
        }
        if ( portal_spiral( bx, bz, scan_r, [ & ]( int x, int z ) { return portal_column_float( dim, x, z, du, dn, w, h, floor_y, world ) && accept( x, z ); } ) ) {
            *floating = 1;
            return true;
        }
    }
    if ( portal_spiral( bx, bz, scan_r, [ & ]( int x, int z ) { return portal_column_try( dim, x, z, du, w, h, floor_y, world ) && accept( x, z ); } ) ) {
        return true;
    }
    embedded = 1;
    if ( portal_spiral( bx, bz, max_r, [ & ]( int x, int z ) { return portal_column_surface( dim, x, z, floor_y, world ) && accept( x, z ); } ) ) {
        return true;
    }
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    // The ledge floats over the lava sea / at sea level: it gets the
    // standing ring too when the caller lays platforms.
    if ( floating ) {
        *floating = 1;
    }
    // The lava-sea ledge, walked along the same spiral columns until one
    // passes the clearance check; when none does the ledge is used anyway
    // (still better than failing the travel outright).
    floor_y = ledge_y;
    if ( world != nullptr ) {
        if ( portal_spiral( bx, bz, max_r, accept ) ) {
            return true;
        }
        pr_debug( "portal build: no cleared ledge site, using the centre column anyway" );
    }
    out = glm::ivec3( bx, ledge_y, bz );
    return true;
}

// Feet at the bottom of an existing portal's plate: `found` is an
// `interior` cell, so stand on the sill at the bottom of its column.
static glm::dvec3 portal_arrive_feet_inside( RepGameState &state, const glm::ivec3 &found, const BlockID interior ) {
    glm::ivec3 base = found;
    while ( state.world.get_loaded_block( glm::ivec3( base.x, base.y - 1, base.z ) ).id == interior ) {
        base.y--;
    }
    return glm::dvec3( base.x + 0.5, base.y, base.z + 0.5 );
}

static int portal_axis_u( const glm::ivec3 &pos, const int axis ) {
    return axis ? pos.z : pos.x;
}

// Feet inside a frame whose PortalFrame bounds are known: bottom interior
// row (lo.y), centred along the axis, +0.5 on the thin one.
static glm::dvec3 portal_arrive_feet_frame( const PortalFrame &frame ) {
    const double u_mid = ( portal_axis_u( frame.lo, frame.axis ) + portal_axis_u( frame.hi, frame.axis ) + 1 ) / 2.0;
    return frame.axis ? glm::dvec3( frame.lo.x + 0.5, frame.lo.y, u_mid )
                      : glm::dvec3( u_mid, frame.lo.y, frame.lo.z + 0.5 );
}

// The 8:1 horizontal scale applied to a source portal's x/z: nether-bound
// divides by PORTAL_COORD_SCALE (floor, so negatives map like Minecraft),
// nether-to-elsewhere multiplies — 1 nether block is 8 overworld blocks.
// Trips that don't touch the nether (repheim/end -> overworld) keep x/z.
static int portal_scale_coord( const int v, const DimensionId src, const DimensionId dst ) {
    if ( dst == DIM_NETHER && src != DIM_NETHER ) {
        return static_cast<int>( floor( v / static_cast<double>( PORTAL_COORD_SCALE ) ) );
    }
    return src == DIM_NETHER && dst != DIM_NETHER ? v * PORTAL_COORD_SCALE : v;
}

// Travel: drop the player at generated ground in the destination band and
// freeze them on hold_feet until portal_pending_tick's readiness gate says
// the destination chunks (and, in MP, the server's edit diffs) are in —
// only then does it search for an existing portal or build one. The
// destination anchor is the source portal's x/z scaled by the 8:1 nether
// ratio (portal_scale_coord) — every search and the build site centre on
// it, not on the source coordinate. A built destination frame copies
// `src`'s axis and interior size (null → the default 2x3 x-plate).
static void portal_travel( RepGameState &state, const DimensionId dst, const PortalFrame *src, const bool build, const int kind_fallback ) {
    int axis = 0, w = 2, h = 3;
    glm::ivec3 src_center( static_cast<int>( floor( state.camera.pos.x ) ), 0,
                           static_cast<int>( floor( state.camera.pos.z ) ) );
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
        src_center = glm::ivec3( ( src->lo.x + src->hi.x ) / 2, ( src->lo.y + src->hi.y ) / 2, ( src->lo.z + src->hi.z ) / 2 );
    }
    const DimensionId src_dim = dimension_of_pos( state.camera.pos );
    const glm::ivec3 dst_center( portal_scale_coord( src_center.x, src_dim, dst ), src_center.y,
                                 portal_scale_coord( src_center.z, src_dim, dst ) );
    glm::ivec3 center;
    int embedded, floating;
    portal_find_spot( dst, dst_center.x, dst_center.z, axis, w, h, center, embedded, nullptr, &floating );
    const double mid = w / 2.0;
    const glm::dvec3 feet = axis ? glm::dvec3( center.x + 0.5, center.y + 1.0, center.z + mid )
                                 : glm::dvec3( center.x + mid, center.y + 1.0, center.z + 0.5 );
    // Travel whoosh on the one-time hop only — NOT inside
    // portal_teleport_feet, which the pending freeze calls every tick
    // while destination chunks stream in (that stacked the clip).
    audio_play_sfx_gain( "portal_travel", 0.2f );
    portal_teleport_feet( state, feet );
    PortalState &p = state.portal;
    p.cooldown = PORTAL_COOLDOWN_TICKS;
    p.must_exit = 1;
    p.pending_build = 1;
    p.pending_no_build = build ? 0 : 1;
    p.pending_carve = embedded;
    p.pending_axis = axis;
    p.pending_w = w;
    p.pending_h = h;
    p.pending_dim = dst;
    p.pending_kind = src ? src->kind : kind_fallback;
    p.hold_feet = feet;
    p.dst_center = dst_center;
    p.pending_start_tick = state.tick_number;
    p.pending_diff_snap = 0;
    p.pending_wait_logged = 0;
    p.pending_scan_tick = -1;
    p.pending_live_seen = -1;
    p.pending_adopted = 0;
}

// The readiness gate for resolving a pending destination portal. False
// while (1) the loader window hasn't shifted to the hold chunk, (2) a
// search-box chunk inside the active window isn't CHUNK_LIVE yet, or
// (3) MP hasn't applied every diff box sent so far. `unmet` records the
// first failing step for the timeout log line.
bool portal_destination_ready( RepGameState &state, int &unmet ) {
    PortalState &p = state.portal;
    ChunkLoader &cl = state.world.chunkLoader;
    const glm::ivec3 hold_chunk = glm::ivec3( glm::floor( p.hold_feet / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
    // Right after the teleport chunk_center is still the old dimension's —
    // without this check every destination chunk would be "outside the
    // window" and the rest of the gate would pass vacuously.
    if ( cl.get_chunk_center( ) != hold_chunk ) {
        unmet = 1;
        return false;
    }
    int y_min, y_max;
    portal_band_span( static_cast<DimensionId>( p.pending_dim ), y_min, y_max );
    const glm::ivec3 cs( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const int sr = portal_search_radius( static_cast<DimensionId>( p.pending_dim ) );
    const glm::ivec3 cmin = glm::floor( glm::vec3( p.dst_center.x - sr, y_min, p.dst_center.z - sr ) / glm::vec3( cs ) );
    const glm::ivec3 cmax = glm::floor( glm::vec3( p.dst_center.x + sr, y_max, p.dst_center.z + sr ) / glm::vec3( cs ) );
    for ( int cy = cmin.y; cy <= cmax.y; cy++ ) {
        // The repheim span wraps the whole torus — skip other bands' chunks
        // (harmless one-liner for the contiguous bands too).
        if ( dimension_of_chunk( cy ) != static_cast<DimensionId>( p.pending_dim ) ) {
            continue;
        }
        for ( int cx = cmin.x; cx <= cmax.x; cx++ ) {
            for ( int cz = cmin.z; cz <= cmax.z; cz++ ) {
                const glm::ivec3 cp( cx, cy, cz );
                if ( !cl.in_active_window( cp ) ) {
                    continue; // outside the window: won't load at this render distance
                }
                const Chunk *chunk = cl.get_chunk( cp );
                if ( chunk == nullptr || !chunk->is_live( ) ) {
                    unmet = 2;
                    return false;
                }
            }
        }
    }
    if ( state.multiplayer.is_active( ) ) {
        // First ready pass: remember how many diff boxes were in flight;
        // resolve once the server has confirmed all of them (in-order TCP
        // serving means DONE for box N covers boxes 1..N). A reconnect
        // resets both counters to zero — if sent dropped below the target,
        // re-snapshot against the new epoch or the gate could never pass.
        if ( !p.pending_diff_snap || static_cast<int32_t>( state.multiplayer.diff_boxes_sent_count( ) - p.pending_diff_target ) < 0 ) {
            p.pending_diff_target = state.multiplayer.diff_boxes_sent_count( );
            p.pending_diff_snap = 1;
        }
        if ( static_cast<int32_t>( state.multiplayer.diff_boxes_done_count( ) - p.pending_diff_target ) < 0 ) {
            unmet = 3;
            return false;
        }
    }
    unmet = 0;
    return true;
}

// The resolve's existing-portal half: scan the destination band's box for a
// PORTAL cell and, on a hit, stand the player inside that plate and re-aim
// the hold at it so the freeze keeps them there. Shared by the ready path,
// the give-up path, and the pending wait's periodic scans — adopting a
// loaded portal is always safe (a PORTAL cell can only sit in confirmed
// chunks); only building needs the gate. `quiet_miss` skips the log line
// for the periodic scans, where a miss is the common case.
static bool portal_try_adopt( RepGameState &state, const bool quiet_miss ) {
    PortalState &p = state.portal;
    int y_min, y_max;
    portal_band_span( static_cast<DimensionId>( p.pending_dim ), y_min, y_max );
    const glm::ivec3 search_center( p.dst_center.x, static_cast<int>( floor( p.hold_feet.y ) ), p.dst_center.z );
    const auto t0 = std::chrono::steady_clock::now( );
    glm::ivec3 found;
    const bool have_portal = portal_find_nearest( state.world, static_cast<DimensionId>( p.pending_dim ), search_center, y_min, y_max, found,
                                                  PORTAL_KINDS[p.pending_kind].interior );
    const long long search_us = std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now( ) - t0 ).count( );
    if ( have_portal || !quiet_miss ) {
        pr_debug( "portal search: %lld us (%s)", search_us, have_portal ? "found" : "missed" );
    }
    if ( !have_portal ) {
        return false;
    }
    PortalFrame frame;
    const glm::dvec3 feet = portal_find_frame( state.world, found, frame )
                                ? portal_arrive_feet_frame( frame )
                                : portal_arrive_feet_inside( state, found, PORTAL_KINDS[p.pending_kind].interior );
    p.hold_feet = feet;
    portal_teleport_feet( state, feet );
    p.must_exit = 1;
    return true;
}

// Once the readiness gate passes: adopt the nearest existing PORTAL cell as
// the exit, else pick a build site from the LOADED world, carve the pocket
// (embedded drops) and build the frame there. A gate that never passes can
// still adopt a found portal at the give-up cap — but never builds: a build
// on unconfirmed chunks is what planted frames beside real portals.
static void portal_pending_tick( RepGameState &state ) {
    PortalState &p = state.portal;
    const glm::ivec3 hold_cell( glm::floor( p.hold_feet ) );

    if ( !p.pending_build ) {
        // The carve-only drop (portal_travel_to_dimension — /repheim) and
        // the End arrival pad: wait for the
        // drop cell, then carve the pocket around the frame site — the pad
        // is generated obsidian, nothing to carve there.
        if ( state.world.get_loaded_block( hold_cell ).id == LAST_BLOCK_ID ) {
            return;
        }
        if ( p.pending_platform ) {
            p.pending_platform = 0;
            return;
        }
        const int axis = p.pending_axis;
        const int w = p.pending_w, h = p.pending_h;
        p.pending_carve = 0;
        glm::ivec3 frame( hold_cell.x, hold_cell.y - 1, hold_cell.z );
        ( axis ? frame.z : frame.x ) = static_cast<int>( floor( ( axis ? p.hold_feet.z : p.hold_feet.x ) - w / 2.0 + 0.5 ) );
        portal_carve_pocket( state, frame, axis, w, h );
        return;
    }

    // Freeze: re-snap to the hold feet so physics/walking can't drift the
    // drop while chunks stream in (runs right after process_movement).
    portal_teleport_feet( state, p.hold_feet );

    int unmet = 0;
    const bool ready = portal_destination_ready( state, unmet );
    const long elapsed = state.tick_number - p.pending_start_tick;
    if ( !ready ) {
        static const char *why[] = { "none", "chunk_center not at hold", "search-box chunk not live", "diff boxes pending" };
        const char *why_s = why[ unmet >= 0 && unmet <= 3 ? unmet : 0 ];
        if ( elapsed < PORTAL_RESOLVE_GIVEUP_TICKS ) {
            // Early adopt: as soon as the loaded chunks reveal a PORTAL
            // cell, move the hold inside it — the release then happens in
            // place instead of snapping hold→portal at resolve. Scan every
            // PORTAL_ADOPT_SCAN_TICKS and (until first adopted) whenever
            // new chunks went live since the last scan — a chunk only
            // goes live once its diffs are in — throttled to once per 4
            // ticks.
            const long live_now = state.world.get_live_finalize_count( );
            if ( elapsed - p.pending_scan_tick >= 4 &&
                 ( elapsed % PORTAL_ADOPT_SCAN_TICKS == 0 ||
                   ( p.pending_adopted == 0 && live_now != p.pending_live_seen ) ) ) {
                p.pending_scan_tick = elapsed;
                p.pending_live_seen = live_now;
                if ( portal_try_adopt( state, true ) && !p.pending_adopted ) {
                    p.pending_adopted = 1;
                    pr_debug( "early adopt at elapsed %d (unmet: %s)", static_cast<int>( elapsed ), why_s );
                }
            }
            if ( elapsed >= PORTAL_RESOLVE_TIMEOUT_TICKS && !p.pending_wait_logged ) {
                p.pending_wait_logged = 1;
                pr_debug( "portal resolve still waiting after %d ticks (unmet: %s)", PORTAL_RESOLVE_TIMEOUT_TICKS, why_s );
            }
            return;
        }
        pr_debug( "portal resolve gave up after %d ticks (unmet: %s): not building", PORTAL_RESOLVE_GIVEUP_TICKS, why_s );
        // Adopt a loaded portal if the search finds one, else leave the
        // player frozen at the hold — anything already visible is safe to
        // enter, but no new frame may be written off unconfirmed chunks.
        p.pending_build = 0;
        p.pending_carve = 0;
        if ( !portal_try_adopt( state, false ) ) {
            p.must_exit = 1;
        }
        return;
    }

    const int want_carve = p.pending_carve;
    const int axis = p.pending_axis;
    const int w = p.pending_w, h = p.pending_h;
    const DimensionId dst = static_cast<DimensionId>( p.pending_dim );
    p.pending_build = 0;
    p.pending_carve = 0;

    if ( portal_try_adopt( state, false ) ) {
        return;
    }

    // Build: pick the site from the loaded world — spiral from the scaled
    // destination anchor, clearance-vetoed against existing frames/bedrock.
    glm::ivec3 site;
    int embedded, floating;
    portal_find_spot( dst, p.dst_center.x, p.dst_center.z, axis, w, h, site, embedded, &state.world, &floating );
    if ( want_carve || embedded ) {
        portal_carve_pocket( state, site, axis, w, h );
    }
    if ( floating ) {
        // Over open lava/water: the standing ring the player lands on —
        // laid for /nether-style landings too (they stand on the slab).
        portal_build_platform( state, site, axis, w, p.pending_kind );
    }
    if ( p.pending_no_build ) {
        // /nether-style command travel: same site + pocket, but no frame is
        // written — the player lands on the site's floor.
        const double mid = w / 2.0;
        portal_teleport_feet( state, axis ? glm::dvec3( site.x + 0.5, site.y + 1.0, site.z + mid )
                                          : glm::dvec3( site.x + mid, site.y + 1.0, site.z + 0.5 ) );
        p.must_exit = 1;
        return;
    }
    portal_build_frame( state, site, axis, w, h, p.pending_kind );
    // Stand inside the new plate: bottom interior row, centred on the axis.
    PortalFrame built;
    built.lo = site + glm::ivec3( 0, 1, 0 );
    built.hi = built.lo + ( axis ? glm::ivec3( 0, h - 1, w - 1 ) : glm::ivec3( w - 1, h - 1, 0 ) );
    built.axis = axis;
    portal_teleport_feet( state, portal_arrive_feet_frame( built ) );
    // The player stands inside the new plate — without this a must_exit
    // lost to slow chunk loads would bounce them back.
    p.must_exit = 1;
}

// End-portal entry: every end portal in the world leads to the one
// obsidian pad beside the exit fountain — no x/z mapping (the source
// x/z could be over kilometres of void), no destination build. The pad
// wait just holds the player until the pad's chunk is live. Also /end.
bool portal_travel_end( RepGameState &state ) {
    if ( dimension_of_pos( state.camera.pos ) == DIM_END ) {
        return false;
    }
    const glm::dvec3 feet( ( MGE_PAD_X0 + MGE_PAD_X1 + 1 ) / 2.0, DIM_BY_MIN_E + MGE_PAD_Y + 1.0,
                           ( MGE_PAD_Z0 + MGE_PAD_Z1 + 1 ) / 2.0 );
    audio_play_sfx_gain( "portal_travel", 0.2f );
    portal_teleport_feet( state, feet );
    PortalState &p = state.portal;
    p.cooldown = PORTAL_COOLDOWN_TICKS;
    p.must_exit = 1;
    p.pending_platform = 1;
    p.pending_dim = DIM_END;
    p.hold_feet = feet;
    return true;
}

// Leaving the End (fountain pool or /overworld): no x/z mapping — the
// player goes back to their respawn point (bed, else the world-spawn
// column) via the spawn_pending ground snap, vitals untouched.
bool portal_return_from_end( RepGameState &state ) {
    if ( dimension_of_pos( state.camera.pos ) != DIM_END ) {
        return false;
    }
    audio_play_sfx_gain( "portal_travel", 0.2f );
    PortalState &p = state.portal;
    p.pending_build = 0;
    p.pending_carve = 0;
    p.pending_platform = 0;
    p.pending_no_build = 0;
    p.cooldown = PORTAL_COOLDOWN_TICKS;
    p.must_exit = 1;
    repgame_goto_respawn_point( state );
    return true;
}

void repgame_portal_tick( RepGameState &state ) {
    PortalState &p = state.portal;
    if ( p.pending_build || p.pending_carve || p.pending_platform ) {
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
    const int in_portal = feet_id == PORTAL || body_id == PORTAL;
    const int in_rep_portal = feet_id == REPHEIM_PORTAL || body_id == REPHEIM_PORTAL;
    const int in_end_portal = feet_id == END_PORTAL || body_id == END_PORTAL;
    if ( !in_portal && !in_rep_portal && !in_end_portal ) {
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
    // First tick inside: the dwell shimmer — quiet, the clip is a shrill
    // one at full gain. Retriggered only on re-entry — must_exit/cooldown
    // exits leave ticks_inside at 0 without replaying.
    if ( p.ticks_inside == 0 ) {
        audio_play_sfx_gain( "portal_trigger", 0.15f );
    }
    if ( ++p.ticks_inside >= PORTAL_TRAVEL_TICKS ) {
        p.ticks_inside = 0;
        const DimensionId cur = dimension_of_pos( feet_cell );
        if ( in_end_portal ) {
            // The End's one-way hop: anywhere -> the island's arrival pad
            // (next to the fountain, which doubles as the way out); in the
            // End -> back to the player's respawn point.
            if ( cur == DIM_END ) {
                portal_return_from_end( state );
            } else {
                portal_travel_end( state );
            }
            return;
        }
        if ( in_rep_portal ) {
            // The repgate pairs the overworld and repheim bands 1:1 — in
            // other bands a gold frame does nothing (same as a nether
            // portal outside its pair).
            const DimensionId dst = cur == DIM_OVERWORLD ? DIM_REPHEIM : cur == DIM_REPHEIM ? DIM_OVERWORLD : cur;
            if ( dst != cur ) {
                PortalFrame src;
                const glm::ivec3 seed = feet_id == REPHEIM_PORTAL ? feet_cell : body_cell;
                portal_travel( state, dst, portal_find_frame( state.world, seed, src ) ? &src : nullptr, true, PORTAL_KIND_REPHEIM );
            }
            return;
        }
        const DimensionId dst = cur == DIM_OVERWORLD ? DIM_NETHER : cur == DIM_NETHER ? DIM_OVERWORLD : cur;
        if ( dst != cur ) {
            // The destination frame copies this portal's axis and interior
            // size — detect the ring around whichever cell is lit.
            PortalFrame src;
            const glm::ivec3 seed = feet_id == PORTAL ? feet_cell : body_cell;
            portal_travel( state, dst, portal_find_frame( state.world, seed, src ) ? &src : nullptr, true, PORTAL_KIND_NETHER );
        }
    }
}

// Portal-less drop on generated ground in `dim` (same x/z) — the
// /repheim path; embedded landings still get their pocket carved (no
// frame).
bool portal_travel_to_dimension( RepGameState &state, const DimensionId dim ) {
    if ( dim != DIM_OVERWORLD && dim != DIM_NETHER && dim != DIM_END && dim != DIM_REPHEIM ) {
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
    p.pending_build = 0;
    p.pending_carve = embedded;
    p.pending_axis = 0;
    p.pending_w = 2;
    p.pending_h = 3;
    p.pending_dim = dim;
    p.hold_feet = feet;
    p.pending_no_build = 0;
    return true;
}

// /nether and /overworld: the portal trip itself minus the destination
// frame — the same 8:1-scaled anchor, hold-and-resolve freeze and
// existing-portal adoption, but the resolve lands the player on the site
// instead of building one. False for spare bands or when already in `dim`.
bool portal_travel_no_build( RepGameState &state, const DimensionId dim ) {
    if ( dim != DIM_OVERWORLD && dim != DIM_NETHER ) {
        return false;
    }
    if ( dimension_of_pos( state.camera.pos ) == dim ) {
        return false;
    }
    portal_travel( state, dim, nullptr, false, PORTAL_KIND_NETHER );
    return true;
}
