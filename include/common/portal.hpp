#pragma once

// ---------------------------------------------------------------------------
// portal.hpp — nether portal frames, ignition, survival and travel.
//
// A portal is an obsidian ring (min interior 1x2, max 16x25) filled with
// PORTAL blocks. Flint & steel (or a fire spread) on an interior cell ignites
// it; standing inside for PORTAL_TRAVEL_TICKS moves the player between the
// overworld and nether dimension bands (dimension.hpp). Travel keeps x/z —
// no 8:1 compression — and finds or builds a destination portal.
//
// portal.cpp holds the pure world helpers (frame detection, the survive rule
// block_can_survive_at calls, frame building); portal_travel.cpp the per-tick
// player state. Both run client-side: frame/portal cells go through
// PlayerBlockPlacedEvent so multiplayer syncs them as ordinary edits.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>

struct RepGameState;
class World;
#include "common/block_types.hpp"
#include "common/dimension.hpp"

// Interior size limits (frame ring is +2 in each axis).
#define PORTAL_MAX_INTERIOR_W 16
#define PORTAL_MAX_INTERIOR_H 25
// Ticks standing in a PORTAL cell before travelling (~4 s at UPS_RATE).
#define PORTAL_TRAVEL_TICKS 80
// Ticks after a travel where portal contact doesn't count — keeps a player
// who lands inside the destination portal from bouncing straight back.
#define PORTAL_COOLDOWN_TICKS 60

// A detected empty obsidian ring ready for ignition.
struct PortalFrame {
    glm::ivec3 lo; // interior min corner (inclusive)
    glm::ivec3 hi; // interior max corner (inclusive)
    int axis;      // 0 = interior spans x (plate thin in z), 1 = spans z
};

// Per-player portal state on RepGameState.
struct PortalState {
    int ticks_inside = 0;   // consecutive ticks standing in a PORTAL cell
    int cooldown = 0;       // ticks before a portal can trigger again
    int must_exit = 0;      // just arrived: no travel until the player steps out
    int pending_build = 0;  // destination portal still needs finding/building
    int pending_carve = 0;  // embedded drop: carve the pocket around the site
    int pending_axis = 0;   // frame to build: source portal's axis (PortalFrame::axis)
    int pending_w = 2;      // frame to build: interior width along the axis
    int pending_h = 3;      // frame to build: interior height
    glm::dvec3 pending_pos; // where the player was dropped (frame search centre)
};

// Detect a complete empty obsidian ring containing `seed` (an interior-able
// cell: AIR/FIRE/SOUL_FIRE/PORTAL). Tries both plane axes; fills `frame`.
bool portal_find_frame( World &world, const glm::ivec3 &seed, PortalFrame &frame );

// Ignite: validate the ring at `seed` and fill the interior with PORTAL
// blocks (each a PlayerBlockPlacedEvent, so it syncs in multiplayer).
// Returns false when no valid frame contains the cell.
bool portal_ignite( RepGameState &state, const glm::ivec3 &seed );

// The block_can_survive_at rule for PORTAL: the cell must sit inside a
// complete ring — scanning along its plane axis and y from the cell reaches
// obsidian on both sides through only PORTAL cells. Breaking any frame block
// pops the adjacent cell, which cascades through the interior.
bool portal_cell_survives( World &world, const glm::ivec3 &pos, const BlockState &block_state );

// Build a lit (w+2)x(h+2) obsidian ring (interior w×h) with its bottom row
// at `center.y` and the interior spanning `axis` over `center` — the
// destination half of travel when no portal is found. Callers pass the
// source frame's axis and interior size so both ends look the same.
void portal_build_frame( RepGameState &state, const glm::ivec3 &center, int axis, int w, int h );

// Clear an air pocket around a destination frame site (same `center` and
// w/h as portal_build_frame): a room over the frame's open faces so a
// portal embedded in solid rock isn't an immediate entombment. Solid and
// fluid cells become AIR; call BEFORE portal_build_frame.
void portal_carve_pocket( RepGameState &state, const glm::ivec3 &center, int axis, int w, int h );

// Nearest loaded PORTAL cell to `center` inside a (rx, ry) box — the
// destination-portal search half of travel.
bool portal_find_nearest( World &world, const glm::ivec3 &center, int rx, int ry, glm::ivec3 &out );

// Per-tick portal contact/travel driver — called from the game tick right
// after vitals. Counts dwell time inside a PORTAL cell, runs the travel, and
// materializes a pending destination frame once its chunks load.
void repgame_portal_tick( RepGameState &state );

// Teleport the player into `dim` at the same x/z on generated ground (the
// /dimension command path — no portal built). False when the destination is
// unreachable (spare bands, already there).
bool portal_travel_to_dimension( RepGameState &state, DimensionId dim );
