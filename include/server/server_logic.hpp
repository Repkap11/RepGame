#pragma once

#include <glm/glm.hpp>
#include "common/block.hpp"
#include "common/constants.hpp"
#include "common/net/framed_socket.hpp"
#include "common/utils/map_storage.hpp"
#include "server/diff_cache.hpp"
#include "server/server_containers.hpp"
#include "server/server_entities.hpp"
#include "server/server_signs.hpp"

#include <chrono>
#include <map>
#include <set>
#include <string>

class Server;

// Compare_I_Vec3 lives in common/chest.hpp (shared with the client).

// Per-chunk in-memory cache of unsaved edits (diffs against generated
// terrain). The cache holds ONLY chunks with pending edits — diff requests
// for unedited chunks read the save file without caching. An entry with
// empty `blocks` is timer-only: a container edit armed the idle save so
// its sidecars get persisted, but no block diff was ever merged — block
// reads must treat it as a cache miss.
// wake_deadline drives the idle-timer save: when a chunk hasn't been
// edited for CHUNK_IDLE_SAVE_SECONDS, it's persisted to disk and evicted
// from memory.
struct ChunkCacheEntry {
    std::map<int, BlockState> blocks;
    std::chrono::steady_clock::time_point last_edit_time;
    // Armed idle-save deadline, max() when unarmed. This is the timer's
    // truth — wake_schedule may hold stale entries after a reschedule.
    std::chrono::steady_clock::time_point wake_deadline = std::chrono::steady_clock::time_point::max( );
};

// How long a chunk must be idle (no edits) before the server saves it to
// disk and evicts it from the in-memory cache.
static constexpr int CHUNK_IDLE_SAVE_SECONDS = 30;

// How often the server broadcasts its authoritative world clock to all
// connected clients. Both ends tick at UPS_RATE, so this only needs to
// correct accumulated drift (e.g. client ticks dropped under load).
static constexpr int WORLD_TIME_SYNC_SECONDS = 5;

class ServerLogic {
    MapStorage map_storage;
    // Entity arbitration: chunk ownership, sidecar persistence, pickup
    // races. No simulation.
    ServerEntities entities;
    // Container arbitration: per-kind chunk caches, subscriber sets,
    // optimistic slot writes with correction, furnace ticking. Feature-owned
    // like `entities` — see server_containers.hpp.
    ServerContainers containers;
    // ServerContainers reaches these internals directly (map_storage,
    // world_cache, record_block/scheduleChunkSave for lit-swap edits).
    friend class ServerContainers;
    // Sign-text arbitration: per-chunk cache of sign_* sidecars, edit
    // validation against recorded block state, chunk-request + broadcast
    // sync. Feature-owned like `containers` — see server_signs.hpp.
    ServerSigns signs;
    friend class ServerSigns;
    std::map<glm::ivec3, ChunkCacheEntry, Compare_I_Vec3> world_cache;

    // Sorted wake-up schedule: time -> chunk position whose idle timer
    // expires then. This is the "sorted list of times to wake up" —
    // epoll_wait uses the earliest entry as its timeout. Reschedules leave
    // stale entries behind; processTimers skips a popped time that no
    // longer matches the entry's wake_deadline.
    std::multimap<std::chrono::steady_clock::time_point, glm::ivec3> wake_schedule;

    // Bounded LRU of encoded CHUNK_DIFF_RESULT payloads for CLEAN chunks
    // (no pending world_cache entry). Sized by REPGAME_SERVER_DIFF_CACHE_MB
    // / REPGAME_SERVER_DIFF_CACHE_MAX in init(); record_block invalidates
    // every chunk it dirties (halo included) before touching world_cache,
    // so the cache can never serve a stale diff.
    DiffCache diff_cache;
    // Running total of dirty cells across world_cache (sum of
    // entry.blocks.size()) — maintained by record_block/loadIntoCache/
    // eviction so worldCacheDirtyCells() stays O(1). Past
    // dirty_cells_max_ (REPGAME_SERVER_DIRTY_CELLS_MAX) record_block
    // persists and evicts the least-recently-edited chunks early instead
    // of letting the idle timers grow memory without bound.
    size_t dirty_cells_ = 0;
    size_t dirty_cells_max_ = 200000;

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
    // The latest recorded state of one cell — world_cache edits first,
    // then the chunk's on-disk edit file. False when the cell was never
    // edited (the server has no terrain view; unedited natural cells are
    // implicitly "not a sign" for ServerSigns validation).
    bool recorded_block_at( const glm::ivec3 &pos, BlockState &out );
    // Queues a CHUNK_DIFF_RESULT frame for chunks with diffs. Returns the
    // payload bytes queued (0 when the chunk has no diffs).
    size_t respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset );
    void persistChunk( const glm::ivec3 &chunk_pos );
    void persistAll( );
    // Persist + evict one world_cache entry (the shared tail of the idle
    // timer and the dirty-cap early save); decrements dirty_cells_ and
    // drops the chunk's unwatched container caches.
    void evictChunk( std::map<glm::ivec3, ChunkCacheEntry, Compare_I_Vec3>::iterator it );
    // While dirty_cells_ exceeds the cap, persist+evict the
    // least-recently-edited chunks first.
    void enforceDirtyCap( );
    ChunkCacheEntry *loadIntoCache( const glm::ivec3 &chunk_offset );
    bool loadDiffsFromDisk( const glm::ivec3 &chunk_offset, std::map<int, BlockState> &out );
    void scheduleChunkSave( const glm::ivec3 &chunk_pos );
    void cancelChunkSave( const glm::ivec3 &chunk_pos );

  public:
    void init( const char *world_name );
    // MPLOAD srv_mem counters (REPGAME_MPLOAD_TRACE).
    size_t worldCacheChunks( ) const { return this->world_cache.size( ); }
    size_t worldCacheDirtyCells( ) const { return this->dirty_cells_; }
    size_t dirtyCellsMax( ) const { return this->dirty_cells_max_; }
    // Per-kind save-file index sizes (see MapStorage::enable_file_index).
    void fileIndexCounts( size_t out[ MAP_FILE_KIND_COUNT ] ) const {
        this->map_storage.file_index_counts( out );
    }
    // DiffCache counters (see the diff_cache member above).
    size_t diffCacheEntries( ) const { return this->diff_cache.entries( ); }
    size_t diffCacheBytes( ) const { return this->diff_cache.bytes( ); }
    size_t diffCacheHits( ) const { return this->diff_cache.hits( ); }
    size_t diffCacheMisses( ) const { return this->diff_cache.misses( ); }
    size_t diffCacheEvictions( ) const { return this->diff_cache.evictions( ); }
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
    // Selftest hook: the world's storage (file index, direct file writes).
    MapStorage &test_storage( ) {
        return this->map_storage;
    }
    // Selftest hook: entity arbitration state.
    ServerEntities &test_entities( ) {
        return this->entities;
    }
    // Selftest hooks: the diff cache and the dirty-cell cap.
    DiffCache &test_diff_cache( ) {
        return this->diff_cache;
    }
    void test_set_dirty_cells_max( size_t n ) {
        this->dirty_cells_max_ = n;
    }
    // Selftest hook: container arbitration state.
    ServerContainers &test_containers( ) {
        return this->containers;
    }
    // Selftest hook: sign arbitration state.
    ServerSigns &test_signs( ) {
        return this->signs;
    }
};
