#pragma once

#include <cstdint>
#include <vector>

class Chunk;
struct PlayerData;
struct BlockState;

#define MAP_NAME_MAX_LENGTH 512
#define CHUNK_NAME_MAX_LENGTH 640
#define FILE_ROOT_CHUNK "%s/chunk_%d_%d_%d"
#define FILE_ROOT_CHEST "%s/chest_%d_%d_%d"
#define FILE_ROOT_FURNACE "%s/furnace_%d_%d_%d"
#define FILE_ROOT_MOBS "%s/mobs_%d_%d_%d"
#define FILE_ROOT_ITEMS "%s/items_%d_%d_%d"
#define FILE_ROOT_PLAYER_DATA "%s/player.dat"
#define FILE_ROOT_WORLD_DATA "%s/world.dat"
#include <glm/glm.hpp>

#include "common/chest.hpp"
#include "common/furnace.hpp"
#include "common/item.hpp"

// Save migration (pre-2047 format): LEGACY_EMPTY_BLOCK_ID was the empty-slot
// marker, and ids now past LAST_BLOCK_ID or undefined (in either the block
// or item range) load as empty slots or as AIR in chunks.
static inline bool save_slot_id_is_empty( uint32_t id ) {
    return id == LEGACY_EMPTY_BLOCK_ID || !item_id_valid( id );
}

static inline BlockID save_chunk_block_id( uint32_t id ) {
    return ( id == LEGACY_EMPTY_BLOCK_ID || id >= LAST_BLOCK_ID ) ? AIR : static_cast<BlockID>( id );
}

// Per-mob save record (persist_mobs/load_mobs). Packed on disk; readers copy
// only the shared prefix so records from a build with a different size still
// parse. Records with type >= MOB_TYPE_COUNT are skipped by the loader.
// net_id is the natural-mob id (net_natural_mob_id) for records produced by
// the natural roll, 0 otherwise — older files without it load as 0.
struct __attribute__( ( packed ) ) MobFileRecord {
    uint16_t type;
    uint16_t variant;
    double x, y, z;
    float yaw;
    float health;
    uint32_t flags;
    uint64_t net_id;
    // Breeding state: negative = a baby's remaining growth ticks (grown up
    // at 0), positive = an adult's re-breed cooldown. Older files zero-fill.
    int32_t grow_ticks;
};

// Per-item save record (persist_items/load_items). Packed on disk; readers
// copy only the shared prefix so records from a build with a different size
// still parse. Records whose item fails item_id_valid or whose count <= 0
// are skipped by the loader. Pickup delay is not persisted — loaded items
// are pickable immediately.
struct __attribute__( ( packed ) ) ItemFileRecord {
    uint32_t item;
    int32_t count;
    double x, y, z;
    float vx, vy, vz;
    int32_t age_ticks;
};

struct PlayerData;

class MapStorage {
    char map_name[ MAP_NAME_MAX_LENGTH ];

  public:
    void init( const char *world_name );
    // Points map_name at an existing directory without creating anything —
    // the offline inspector's read-only mode.
    void init_readonly( const char *map_dir );
    // Reads `<world dir>/world_type`; a first token of `flat` sets
    // g_repgame_flat_world (sandstone-floor generator, see
    // MapGen::flat_world_id). Must run before init()/any chunk generation.
    static bool detect_flat_world( const char *world_name );
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
    // Same for furnace contents (slots + cook/burn progress). Keys are world
    // block positions.
    void persist_furnaces( const glm::ivec3 &chunk_offset, const FurnaceContentsMap &furnaces );
    int load_furnaces( const glm::ivec3 &chunk_offset, FurnaceContentsMap &out );
    // Writes the chunk's mob records plus the tombstoned natural-roll
    // indices (`removed`). A file's existence no longer marks the chunk
    // populated — the deterministic roll always runs on load — so when
    // records and removed are both empty the file is deleted instead of
    // written.
    void persist_mobs( const glm::ivec3 &chunk_offset, const std::vector<MobFileRecord> &mobs, const std::vector<uint8_t> &removed = {} );
    // Loads mob records into `out`, and (when `removed` is given) the
    // tombstoned natural-roll indices stored after the records. Returns 1
    // when the file existed — even when it holds zero records.
    int load_mobs( const glm::ivec3 &chunk_offset, std::vector<MobFileRecord> &out, std::vector<uint8_t> *removed = nullptr );
    // Writes the chunk's dropped-item records. An empty list still writes
    // the file — unlike mobs there is no natural respawn, so an empty file
    // and a missing one are equivalent; writing it just keeps a chunk whose
    // items were all picked up from resurrecting stale records.
    void persist_items( const glm::ivec3 &chunk_offset, const std::vector<ItemFileRecord> &items );
    // Loads dropped-item records into `out`. Returns 1 when the file existed.
    int load_items( const glm::ivec3 &chunk_offset, std::vector<ItemFileRecord> &out );
    int read_player_data( PlayerData &player_data );
    void write_player_data( const PlayerData &player_data );
    // Server-side world clock (world.dat). Returns 1 if the file existed and
    // parsed; 0 leaves `world_time` untouched.
    int read_world_time( int64_t &world_time );
    void write_world_time( int64_t world_time );
    char *get_map_name( );
};