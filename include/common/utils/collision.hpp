#pragma once

#include <vector>

// Kept light: RepGame.hpp pulls in world.hpp, which pulls in block_bodies.hpp,
// which pulls this header back in — a full include here would hide the
// BlockBodies definition from world.hpp.
#include <glm/glm.hpp>
#include "common/constants.hpp"

class World;
struct RepGameState;

// An extra AABB obstacle move_box collides against on top of the voxel
// world — the other live BlockBodies. index is opaque to collision code;
// move_box reports it back through hit_extra_axis so the caller can exchange
// momentum with the body that was actually hit.
struct BodyBox {
    glm::dvec3 min;
    glm::dvec3 max;
    int index;
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
                            double player_height, double eye_height, bool edge_guard, double half_width = PLAYER_WIDTH / 2.0 );
    // Returns 1 if the player AABB at `player` overlaps the block at `block`.
    static int check_collides_with_block( World &world, const glm::dvec3 &player, const glm::ivec3 &block,
                                          double player_height, double eye_height, double half_width = PLAYER_WIDTH / 2.0 );
    // Returns true if the player AABB with the given dimensions at eye
    // position `eye` overlaps any solid block. Used for the headroom check
    // before standing up out of a sneak.
    static bool collides_at( World &world, const glm::dvec3 &eye, double player_height, double eye_height, double half_width = PLAYER_WIDTH / 2.0 );
    // Pushes a body whose box already overlaps the world out to free space —
    // for a hitbox that just grew into a wall (baby mob -> adult), where
    // check_move's face-crossing clamps can't help and the body would tunnel
    // through. Repeatedly escapes along the shallowest-penetration axis and
    // rewrites `eye` to the resolved position. Returns false (eye untouched)
    // when a few pushes can't free it — a pocket tighter than the box.
    static bool unstick( World &world, glm::dvec3 &eye, double player_height, double eye_height, double half_width = PLAYER_WIDTH / 2.0 );
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
    static void move_box( World &world, glm::dvec3 &min, glm::dvec3 &max, const glm::dvec3 &delta,
                          const std::vector<BodyBox> &extra, bool hit_axis[3], int hit_extra_axis[3], bool *standing );
};
