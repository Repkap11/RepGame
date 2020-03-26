#ifndef HEADER_RAY_TRAVERSAL_H
#define HEADER_RAY_TRAVERSAL_H

#include "common/chunk_loader.hpp"

int ray_traversal_find_block_from_to( World *world, Block *pixel_block,               //
                                      const float x1, const float y1, const float z1, //
                                      const float x2, const float y2, const float z2, //
                                      int *out_x, int *out_y, int *out_z, int *out_whichFace, int flag, int is_pick, int is_pixel );

#endif