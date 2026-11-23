// The per-tick portal side: dwell counting inside a PORTAL cell, dimension
// travel, and the deferred destination-portal materialization. portal.cpp
// holds the world helpers; this file owns PortalState.

#include <chrono>

#include "common/portal.hpp"
#include "common/audio.hpp"
#include "common/game/game_state.hpp"
#include "common/world.hpp"
#include "common/block_definitions.hpp"
#include "common/map_gen.hpp" // map_gen_consts.hpp: MGN_LAVA_SEA ledge fallback
#include "common/multiplayer.hpp"
#include "common/dimension_profile.hpp"
#include "common/harness/debug_commands.hpp" // dbgcmd::goto_
#include "common/Logging.hpp"

// Spiral radius for the generated-ground column scan.
#define PORTAL_SCAN_RADIUS 24

// The block source for the site scan: nullptr reads untouched generated
// terrain (MapGen::gen_block_id — the hold position and /dimension picks);
// a World reads the loaded chunks (the resolve-time build-site pick). A
// LAST_BLOCK_ID cell (unloaded) skips that scan row, never poisons the
// column — the loaded window is narrower than the band's scan range.
static BlockID portal_site_block( World *world, const int x, const int y, const int z ) {
    return world ? world->get_loaded_block( glm::ivec3( x, y, z ) ).id : MapGen::gen_block_id( x, y, z );
}

// The band's landing-scan y range plus the open-void ledge height — inside
// the nether cavern (below the roof base, over the lava sea) or the
// overworld's terrain band (mountain cap to lava layer).
static void portal_band_bounds( const DimensionId dim, int &y_top, int &y_bottom, int &ledge_y ) {
    if ( dim == DIM_NETHER ) {
        // Scan the open cavern: from just over the lava sea up to a hair
        // under the lowest possible roof base. The ledge sits on the sea.
        y_top = DIM_BY_MIN_N + static_cast<int>( MGN_ROOF_MID - 20.0f );
        y_bottom = DIM_BY_MIN_N + MGN_LAVA_SEA + 2;
        ledge_y = DIM_BY_MIN_N + MGN_LAVA_SEA + 1;
    } else {
        y_top = 300;
        y_bottom = -90;
        ledge_y = WATER_LEVEL + 2;
    }
}

// The full block-y span of a dimension band — the existing-portal search
// box's y extent (a shelf far above/below the drop can hold the exit).
static void portal_band_span( const DimensionId dim, int &y_min, int &y_max ) {
    if ( dim == DIM_NETHER ) {
        y_min = DIM_BY_MIN_N;
        y_max = DIM_BY_MAX_N;
    } else {
        y_min = DIM_BY_MIN_O;
        y_max = DIM_BY_MAX_O;
    }
}

// Loaded-world-only site veto: the frame's clearance volume — u in
// [-2, w+1] along the axis, v in [-3, 3] normal to it, y in [0, h+2] above
// the ring's bottom row — must hold no PORTAL, OBSIDIAN or BEDROCK. Keeps a
// fresh build from overlapping an existing frame (lit or not) or
// unbreakable ground.
static bool portal_clearance_ok( World &world, const glm::ivec3 &origin, const int axis, const int w, const int h ) {
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    const glm::ivec3 dn = axis ? glm::ivec3( 1, 0, 0 ) : glm::ivec3( 0, 0, 1 );
    for ( int u = -2; u <= w + 1; u++ ) {
        for ( int v = -3; v <= 3; v++ ) {
            for ( int y = 0; y <= h + 2; y++ ) {
                const BlockID id = world.get_loaded_block( origin + du * u + dn * v + glm::ivec3( 0, y, 0 ) ).id;
                if ( id == PORTAL || id == OBSIDIAN || id == BEDROCK ) {
                    return false;
                }
            }
        }
    }
    return true;
}

// A column at (x,z) is a portal site when the w+2 ring-floor cells at y
// collide and the w×(h+1) interior+lintel cavity above them is clear of
// solids and lava. `y` is the floor cell — the ring's bottom obsidian row
// replaces it. The footprint runs along `du` (the frame axis).
static bool portal_column_try( const DimensionId dim, const int x, const int z, const glm::ivec3 &du, const int w, const int h, int &floor_y, World *world ) {
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    for ( int y = y_top; y >= y_bottom; y-- ) {
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
                cavity_ok = column_loaded && id != LAVA && !block_definition_get_definition( id )->collides_with_player;
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

// The pass-B fallback: bottom-up, the first solid cell in the band is the
// cavern floor — no cavity required, the frame embeds and a pocket is
// carved around it on arrival.
static bool portal_column_surface( const DimensionId dim, const int x, const int z, int &floor_y, World *world ) {
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    for ( int y = y_bottom; y <= y_top; y++ ) {
        const BlockID id = portal_site_block( world, x, y, z );
        if ( id == LAST_BLOCK_ID ) {
            continue; // skip unknown rows — same rule as portal_column_try
        }
        if ( !block_definition_get_definition( id )->collides_with_player ) {
            continue;
        }
        // A solid cell sitting on an unloaded one is the window's bottom
        // edge, not a real floor — demand the cell below is known too.
        if ( world != nullptr && portal_site_block( world, x, y - 1, z ) == LAST_BLOCK_ID ) {
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

// Find the frame origin (ring corner: interior spans u in [0,w), floor row
// at center.y) near (bx,bz) in `dim`, for a frame spanning `axis` with a
// w×h interior. Spirals outward column by column.
// `embedded` reports a pass-B/ledge site — the arrival carve digs the
// pocket. Never fails: the last resort is an obsidian ledge over the lava
// sea (the overworld fallback is unreachable — every column has bedrock).
// `world` != nullptr reads the loaded chunks (LAST_BLOCK_ID rows are
// skipped as unknown) and vetoes each candidate on portal_clearance_ok; nullptr reads
// generated terrain (hold position, /dimension — no clearance check, the
// generated world never holds portals).
static bool portal_find_spot( const DimensionId dim, const int bx, const int bz, const int axis, const int w, const int h, glm::ivec3 &out, int &embedded, World *world = nullptr ) {
    const glm::ivec3 du = axis ? glm::ivec3( 0, 0, 1 ) : glm::ivec3( 1, 0, 0 );
    embedded = 0;
    int floor_y;
    for ( int r = 0; r <= PORTAL_SCAN_RADIUS; r++ ) {
        for ( int dx = -r; dx <= r; dx++ ) {
            for ( int dz = -r; dz <= r; dz++ ) {
                if ( r != 0 && abs( dx ) != r && abs( dz ) != r ) {
                    continue;
                }
                if ( portal_column_try( dim, bx + dx, bz + dz, du, w, h, floor_y, world ) ) {
                    const glm::ivec3 origin( bx + dx, floor_y, bz + dz );
                    if ( world == nullptr || portal_clearance_ok( *world, origin, axis, w, h ) ) {
                        out = origin;
                        return true;
                    }
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
                if ( portal_column_surface( dim, bx + dx, bz + dz, floor_y, world ) ) {
                    const glm::ivec3 origin( bx + dx, floor_y, bz + dz );
                    if ( world == nullptr || portal_clearance_ok( *world, origin, axis, w, h ) ) {
                        embedded = 1;
                        out = origin;
                        return true;
                    }
                }
            }
        }
    }
    int y_top, y_bottom, ledge_y;
    portal_band_bounds( dim, y_top, y_bottom, ledge_y );
    // The lava-sea ledge, walked along the same spiral columns until one
    // passes the clearance check; when none does the ledge is used anyway
    // (still better than failing the travel outright).
    if ( world != nullptr ) {
        for ( int r = 0; r <= PORTAL_SCAN_RADIUS * 2; r++ ) {
            for ( int dx = -r; dx <= r; dx++ ) {
                for ( int dz = -r; dz <= r; dz++ ) {
                    if ( r != 0 && abs( dx ) != r && abs( dz ) != r ) {
                        continue;
                    }
                    const glm::ivec3 origin( bx + dx, ledge_y, bz + dz );
                    if ( portal_clearance_ok( *world, origin, axis, w, h ) ) {
                        embedded = 1;
                        out = origin;
                        return true;
                    }
                }
            }
        }
        pr_debug( "portal build: no cleared ledge site, using the centre column anyway" );
    }
    embedded = 1;
    out = glm::ivec3( bx, ledge_y, bz );
    return true;
}

// Feet at the bottom of an existing portal's plate: `found` is a PORTAL
// cell, so stand on the sill at the bottom of its interior column.
static glm::dvec3 portal_arrive_feet_inside( RepGameState &state, const glm::ivec3 &found ) {
    glm::ivec3 base = found;
    while ( state.world.get_loaded_block( glm::ivec3( base.x, base.y - 1, base.z ) ).id == PORTAL ) {
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
// overworld-bound multiplies — 1 nether block is 8 overworld blocks.
static int portal_scale_coord( const int v, const DimensionId dst ) {
    return dst == DIM_NETHER ? static_cast<int>( floor( v / static_cast<double>( PORTAL_COORD_SCALE ) ) )
                             : v * PORTAL_COORD_SCALE;
}

// Travel: drop the player at generated ground in the destination band and
// freeze them on hold_feet until portal_pending_tick's readiness gate says
// the destination chunks (and, in MP, the server's edit diffs) are in —
// only then does it search for an existing portal or build one. The
// destination anchor is the source portal's x/z scaled by the 8:1 nether
// ratio (portal_scale_coord) — every search and the build site centre on
// it, not on the source coordinate. A built destination frame copies
// `src`'s axis and interior size (null → the default 2x3 x-plate).
static void portal_travel( RepGameState &state, const DimensionId dst, const PortalFrame *src, const bool build ) {
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
    const glm::ivec3 dst_center( portal_scale_coord( src_center.x, dst ), src_center.y,
                                 portal_scale_coord( src_center.z, dst ) );
    glm::ivec3 center;
    int embedded;
    portal_find_spot( dst, dst_center.x, dst_center.z, axis, w, h, center, embedded );
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
    const glm::ivec3 cmin = glm::floor( glm::vec3( p.dst_center.x - PORTAL_SEARCH_R, y_min, p.dst_center.z - PORTAL_SEARCH_R ) / glm::vec3( cs ) );
    const glm::ivec3 cmax = glm::floor( glm::vec3( p.dst_center.x + PORTAL_SEARCH_R, y_max, p.dst_center.z + PORTAL_SEARCH_R ) / glm::vec3( cs ) );
    for ( int cy = cmin.y; cy <= cmax.y; cy++ ) {
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
    const bool have_portal = portal_find_nearest( state.world, search_center, y_min, y_max, found );
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
                                : portal_arrive_feet_inside( state, found );
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
        // /dimension carve-only drop: keep the old behaviour — wait for the
        // drop cell, then carve the pocket around the frame site.
        if ( state.world.get_loaded_block( hold_cell ).id == LAST_BLOCK_ID ) {
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
    int embedded;
    portal_find_spot( dst, p.dst_center.x, p.dst_center.z, axis, w, h, site, embedded, &state.world );
    if ( want_carve || embedded ) {
        portal_carve_pocket( state, site, axis, w, h );
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
    portal_build_frame( state, site, axis, w, h );
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
    // First tick inside: the dwell shimmer — quiet, the clip is a shrill
    // one at full gain. Retriggered only on re-entry — must_exit/cooldown
    // exits leave ticks_inside at 0 without replaying.
    if ( p.ticks_inside == 0 ) {
        audio_play_sfx_gain( "portal_trigger", 0.15f );
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
            portal_travel( state, dst, portal_find_frame( state.world, seed, src ) ? &src : nullptr, true );
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
    portal_travel( state, dim, nullptr, false );
    return true;
}
