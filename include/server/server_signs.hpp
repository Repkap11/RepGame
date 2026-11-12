#pragma once

// Server-side sign-text arbitration — the third feature hub next to
// ServerEntities and ServerContainers. Unlike containers there is no
// open/subscribe: every client that can see a sign needs its text, so the
// server pushes one SIGN_TEXT frame per stored sign inside each
// CHUNK_DIFF_REQUEST response (late joiners + reloads) and broadcasts the
// stored text on every accepted edit (live viewers).
//
// Validation model: the server has no terrain view, but sign blocks only
// ever exist through player BLOCK_UPDATE edits — so the recorded diff
// state for the cell (world_cache, falling back to the chunk's on-disk
// edit file) is authoritative. An edit naming a cell without a recorded
// sign state is rejected. Sign blocks that predate this feature simply
// can't be edited until the server learns their state — acceptable: their
// placement diff is already in the chunk file from when they were placed.
//
// Lifecycle mirrors ServerContainers: per-chunk cache lazy-loaded from the
// sign_* sidecar, dirty flag + scheduleChunkSave on mutation, persist on
// the chunk's idle-save/eviction, drop the cache entry at eviction (no
// subscribers — nothing keeps a sign cache pinned).

#include <map>

#include <glm/glm.hpp>

#include "common/chest.hpp" // Compare_I_Vec3
#include "common/sign.hpp"

class Server;
class ServerLogic;
class PacketReader;
enum class NetMsgType : uint8_t;
struct BlockState;

class ServerSigns {
    ServerLogic *logic = nullptr;

    // Per-chunk text cache, keyed by world block position (the sign_*
    // sidecar's key space — no index conversion like the client's map).
    struct ChunkCache {
        // Lazy-load guard: the first access in a chunk pulls in every sign
        // the sidecar has, so persisting writes the complete set.
        bool loaded = false;
        bool dirty = false;
        SignTextMap texts;
    };
    std::map<glm::ivec3, ChunkCache, Compare_I_Vec3> chunks;

    ChunkCache &ensureChunk( const glm::ivec3 &chunk_pos );
    static void send_text( Server &server, int client_fd, const glm::ivec3 &pos, const SignText &text );
    static void broadcast_text( Server &server, const glm::ivec3 &pos, const SignText &text );

  public:
    void init( ServerLogic *logic );

    // Dispatch from ServerLogic::on_client_message; returns true when the
    // type was consumed (SIGN_TEXT — handled or rejected).
    bool on_client_message( Server &server, int client_fd, NetMsgType type, PacketReader &r );

    // Called for every accepted client BLOCK_UPDATE: drops the stored text
    // when a sign is overwritten and broadcasts the empty clear.
    void on_block_update( Server &server, const glm::ivec3 &pos, const BlockState &state );

    // Queue one SIGN_TEXT per stored sign in the chunk — called for every
    // chunk in a CHUNK_DIFF_REQUEST box, with or without a diff result.
    void respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_pos );

    // Persist hooks driven by ServerLogic's chunk lifecycle.
    void persist_chunk( const glm::ivec3 &chunk_pos );
    void persist_all( );
    void evict_chunk( const glm::ivec3 &chunk_pos );

    // Selftest hooks.
    const SignText *find( const glm::ivec3 &pos ) const;
    size_t chunk_count( ) const { return this->chunks.size( ); }
};
