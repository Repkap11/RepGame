#ifndef HEADER_COLLISION_H
#define HEADER_COLLISION_H
#include "common/RepGame.hpp"

void collision_check_move( World *world, glm::vec3 &movement_vec, glm::vec3 &position, int *out_standing );
int collision_check_collides_with_block( World *world, const glm::vec3 &player, const glm::vec3& block );

#endif