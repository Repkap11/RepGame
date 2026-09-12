#pragma once

#include "common/RepGame.hpp"

class Collision {
  public:
    // Resolves `movement_vec` against the voxel world, mutating it in place to
    // the actual displacement that avoids collisions, and updates `position`
    // to the post-move player position. Sets *out_standing to 1 if the player
    // is resting on a solid surface below (used for jumping/ground detection).
    // The player AABB is centered on (position.x, position.z) with the eye at
    // position.y; the body extends PLAYER_HEIGHT tall with EYE_POSITION_OFFSET.
    static void check_move( World &world, glm::vec3 &movement_vec, glm::vec3 &position, int *out_standing );
    // Returns 1 if the player AABB at `player` overlaps the block at `block`.
    static int check_collides_with_block( World &world, const glm::vec3 &player, const glm::vec3 &block );
};
