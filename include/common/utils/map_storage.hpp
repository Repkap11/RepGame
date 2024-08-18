#pragma once

class Chunk;
struct PlayerData;

#define CHUNK_NAME_MAX_LENGTH 200
#define FILE_ROOT_CHUNK "%s/chunk_%d_%d_%d"
#define FILE_ROOT_PLAYER_DATA "%s/player.dat"
#include <glm.hpp>

struct PlayerData;

class MapStorage {
    char map_name[ CHUNK_NAME_MAX_LENGTH ];
    int check_if_chunk_exists( const glm::ivec3 &chunk_offset );

  public:
    void init( const char *world_name );
    void persist( const Chunk &chunk );
    int load( Chunk &chunk );
    int read_player_data( PlayerData &player_data );
    void write_player_data( const PlayerData &player_data );
    char *get_map_name( );
};