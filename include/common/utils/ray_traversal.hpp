#pragma once

#include "common/chunk_loader.hpp"

class RayTraversal {
  public:
    static int find_block_from_to( World &world, Block *pixel_block, //
                                   const glm::vec3 &v1,              //
                                   const glm::vec3 &v2,              //
                                   glm::ivec3 &out,                  //
                                   int *out_whichFace, int flag, int is_pick, int is_pixel );
};