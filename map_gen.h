#ifndef HEADER_MAP_GEN_H
#define HEADER_MAP_GEN_H

#include "chunk.h"

int *map_gen_load_block( Chunk *chunk );
void map_gen_free_block( int *blocks );

#endif