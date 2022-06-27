#ifndef HEADER_RAY_TRAVERSAL_H
#define HEADER_RAY_TRAVERSAL_H

#include "common/chunk_loader.hpp"

int ray_traversal_find_block_from_to( World *world, Block *pixel_block,               //
                                      const glm::vec3 &v1, //
                                      const glm::vec3 &v2, //
                                      glm::ivec3 &out, //
                                      int *out_whichFace, int flag, int is_pick, int is_pixel );

#endif