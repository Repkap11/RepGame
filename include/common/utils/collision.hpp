#pragma once

#include "common/RepGame.hpp"

class Collision {
  public:
    static void check_move( World &world, glm::vec3 &movement_vec, glm::vec3 &position, int *out_standing );
    static int check_collides_with_block( World &world, const glm::vec3 &player, const glm::vec3 &block );
};