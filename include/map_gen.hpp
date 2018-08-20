#ifndef HEADER_MAP_GEN_H
#define HEADER_MAP_GEN_H

#include "chunk.hpp"

void map_gen_load_block_cuda( Chunk *chunk );
void map_gen_load_block_c( Chunk *chunk );
void map_gen_free_block( Chunk *chunk );
#endif