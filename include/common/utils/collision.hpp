#ifndef HEADER_COLLISION_H
#define HEADER_COLLISION_H
#include "common/RepGame.hpp"

void collision_check_move( World *world, TRIP_ARGS( float *movement_vec_ ), TRIP_ARGS( float position_ ), int *out_standing );
int collision_check_collides_with_block( World *world, TRIP_ARGS( float player_ ), TRIP_ARGS( float block_ ) );

#endif