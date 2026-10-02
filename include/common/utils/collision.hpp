#pragma once

#include "common/RepGame.hpp"

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
    static void check_move( World &world, glm::dvec3 &movement_vec, glm::dvec3 &position, int *out_standing,
                            double player_height, double eye_height, bool edge_guard );
    // Returns 1 if the player AABB at `player` overlaps the block at `block`.
    static int check_collides_with_block( World &world, const glm::dvec3 &player, const glm::ivec3 &block,
                                          double player_height, double eye_height );
    // Returns true if the player AABB with the given dimensions at eye
    // position `eye` overlaps any solid block. Used for the headroom check
    // before standing up out of a sneak.
    static bool collides_at( World &world, const glm::dvec3 &eye, double player_height, double eye_height );
};
