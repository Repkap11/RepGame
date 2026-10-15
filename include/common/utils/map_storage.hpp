#pragma once

#include <cstdint>

class Chunk;
struct PlayerData;
struct BlockState;

#define MAP_NAME_MAX_LENGTH 200
#define CHUNK_NAME_MAX_LENGTH 256
#define FILE_ROOT_CHUNK "%s/chunk_%d_%d_%d"
#define FILE_ROOT_CHEST "%s/chest_%d_%d_%d"
#define FILE_ROOT_PLAYER_DATA "%s/player.dat"
#define FILE_ROOT_WORLD_DATA "%s/world.dat"
#include <glm.hpp>

#include "common/chest.hpp"

struct PlayerData;

class MapStorage {
    char map_name[ MAP_NAME_MAX_LENGTH ];

  public:
    void init( const char *world_name );
    void persist_chunk( const Chunk &chunk );
    void persist_dirty_blocks( const glm::ivec3 &chunk_offset, const BlockState *blocks );
    // Writes (or, when `chests` is empty, removes) the chest contents file for
    // a chunk. Keys are world block positions.
    void persist_chests( const glm::ivec3 &chunk_offset, const ChestContentsMap &chests );
    int check_if_chunk_exists( const glm::ivec3 &chunk_offset );

    int load_chunk( Chunk &chunk );
    int load_blocks( const glm::ivec3 &chunk_offset, BlockState *blocks, int &dirty, bool expect_empty_blocks );
    // Loads all chest contents stored for a chunk into `out` (keyed by world
    // block position). Returns 1 when a chest file existed, 0 otherwise.
    int load_chests( const glm::ivec3 &chunk_offset, ChestContentsMap &out );
    int read_player_data( PlayerData &player_data );
    void write_player_data( const PlayerData &player_data );
    // Server-side world clock (world.dat). Returns 1 if the file existed and
    // parsed; 0 leaves `world_time` untouched.
    int read_world_time( int64_t &world_time );
    void write_world_time( int64_t world_time );
    char *get_map_name( );
};