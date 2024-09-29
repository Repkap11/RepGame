#pragma once

class Chunk;
struct PlayerData;
struct BlockState;

#define MAP_NAME_MAX_LENGTH 200
#define CHUNK_NAME_MAX_LENGTH 256
#define FILE_ROOT_CHUNK "%s/chunk_%d_%d_%d"
#define FILE_ROOT_PLAYER_DATA "%s/player.dat"
#include <glm.hpp>

struct PlayerData;

class MapStorage {
    char map_name[ MAP_NAME_MAX_LENGTH ];
    int check_if_chunk_exists( const glm::ivec3 &chunk_offset );

  public:
    void init( const char *world_name );
    void persist_chunk( const Chunk &chunk );
    void persist_dirty_blocks( const glm::ivec3 &chunk_offset, const BlockState *blocks );

    int load_chunk( Chunk &chunk );
    int load_blocks( const glm::ivec3 &chunk_offset, BlockState *blocks, int &dirty, bool expect_empty_blocks );
    int read_player_data( PlayerData &player_data );
    void write_player_data( const PlayerData &player_data );
    char *get_map_name( );
};