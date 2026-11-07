#pragma once

#include <glm.hpp>
#include "common/block.hpp"
#include "common/chest.hpp"
#include "common/constants.hpp"
#include "common/furnace.hpp"
#include "common/net/framed_socket.hpp"
#include "common/utils/map_storage.hpp"
#include "server/server_entities.hpp"

#include <chrono>
#include <map>
#include <set>
#include <string>

class Server;

// Compare_I_Vec3 lives in common/chest.hpp (shared with the client).

// Per-chunk in-memory cache of unsaved edits (diffs against generated
// terrain). The cache holds ONLY chunks with pending edits — diff requests
// for unedited chunks read the save file without caching.
// last_edit_time drives the idle-timer save: when a chunk hasn't been
// edited for CHUNK_IDLE_SAVE_SECONDS, it's persisted to disk and evicted
// from memory.
struct ChunkCacheEntry {
    std::map<int, BlockState> blocks;
    std::chrono::steady_clock::time_point last_edit_time;
};

// How long a chunk must be idle (no edits) before the server saves it to
// disk and evicts it from the in-memory cache.
static constexpr int CHUNK_IDLE_SAVE_SECONDS = 30;

// How often the server broadcasts its authoritative world clock to all
// connected clients. Both ends tick at UPS_RATE, so this only needs to
// correct accumulated drift (e.g. client ticks dropped under load).
static constexpr int WORLD_TIME_SYNC_SECONDS = 5;

// All chest contents in one chunk, keyed by world block position (the same
// shape MapStorage's chest file uses). `loaded` means the chunk's chest file
// was read (or confirmed absent) — the first chest access in a chunk pulls in
// every chest in it so persisting writes the complete set.
struct ChestChunkState {
    bool loaded = false;
    bool dirty = false;
    ChestContentsMap chests;
};

// All furnace contents in one chunk, keyed by world block position (the
// same shape MapStorage's furnace file uses). `progress_dirty` tracks which
// furnaces changed since the last throttled FURNACE_CONTENTS broadcast so
// viewers see cook/burn progress without a packet per tick.
struct FurnaceChunkState {
    bool loaded = false;
    bool dirty = false;
    std::set<glm::ivec3, Compare_I_Vec3> progress_dirty;
    FurnaceContentsMap furnaces;
    // Last-known BlockState per furnace (placement updates + lit swaps), so
    // the server can flip FURNACE<->LIT_FURNACE without losing rotation.
    std::map<glm::ivec3, BlockState, Compare_I_Vec3> states;
};

class ServerLogic {
    MapStorage map_storage;
    // Entity arbitration: chunk ownership, sidecar persistence, pickup
    // races. No simulation.
    ServerEntities entities;
    std::map<glm::ivec3, ChunkCacheEntry, Compare_I_Vec3> world_cache;
    // Chunks known to have no edits (no save file, or a file with zero
    // diffs): diff requests skip the disk entirely for these.
    std::set<glm::ivec3, Compare_I_Vec3> known_empty_chunks;
    // Bound on known_empty_chunks — ~262k entries is a few tens of MB worst
    // case, sized for the 1GB server box. On overflow the whole set is
    // dropped; it's only an optimization.
    static constexpr size_t KNOWN_EMPTY_CHUNKS_MAX = 1 << 18;
    // Chest state per chunk, populated lazily by ensureChestChunk.
    std::map<glm::ivec3, ChestChunkState, Compare_I_Vec3> chest_chunks;
    // chest world pos -> client fds currently viewing it (one chest open per
    // client; reverse index below).
    std::map<glm::ivec3, std::set<int>, Compare_I_Vec3> chest_subscribers;
    std::map<int, glm::ivec3> client_open_chest;

    // Furnaces: same lazy per-chunk cache + subscriber model as chests, but
    // the server also ticks them (fuel burn, cook progress, lit-block swap)
    // and pushes throttled progress snapshots to viewers.
    std::map<glm::ivec3, FurnaceChunkState, Compare_I_Vec3> furnace_chunks;
    std::map<glm::ivec3, std::set<int>, Compare_I_Vec3> furnace_subscribers;
    std::map<int, glm::ivec3> client_open_furnace;
    // Furnace tick schedule (UPS_RATE cadence) + progress broadcast throttle.
    std::chrono::steady_clock::time_point next_furnace_tick;
    int furnace_broadcast_countdown = 0;

    // Sorted wake-up schedule: time -> set of chunk positions whose idle
    // timer expires at that time. This is the "sorted list of times to
    // wake up" — epoll_wait uses the earliest entry as its timeout.
    std::multimap<std::chrono::steady_clock::time_point, glm::ivec3> wake_schedule;
    // Reverse lookup: chunk position -> its current wake time, so we can
    // cancel/reschedule a chunk's timer when a new edit arrives.
    std::map<glm::ivec3, std::chrono::steady_clock::time_point, Compare_I_Vec3> chunk_wake;

    // Day/night clock (server-authoritative, world_time units are
    // RepGame::tick ticks — DAY_LENGTH_TICKS per day). While any client is
    // connected the clock runs lazily: worldTimeNow() = base + (now -
    // anchor) * UPS_RATE. When the last client disconnects the running
    // value is frozen back into base and persisted to world.dat, so the
    // clock stands still while the server is empty. init() sets base to the
    // saved value, or noon for a fresh world.
    int64_t world_time_base = 0;
    std::chrono::steady_clock::time_point world_time_anchor;
    bool world_time_running = false;
    std::chrono::steady_clock::time_point next_time_broadcast;

    int64_t worldTimeNow( ) const;
    void sendWorldTime( Server &server, int client_fd );
    void broadcastWorldTime( Server &server );

    void record_block( const glm::ivec3 &block_pos, BlockState &block_state );
    void respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset );
    void persistChunk( const glm::ivec3 &chunk_pos );
    void persistAll( );
    ChunkCacheEntry *loadIntoCache( const glm::ivec3 &chunk_offset );
    bool loadDiffsFromDisk( const glm::ivec3 &chunk_offset, std::map<int, BlockState> &out );
    void scheduleChunkSave( const glm::ivec3 &chunk_pos );
    void cancelChunkSave( const glm::ivec3 &chunk_pos );

    // Chests
    ChestChunkState &ensureChestChunk( const glm::ivec3 &chunk_pos );
    void persistChestChunk( const glm::ivec3 &chunk_pos );
    bool chestChunkHasSubscribers( const glm::ivec3 &chunk_pos ) const;
    void sendChestContents( Server &server, int client_fd, const glm::ivec3 &chest_pos, const ChestContents &contents );
    void unsubscribeChest( int client_fd );
    void handleChestOpen( Server &server, int client_fd, const glm::ivec3 &chest_pos );
    void handleChestSetSlot( Server &server, int client_fd, const glm::ivec3 &chest_pos, uint32_t slot_index, const InventorySlot &value );
    void handleChestRemoved( Server &server, int client_fd, const glm::ivec3 &chest_pos );

    // Furnaces
    FurnaceChunkState &ensureFurnaceChunk( const glm::ivec3 &chunk_pos );
    void persistFurnaceChunk( const glm::ivec3 &chunk_pos );
    bool furnaceChunkHasSubscribers( const glm::ivec3 &chunk_pos ) const;
    void sendFurnaceContents( Server &server, int client_fd, const glm::ivec3 &furnace_pos, const FurnaceContents &contents );
    void unsubscribeFurnace( int client_fd );
    void handleFurnaceOpen( Server &server, int client_fd, const glm::ivec3 &furnace_pos );
    void handleFurnaceSetSlot( Server &server, int client_fd, const glm::ivec3 &furnace_pos, uint32_t slot_index, const InventorySlot &value );
    void handleFurnaceRemoved( Server &server, int client_fd, const glm::ivec3 &furnace_pos );
    // Advance every cached furnace one tick; called from processTimers at
    // UPS_RATE while any furnace chunk is cached. Broadcasts lit-state block
    // updates and throttled progress snapshots.
    void tickFurnaces( Server &server );
    // True when any cached furnace can still change (burning, or holding
    // fuel to ignite). Gates the UPS_RATE wake so idle cached furnaces
    // don't keep epoll waking at tick rate.
    bool anyFurnaceActive( ) const;

  public:
    void init( const char *world_name );
    void on_client_connected( Server &server, int client_fd );
    void on_client_message( Server &server, int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload );
    void on_client_disconnected( Server &server, int client_fd );
    // Process expired timers: save/evict idle chunks, and broadcast
    // WORLD_TIME when the periodic sync comes due. Called after each
    // epoll_wait return.
    void processTimers( Server &server );
    // Returns the timeout (in milliseconds) for the next epoll_wait, based
    // on the earliest pending wake-up. Returns -1 if no timers are pending.
    int nextWakeTimeoutMs( ) const;
    // Save all dirty chunks and clear the cache. Called on SIGINT and when
    // the last client disconnects.
    void flushAll( );
    // Selftest hook: number of chunks currently held in world_cache.
    size_t cachedChunkCount( ) const {
        return this->world_cache.size( );
    }
    // Selftest hook: number of chunks known to have no edits.
    size_t knownEmptyChunkCount( ) const {
        return this->known_empty_chunks.size( );
    }
    // Selftest hook: entity arbitration state.
    ServerEntities &test_entities( ) {
        return this->entities;
    }
};
