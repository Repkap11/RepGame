#ifndef HEADER_RAY_TRAVERSAL_H
#define HEADER_RAY_TRAVERSAL_H

#include "chunk_loader.h"

int ray_traversal_find_block_from_to( LoadedChunks *loadedChunks, float x1, float y1, float z1, //
                                      float x2, float y2, float z2,                             //
                                      int *out_x, int *out_y, int *out_z, int *out_whichFace );

#endif