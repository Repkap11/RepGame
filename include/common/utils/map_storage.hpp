#pragma once

#include <atomic>
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

// ---- Chunk edit files (format v1) ----------------------------------------
// A chunk_<x>_<y>_<z> file holds only the cells that differ from generated
// terrain (MapGen + StructureGen), as sorted, non-overlapping runs:
//   u32 magic (0x4B434752), u32 version (1), u32 record_size
//   (sizeof(BlockState) at write time — readers copy the shared prefix),
//   u32 run_count, then run_count records of
//   { u32 start, u32 count, record_size bytes of BlockState }.
// Every cell not inside a run is natural. Files without the magic decode
// through the legacy full-chunk RLE branch (see map_storage.cpp).
struct ChunkEditRun {
    uint32_t start;
    uint32_t count;
    BlockState state;
};
struct ChunkEdits {
    std::vector<ChunkEditRun> runs;
    // The file was a legacy client full snapshot (no hole cells): stored
    // states that match natural terrain aren't real edits and are skipped
    // at apply time.
    bool legacy_snapshot = false;
    // Every legacy file, plus a v1 file whose record_size differs from this
    // build's: the chunk rewrites in v1 on the next persist (or unlinks
    // when nothing differs from natural).
    bool needs_rewrite = false;
};

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
    // Reads a chunk's edit runs: 1 when the file decoded (runs may be
    // empty), 0 when no file exists (or chunk IO is disabled), -1 when the
    // file exists but couldn't be decoded.
    int load_chunk_edits( const glm::ivec3 &pos, ChunkEdits &out );
    // Writes the runs in v1 format; an empty run list removes the file.
    void persist_chunk_edits( const glm::ivec3 &pos, const std::vector<ChunkEditRun> &runs );
    // Tests/selftest only: writes the pre-v1 legacy RLE format (a raw
    // CHUNK_BLOCK_SIZE BlockState array, hole sentinels included) so the
    // migration paths can be exercised. Production code never calls this.
    void persist_chunk_legacy( const glm::ivec3 &chunk_offset, const BlockState *blocks );
    // Multiplayer gate: while false, load_chunk_edits/persist_chunk_edits
    // and the client chest/furnace file paths do no disk IO — that world
    // state lives on the server. Set from Multiplayer::multiplayer_mode()
    // each frame (World::render) and once during init before the first
    // chunk enqueues. The server never touches it.
    std::atomic<bool> chunk_io_enabled{ true };
    void set_chunk_io_enabled( const bool enabled ) {
        this->chunk_io_enabled.store( enabled, std::memory_order_relaxed );
    }
    // Writes (or, when `chests` is empty, removes) the chest contents file for
    // a chunk. Keys are world block positions.
    void persist_chests( const glm::ivec3 &chunk_offset, const ChestContentsMap &chests );
    int check_if_chunk_exists( const glm::ivec3 &chunk_offset );
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