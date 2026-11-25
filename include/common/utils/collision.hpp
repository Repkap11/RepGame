#pragma once

#include <vector>

// Kept light: RepGame.hpp pulls in world.hpp, which pulls in block_bodies.hpp,
// which pulls this header back in — a full include here would hide the
// BlockBodies definition from world.hpp.
#include <glm/glm.hpp>
#include "common/constants.hpp"
#include "common/block_ids_gen.hpp"

class World;
struct RepGameState;
struct BlockState;
struct BlockEmitNeighbors;

// An extra AABB obstacle move_box collides against on top of the voxel
// world — the other live BlockBodies. index is opaque to collision code;
// move_box reports it back through hit_extra_axis so the caller can exchange
// momentum with the body that was actually hit.
struct BodyBox {
    glm::dvec3 min;
    glm::dvec3 max;
    int index;
};

// A dynamic collision box with the translation it made this tick. Piston
// movers use these for direct-contact pushing; index matches the underlying
// collision box so the pushing mover can be ignored while resolving its own
// contact correction.
struct MovingBodyBox {
    BodyBox box;
    glm::dvec3 delta;
};

// Boxes participating in one piston-push pass: movers carries the per-box
// motion, collision carries the same boxes in the form move_box consumes.
struct MovingBoxPushSet {
    std::vector<MovingBodyBox> movers;
    std::vector<BodyBox> collision;
};

class Collision {
  public:
    // Resolves `movement_vec` against the voxel world, mutating it in place to
    // the actual displacement that avoids collisions, and updates `position`
    // to the post-move player position. Sets *out_standing to 1 if the player
    // is resting on a solid surface below (used for jumping/ground detection).
    // The player AABB is centered on (position.x, position.z); position.y is
    // the eye, which sits eye_height above the feet inside a box player_height
    // tall. When edge_guard is set, horizontal moves made while standing that
    // would leave the player with no support within STEP_HEIGHT below are
    // clamped back to the edge (sneak edge protection).
    // half_width is the AABB's horizontal half-extent (mobs pass their own
    // hitbox width; the player keeps the default).
    static void check_move( World &world, glm::dvec3 &movement_vec, glm::dvec3 &position, int *out_standing,
                            double player_height, double eye_height, bool edge_guard, double half_width = PLAYER_WIDTH / 2.0,
                            const std::vector<BodyBox> *extra = nullptr );
    // Returns 1 if the player AABB at `player` overlaps the collision boxes a
    // block with `blockState` would have at `block` — used by placement
    // validation so thin shapes (panes, fence posts) can be placed in a cell
    // the player clips into, while shapes reaching past the cell (fences,
    // closed gates — 1.5 tall) still reject.
    static int check_collides_with_block( World &world, const glm::dvec3 &player, const glm::ivec3 &block, const BlockState &blockState,
                                          double player_height, double eye_height, double half_width = PLAYER_WIDTH / 2.0 );
    // Collision boxes for `blockState` at fractional world origin `origin`,
    // deriving neighbor-dependent shapes (fence connections, stair corners,
    // wall/bars masks) from the spawn-time 3x3x3 `neighbors` snapshot rather
    // than the live world. Used for moving block bodies; ordinary terrain
    // uses the live-world path internally. `mins`/`maxs` must have room for
    // FENCE_MAX_BOXES entries. Returns the box count.
    static int shaped_boxes_for_state( const glm::dvec3 &origin, const glm::ivec3 &cell, const BlockState &blockState,
                                       const BlockEmitNeighbors &neighbors, glm::dvec3 *mins, glm::dvec3 *maxs );
    // Returns true if the player AABB with the given dimensions at eye
    // position `eye` overlaps any solid block. Used for the headroom check
    // before standing up out of a sneak.
    static bool collides_at( World &world, const glm::dvec3 &eye, double player_height, double eye_height, double half_width = PLAYER_WIDTH / 2.0 );
    // Returns true if the player AABB overlaps any cell holding a climbable
    // block (ladders, scaffolding — the whole cell counts, not the thin
    // slab/frame geometry). Unloaded cells don't count. `found_id`, when
    // non-null, gets the matching block's id (movement treats scaffolding
    // differently from ladders: sneak descends and idle hangs).
    static bool on_climbable( World &world, const glm::dvec3 &eye, double player_height, double eye_height, double half_width = PLAYER_WIDTH / 2.0,
                              BlockID *found_id = nullptr );
    // Pushes a body whose box already overlaps the world out to free space —
    // for a hitbox that just grew into a wall (baby mob -> adult), where
    // check_move's face-crossing clamps can't help and the body would tunnel
    // through. Repeatedly escapes along the shallowest-penetration axis and
    // rewrites `eye` to the resolved position. Returns false (eye untouched)
    // when a few pushes can't free it — a pocket tighter than the box.
    static bool unstick( World &world, glm::dvec3 &eye, double player_height, double eye_height, double half_width = PLAYER_WIDTH / 2.0 );
    // Returns true if the AABB [min,max] overlaps any colliding block box
    // (unloaded cells count as solid). move_box only clamps faces the leading
    // edge crosses, so a box that already overlaps is invisible to it — this
    // is how a caller detects one.
    static bool box_collides( World &world, const glm::dvec3 &min, const glm::dvec3 &max );
    // Depenetrates an arbitrary AABB already embedded in solid terrain —
    // e.g. a dropped-item box a block was placed onto, where move_box's
    // face-crossing clamps can never help. Prefers the cheapest horizontal
    // escape whose landing spot is free; when every side stays walled it
    // lifts the box bottom above the highest overlapping box top. Under a
    // taller column one call climbs one block, so callers re-run each tick
    // and the box floats up. A free box is left untouched. Returns false
    // when the box is still embedded after the push (mid-column).
    static bool unstick_box( World &world, glm::dvec3 &min, glm::dvec3 &max );
    // Top of the block's collision boxes at `block_pos` — fences and closed
    // gates reach 1.5 above their cell floor; unloaded counts as a full
    // block. Non-colliding blocks return the cell floor. Used by the mob
    // auto-jump to tell a hoppable 1-block wall from a fence.
    static double block_collision_top( World &world, const glm::ivec3 &block_pos );
    // Moves the AABB [min,max] by `delta` using the same sub-stepped axis
    // resolution as check_move (Y then X then Z) but with no step-up and no
    // edge guard, additionally colliding with the `extra` body boxes.
    // Unloaded cells count as solid. hit_axis[i] records whether the move
    // clamped on axis i; when the clamp came from an extra box,
    // hit_extra_axis[i] holds that box's index (else -1). *standing is set
    // when a downward Y move is blocked.
    // Fills mins/maxs with the terrain collision AABBs in the cells
    // overlapping the given box (full block shapes: fences, stairs, slabs,
    // pistons), returns the count. Shared by oriented-hull collision.
    static int terrain_boxes( World &world, const glm::dvec3 &amin, const glm::dvec3 &amax,
                              glm::dvec3 *mins, glm::dvec3 *maxs, int max_boxes );
    static void move_box( World &world, glm::dvec3 &min, glm::dvec3 &max, const glm::dvec3 &delta,
                          const std::vector<BodyBox> &extra, bool hit_axis[3], int hit_extra_axis[3], bool *standing,
                          int ignore_index = -1 );
    // Direct-contact push from one moving shaped box. If the box overlaps
    // [min,max], computes the separation needed along the box's translation
    // axis and resolves it through move_box against terrain plus `dynamic`,
    // ignoring the pushing box itself. Mutates [min,max] and returns the
    // actual applied displacement (zero when there is no contact or terrain
    // fully wedges the actor).
    static glm::dvec3 apply_moving_box_push( World &world, const MovingBodyBox &moving, glm::dvec3 &min, glm::dvec3 &max,
                                             const std::vector<BodyBox> &dynamic );
};
