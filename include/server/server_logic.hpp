#pragma once

#include <glm.hpp>
#include "common/block.hpp"
#include "common/constants.hpp"
#include "common/net/framed_socket.hpp"
#include "common/utils/map_storage.hpp"

#include <chrono>
#include <map>
#include <set>
#include <string>

class Server;

struct Compare_I_Vec3 {
    bool operator( )( const glm::ivec3 &a, const glm::ivec3 &b ) const {
        int x_diff = a.x - b.x;
        if ( x_diff != 0 ) {
            return x_diff < 0;
        }
        int y_diff = a.y - b.y;
        if ( y_diff != 0 ) {
            return y_diff < 0;
        }
        int z_diff = a.z - b.z;
        return z_diff < 0;
    }
};

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

class ServerLogic {
    MapStorage map_storage;
    std::map<glm::ivec3, ChunkCacheEntry, Compare_I_Vec3> world_cache;

    // Sorted wake-up schedule: time -> set of chunk positions whose idle
    // timer expires at that time. This is the "sorted list of times to
    // wake up" — epoll_wait uses the earliest entry as its timeout.
    std::multimap<std::chrono::steady_clock::time_point, glm::ivec3> wake_schedule;
    // Reverse lookup: chunk position -> its current wake time, so we can
    // cancel/reschedule a chunk's timer when a new edit arrives.
    std::map<glm::ivec3, std::chrono::steady_clock::time_point, Compare_I_Vec3> chunk_wake;

    void record_block( const glm::ivec3 &block_pos, BlockState &block_state );
    void respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset );
    void persistChunk( const glm::ivec3 &chunk_pos );
    void persistAll( );
    ChunkCacheEntry *loadIntoCache( const glm::ivec3 &chunk_offset );
    void scheduleChunkSave( const glm::ivec3 &chunk_pos );
    void cancelChunkSave( const glm::ivec3 &chunk_pos );

  public:
    void init( const char *world_name );
    void on_client_connected( Server &server, int client_fd );
    void on_client_message( Server &server, int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload );
    void on_client_disconnected( Server &server, int client_fd );
    // Process expired idle timers: save and evict chunks that have been idle
    // for CHUNK_IDLE_SAVE_SECONDS. Called after each epoll_wait return.
    void processTimers( );
    // Returns the timeout (in milliseconds) for the next epoll_wait, based
    // on the earliest pending wake-up. Returns -1 if no timers are pending.
    int nextWakeTimeoutMs( ) const;
    // Save all dirty chunks and clear the cache. Called on SIGINT and when
    // the last client disconnects.
    void flushAll( );
};
