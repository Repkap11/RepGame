#ifndef HEADER_MAP_STORAGE_H
#define HEADER_MAP_STORAGE_H

#include "common/chunk.hpp"
#include "common/RepGame.hpp"

void map_storage_init( );
void map_storage_persist( Chunk *chunk );
int map_storage_load( Chunk *chunk );
int map_storage_read_player_data( PlayerData *player_data );
void map_storage_write_player_data( PlayerData *player_data );

#endif