#pragma once

// ---------------------------------------------------------------------------
// portal.hpp — portal frames, ignition, survival and travel.
//
// A portal is a ring (min/max interior set by its PortalKind) filled with
// portal blocks: an obsidian ring holds PORTAL and bridges the overworld and
// nether bands; a gold-block ring holds REPHEIM_PORTAL and bridges the
// overworld and repheim bands (dimension.hpp). Flint & steel (or a fire
// spread) on an interior cell ignites it; standing inside for
// PORTAL_TRAVEL_TICKS moves the player. Nether travel maps x/z by the 8:1
// scale (1 nether block = 8 overworld blocks); repheim travel keeps x/z
// 1:1 (repheim is another overworld at the same coordinates). Travel finds
// or builds a destination portal.
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
#include "common/constants.hpp" // UPS_RATE

// Interior size limits (frame ring is +2 in each axis).
#define PORTAL_MAX_INTERIOR_W 16
#define PORTAL_MAX_INTERIOR_H 25
// Ticks standing in a PORTAL cell before travelling (~4 s at UPS_RATE).
#define PORTAL_TRAVEL_TICKS 80
// Ticks after a travel where portal contact doesn't count — keeps a player
// who lands inside the destination portal from bouncing straight back.
#define PORTAL_COOLDOWN_TICKS 60
// The 8:1 nether<->overworld horizontal scale: a source portal's x/z maps
// to the destination anchor by /8 (overworld -> nether) or *8
// (nether -> overworld).
#define PORTAL_COORD_SCALE 8
// Horizontal half-extent of the existing-portal search box around the
// SCALED destination anchor (not the drop point — the drop can land off
// the anchor and at a wildly different y), in overworld blocks. The
// nether box is scaled down by PORTAL_COORD_SCALE (portal_search_radius)
// so both directions cover the same ground: a ±64 nether box would link
// overworld portals up to 512 blocks apart to one nether portal.
#define PORTAL_SEARCH_R 64

// The existing-portal search half-extent for a destination band.
static inline int portal_search_radius( const DimensionId dim ) {
    return dim == DIM_NETHER ? PORTAL_SEARCH_R / PORTAL_COORD_SCALE : PORTAL_SEARCH_R;
}
// Soft timeout (~10 s at UPS_RATE): the readiness gate keeps the player
// frozen past this — it only logs that the wait is taking a while.
#define PORTAL_RESOLVE_TIMEOUT_TICKS ( 10 * UPS_RATE )
// Hard cap (~60 s): stop waiting entirely. The resolve may adopt an
// existing portal that's already loaded, but never builds a new frame —
// building on unconfirmed chunks is what planted frames beside existing
// portals.
#define PORTAL_RESOLVE_GIVEUP_TICKS ( 60 * UPS_RATE )
// How often the pending wait re-scans for a destination portal to adopt
// early (~0.5 s): once the loaded chunks reveal a PORTAL cell the hold
// moves inside that plate, so the resolve's release is a no-op teleport
// instead of a visible snap from the predicted site to the real portal.
#define PORTAL_ADOPT_SCAN_TICKS ( UPS_RATE / 2 )

// A portal type: which block forms the ring, which fills the interior, and
// the smallest interior the ring accepts (the repgate admits a 1x2 hole —
// vanilla's nether portal wants 2x3).
struct PortalKind {
    BlockID ring;     // frame material (OBSIDIAN / GOLD_BLOCK)
    BlockID interior; // lit interior cell (PORTAL / REPHEIM_PORTAL)
    int min_w;        // smallest interior width along the axis
    int min_h;        // smallest interior height
};

enum {
    PORTAL_KIND_NETHER = 0,
    PORTAL_KIND_REPHEIM = 1,
    PORTAL_KIND_COUNT = 2,
};
extern const PortalKind PORTAL_KINDS[PORTAL_KIND_COUNT];

// A detected empty ring ready for ignition.
struct PortalFrame {
    glm::ivec3 lo; // interior min corner (inclusive)
    glm::ivec3 hi; // interior max corner (inclusive)
    int axis;      // 0 = interior spans x (plate thin in z), 1 = spans z
    int kind;      // PORTAL_KIND_* — ring material + interior cell type
};

// Per-player portal state on RepGameState.
struct PortalState {
    int ticks_inside = 0;          // consecutive ticks standing in a PORTAL cell
    int cooldown = 0;              // ticks before a portal can trigger again
    int must_exit = 0;             // just arrived: no travel until the player steps out
    int pending_build = 0;         // destination portal still needs finding/building
    int pending_carve = 0;         // embedded drop: carve the pocket around the site
    int pending_axis = 0;          // frame to build: source portal's axis (PortalFrame::axis)
    int pending_w = 2;             // frame to build: interior width along the axis
    int pending_h = 3;             // frame to build: interior height
    int pending_dim = 0;           // destination DimensionId (site scan + search band)
    int pending_kind = PORTAL_KIND_NETHER; // portal type to find/build at the destination
    glm::dvec3 hold_feet;          // frozen drop position the player is re-snapped to —
                                   // moved inside the adopted plate when an early scan finds one
    glm::ivec3 dst_center;         // destination anchor: source interior centre x/z scaled by
                                   // PORTAL_COORD_SCALE (search/build origin x/z)
    long pending_start_tick = 0;   // tick_number when the travel began (timeout clock)
    uint32_t pending_diff_target = 0; // MP gate: diff_boxes_sent snapshot to wait for
    int pending_diff_snap = 0;     // MP gate: the snapshot above has been taken
    int pending_wait_logged = 0;   // soft-timeout "still waiting" line already logged
    long pending_scan_tick = -1;   // elapsed of the last early-adopt scan
    long pending_live_seen = -1;   // ChunkLoader::live_finalize_count at that scan —
                                   // newly live chunks retrigger it (throttled to 4 ticks)
    int pending_adopted = 0;       // an early scan already moved the hold into a plate —
                                   // live-triggered scans stop, periodic ones keep
                                   // looking for a nearer portal
    int pending_platform = 0;      // End arrival-pad wait: hold until the pad's
                                   // chunk is live, no portal to build or carve
    int pending_no_build = 0;      // /nether-style command travel (portal_travel_no_build):
                                   // resolve lands on the site, never writes a frame
};

// Detect a complete empty ring containing `seed` (an interior-able cell:
// AIR/FIRE/SOUL_FIRE/the kind's portal cell). Tries both plane axes and
// every PORTAL_KINDS entry; fills `frame` (frame.kind says which matched).
bool portal_find_frame( World &world, const glm::ivec3 &seed, PortalFrame &frame );

// Ignite: validate the ring at `seed` and fill the interior with the kind's
// portal blocks (each a PlayerBlockPlacedEvent, so it syncs in multiplayer).
// Returns false when no valid frame contains the cell.
bool portal_ignite( RepGameState &state, const glm::ivec3 &seed );

// The block_can_survive_at rule for PORTAL/REPHEIM_PORTAL (kind picked by
// block_state.id): the cell must sit inside a complete ring — scanning along
// its plane axis and y from the cell reaches the kind's ring block on both
// sides through only its portal cells. Breaking any frame block pops the
// adjacent cell, which cascades through the interior.
bool portal_cell_survives( World &world, const glm::ivec3 &pos, const BlockState &block_state );

// Build a lit (w+2)x(h+2) ring of `kind` (interior w×h) with its bottom row
// at `center.y` and the interior spanning `axis` over `center` — the
// destination half of travel when no portal is found. Callers pass the
// source frame's axis and interior size so both ends look the same.
void portal_build_frame( RepGameState &state, const glm::ivec3 &center, int axis, int w, int h, int kind );

// Clear an air pocket around a destination frame site (same `center` and
// w/h as portal_build_frame): a room over the frame's open faces so a
// portal embedded in solid rock isn't an immediate entombment. Solid and
// fluid cells become AIR; PORTAL, OBSIDIAN and BEDROCK are never
// overwritten (an existing portal or unbreakable ground survives a new
// frame landing next to it). Call BEFORE portal_build_frame.
void portal_carve_pocket( RepGameState &state, const glm::ivec3 &center, int axis, int w, int h );

// The standing ring for a frame floated over open lava/water (same
// `center`/w as portal_build_frame): a slab of the kind's ring material at
// center.y covering the ring's bottom row plus one cell on every side.
// Fills only air-like and fluid cells — solids and portal cells are kept.
void portal_build_platform( RepGameState &state, const glm::ivec3 &center, int axis, int w, int kind );

// Detect a complete horizontal 5x5 END_PORTAL_FRAME/BEDROCK ring whose 3x3
// interior holds `seed` (an interior-able cell: AIR/FIRE/SOUL_FIRE/
// END_PORTAL); the ring's corner cells are ignored. `lo` gets the
// interior's min x/z corner at seed.y.
bool end_portal_find_frame( World &world, const glm::ivec3 &seed, glm::ivec3 &lo );

// End ignition: validate the ring at `seed` and fill the 3x3 interior with
// END_PORTAL cells (each a PlayerBlockPlacedEvent, syncing in multiplayer).
// Returns false when no valid ring contains the cell.
bool end_portal_ignite( RepGameState &state, const glm::ivec3 &seed );

// Seat an eye of ender into the END_PORTAL_FRAME at `pos`: a display-state
// write flipping display_id to the hidden END_PORTAL_FRAME_EYE render
// variant (the cell id — and so picks, drops and ring detection — is
// unchanged). False when the cell isn't an eyeless frame.
bool end_portal_insert_eye( RepGameState &state, const glm::ivec3 &pos );

// Ignition half of eye insertion: scan every 5x5 window whose ring holds
// `pos` as a non-corner perimeter cell — twelve eye-filled frames (or
// bedrock) around an open interior fills the 3x3 with END_PORTAL. True
// when a ring completed.
bool end_portal_complete_ring( RepGameState &state, const glm::ivec3 &pos );

// The block_can_survive_at rule for END_PORTAL: bounded END_PORTAL runs
// along both horizontal axes must reach an END_PORTAL_FRAME or BEDROCK
// ring cell. Breaking a ring cell pops the pool.
bool end_portal_cell_survives( World &world, const glm::ivec3 &pos, const BlockState &block_state );

// Nearest `interior` portal cell to `center`'s x/z inside the box
// x/z +- portal_search_radius( dim ), y in [y_min, y_max] — the
// destination-portal search half of travel. Only CHUNK_LIVE chunks
// contribute (the pending-travel gate promises everything overlapping
// the box inside the active window is live). Ranking: horizontal
// distance^2 to center.x/z, tie-break |dy| to center.y.
bool portal_find_nearest( World &world, DimensionId dim, const glm::ivec3 &center, int y_min, int y_max, glm::ivec3 &out, BlockID interior );

// The pending-travel readiness gate (portal_travel.cpp): false while the
// loader window hasn't reached the hold chunk, a search-box chunk inside
// the active window isn't CHUNK_LIVE, or (multiplayer) a CHUNK_DIFF box
// sent so far hasn't been DONE-confirmed. `unmet` gets the failing step.
bool portal_destination_ready( RepGameState &state, int &unmet );

// Per-tick portal contact/travel driver — called from the game tick right
// after vitals. Counts dwell time inside a PORTAL cell, runs the travel, and
// materializes a pending destination frame once its chunks load.
void repgame_portal_tick( RepGameState &state );

// Teleport the player into `dim` at the same x/z on generated ground —
// the portal-less drop used by /repheim (no 8:1 scaling, no portal
// built). False when the destination is unreachable (spare bands,
// already there).
bool portal_travel_to_dimension( RepGameState &state, DimensionId dim );

// End-portal entry and /end: always the End's fixed obsidian arrival pad
// beside the exit fountain, held until the pad's chunk is live. False
// when already in the End.
bool portal_travel_end( RepGameState &state );

// Leaving the End (the fountain pool, or /overworld from the End): back
// to the player's respawn point — the recorded bed, else the world-spawn
// column — vitals untouched. False when not in the End.
bool portal_return_from_end( RepGameState &state );

// /nether and /overworld (chat_commands.cpp): portal-style travel —
// 8:1-scaled anchor, hold-and-resolve, existing-portal adoption — but the
// resolve never builds a destination frame; the player lands on generated
// ground (an embedded site still gets its pocket carved). False for spare
// bands or when already in `dim`.
bool portal_travel_no_build( RepGameState &state, DimensionId dim );
