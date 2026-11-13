#pragma once

// Rail shapes, state encoding and connection logic for RAIL/GOLDEN_RAIL.
// Pure functions over World::get_loaded_block so the unit tests can drive
// them without GL. Used by the block-update path (perform_checks merging,
// golden-rail power), the mesher (rail_render_yaw + BLOCK_COORDS_SLOPE) and
// the minecart physics (endpoint/direction helpers).
//
// Direction convention everywhere below: the four horizontal directions
// index the usual world yaw order — 0 = N(-z), 1 = E(+x), 2 = S(+z),
// 3 = W(-x) — matching getPlacedRotation's buckets and FACE_ROTATE_90.
//
// Encoding: rotation is 3 bits but there are 10 shapes, so the four curves
// borrow display_id the way redstone dust does. Shapes 0-5 (straight +
// ascending) store rotation = shape and display_id = id; curves 6-9 (plain
// RAIL only — golden rails never curve) keep id = RAIL and store
// display_id = RAIL_CORNER with a curve rotation (RAIL_SE..RAIL_NE store
// rotations 3,0,1,2 — a frozen save/wire encoding; the mesher's curve yaw
// comes from rail_render_yaw, not from this rotation).
// RAIL_CORNER is a hidden render-only block whose only difference is the
// turned top tile.

#include <cstdint>

#include <glm/glm.hpp>

#include "common/block_types.hpp"

class World;

typedef enum RailShape : uint8_t {
    RAIL_NS,    // straight north-south
    RAIL_EW,    // straight east-west
    RAIL_ASC_E, // rises toward +x
    RAIL_ASC_W, // rises toward -x
    RAIL_ASC_N, // rises toward -z
    RAIL_ASC_S, // rises toward +z
    RAIL_SE,    // curve connecting +z and +x
    RAIL_SW,    // curve connecting +z and -x
    RAIL_NW,    // curve connecting -z and -x
    RAIL_NE,    // curve connecting -z and +x
    RAIL_SHAPE_COUNT
} RailShape;

// Decode the shape stored in a rail cell's BlockState (id/display_id/
// rotation — see the header comment). Callers must only pass rail states.
RailShape rail_shape_of( const BlockState &state );

// Encode `shape` into `state` (rotation + display_id per the encoding
// above); id, power and waterlogged pass through untouched.
BlockState rail_state_with_shape( BlockState state, RailShape shape );

// The track's two exit directions as horizontal unit vectors, plus the
// rise at each end (1 = the track climbs one block in that direction — the
// connected cell is at pos+dirs[i]+up). Used by both the connection logic
// and the minecart physics, which steps cell-to-cell along these.
void rail_endpoints( RailShape shape, glm::ivec3 dirs[ 2 ], int dy[ 2 ] );

// face_shift yaw for the mesher. For the four curves it is a measured
// table (the turned tile draws N+W at yaw 0, NE/SE/SW at 1/2/3); for straights it turns the straight tile onto the
// track axis; for ascending shapes it encodes the uphill direction
// (0=-z, 1=+x, 2=+z, 3=-x), which is also what BLOCK_COORDS_SLOPE reads.
unsigned char rail_render_yaw( RailShape shape );

bool rail_is_ascending( RailShape shape );

// Pick-box height in 1/16 px: ascending rails select on an 8px box like
// MC; flat and curved rails keep the def's 1px plate. Only meaningful for
// is_rail cells — reads the cell's real state (rotation/display_id).
int rail_pick_height_px( const BlockState &state );

// The shape a rail cell should take given its neighbours: recompute the
// four direction links (a rail beside us, one up that we ascend toward, or
// one below ascending toward us — the neighbour must also accept), keep
// the current shape while both its endpoints still link (sticky — keeps
// T-junctions stable), else pick a straight pair, a curve pair (plain RAIL
// only, in MC order SE, SW, NW, NE), a single-link straight, or keep the
// current axis. A straight with exactly one uphill link becomes ASC_<dir>.
// placer_yaw is the horizontal camera angle in degrees (0..360, -1 = none)
// and only decides the axis of a freshly placed rail with no links.
// Returns the state unchanged when any probed neighbour is unloaded
// (LAST_BLOCK_ID) — like dust's "Not ready", resolving at chunk edges
// would guess wrong and then stick.
BlockState rail_resolve_shape( World &world, const glm::ivec3 &pos, const BlockState &state, int placer_yaw );

// Whether any directly-activated golden rail is reachable from `pos`
// through at most 8 connected hops (BFS along shape endpoints, slope rises
// included). Reads only direct activation (mechanism_direct_power), never
// stored rail power — a loop of powered rails must not hold itself on.
bool golden_rail_chain_powered( World &world, const glm::ivec3 &pos );
