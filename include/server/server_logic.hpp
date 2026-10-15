#pragma once

#include <glm.hpp>
#include "common/block.hpp"
#include "common/chest.hpp"
#include "common/constants.hpp"
#include "common/net/framed_socket.hpp"
#include "common/utils/map_storage.hpp"

#include <chrono>
#include <map>
#include <set>
#include <string>

class Server;

// Compare_I_Vec3 lives in common/chest.hpp (shared with the client).

// Per-chunk in-memory cache of edits (diffs against generated terrain).
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

class ServerLogic {
    MapStorage map_storage;
    std::map<glm::ivec3, ChunkCacheEntry, Compare_I_Vec3> world_cache;
    // Chest state per chunk, populated lazily by ensureChestChunk.
    std::map<glm::ivec3, ChestChunkState, Compare_I_Vec3> chest_chunks;
    // chest world pos -> client fds currently viewing it (one chest open per
    // client; reverse index below).
    std::map<glm::ivec3, std::set<int>, Compare_I_Vec3> chest_subscribers;
    std::map<int, glm::ivec3> client_open_chest;

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
};
