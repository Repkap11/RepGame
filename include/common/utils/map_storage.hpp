#pragma once

#include <cstdint>
#include <vector>

class Chunk;
struct PlayerData;
struct BlockState;

#define MAP_NAME_MAX_LENGTH 200
#define CHUNK_NAME_MAX_LENGTH 256
#define FILE_ROOT_CHUNK "%s/chunk_%d_%d_%d"
#define FILE_ROOT_CHEST "%s/chest_%d_%d_%d"
#define FILE_ROOT_MOBS "%s/mobs_%d_%d_%d"
#define FILE_ROOT_PLAYER_DATA "%s/player.dat"
#define FILE_ROOT_WORLD_DATA "%s/world.dat"
#include <glm.hpp>

#include "common/chest.hpp"

// Save migration (pre-2047 format): LEGACY_EMPTY_BLOCK_ID was the empty-slot
// marker, and ids now past LAST_BLOCK_ID or undefined load as empty slots or
// as AIR in chunks.
static inline bool save_slot_id_is_empty( uint32_t id ) {
    return id == LEGACY_EMPTY_BLOCK_ID || id >= LAST_BLOCK_ID || !block_id_defined( id );
}

static inline BlockID save_chunk_block_id( uint32_t id ) {
    return ( id == LEGACY_EMPTY_BLOCK_ID || id >= LAST_BLOCK_ID ) ? AIR : static_cast<BlockID>( id );
}

// Per-mob save record (persist_mobs/load_mobs). Packed on disk; readers copy
// only the shared prefix so records from a build with a different size still
// parse. Records with type >= MOB_TYPE_COUNT are skipped by the loader.
struct __attribute__( ( packed ) ) MobFileRecord {
    uint16_t type;
    uint16_t variant;
    double x, y, z;
    float yaw;
    float health;
    uint32_t flags;
};

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
    // Writes the chunk's mob records. An empty list still writes the file —
    // its presence marks the chunk as populated (mobs were killed/removed)
    // so natural spawning never refills it.
    void persist_mobs( const glm::ivec3 &chunk_offset, const std::vector<MobFileRecord> &mobs );
    // Loads mob records into `out`. Returns 1 when the file existed — even
    // when it holds zero records (the populated marker).
    int load_mobs( const glm::ivec3 &chunk_offset, std::vector<MobFileRecord> &out );
    int read_player_data( PlayerData &player_data );
    void write_player_data( const PlayerData &player_data );
    // Server-side world clock (world.dat). Returns 1 if the file existed and
    // parsed; 0 leaves `world_time` untouched.
    int read_world_time( int64_t &world_time );
    void write_world_time( int64_t world_time );
    char *get_map_name( );
};