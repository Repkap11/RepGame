#ifndef HEADER_MAP_STORAGE_H
#define HEADER_MAP_STORAGE_H

#include "chunk.h"

void map_storage_persist( Chunk *chunk );
int map_storage_load( Chunk *chunk );

#endif