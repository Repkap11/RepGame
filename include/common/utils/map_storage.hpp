#pragma once

#include "common/chunk.hpp"
#include "common/RepGame.hpp"

#define FILE_ROOT_CHUNK "%s/chunk_%d_%d_%d"
#define FILE_ROOT_PLAYER_DATA "%s/player.dat"

class MapStorage {
  public:
    static void init( const char *world_name );
    static void persist( const Chunk &chunk );
    static int load( Chunk &chunk );
    static int read_player_data( PlayerData &player_data );
    static void write_player_data( const PlayerData &player_data );
};