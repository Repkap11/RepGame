#pragma once

// Server-side entity arbitration ("server arbitrates, owner client
// simulates"). The server never simulates: it owns entity identity,
// persistence (items_x_y_z / mobs_x_y_z sidecars via MapStorage), per-chunk
// ownership assignment and pickup races. The client nearest a chunk
// simulates that chunk's entities and streams ENTITY_STATE; every other
// client in view range renders mirrors.

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include <glm.hpp>

#include "common/chest.hpp" // Compare_I_Vec3
#include "common/net/packet.hpp"
#include "common/utils/map_storage.hpp"

class Server;

// One client's declared view box (from VIEW) and last known player chunk
// (from PLAYER_LOCATION eye pos). A chunk is covered by this client when it
// lies within center ± radius (chebyshev on xz with radius_xz, on y with
// radius_y).
struct EntityClientView {
    bool valid = false;
    glm::ivec3 center = glm::ivec3( 0 );
    int rx = 0;
    int ry = 0;
    glm::ivec3 player_chunk = glm::ivec3( 0 );
    bool has_player_chunk = false;
};

struct ServerChunkEntities {
    int owner_fd = -1;
    // The natural-spawn roll ran during this server coverage (CHUNK_POPULATED).
    // In-memory only — never read from or written to disk; on every re-cover
    // the pristine naturals are simply re-rolled by the new owner.
    bool rolled = false;
    // Tombstoned natural-roll indices: touched/killed naturals that the
    // deterministic roll must not recreate. Persisted in the mobs sidecar.
    std::set<uint8_t> removed_natural;
    // A mobs sidecar existed at first cover (kept so an all-empty persist
    // still deletes/updates it rather than leaving a stale file).
    bool mobs_file_present = false;
    bool items_dirty = false;
    bool mobs_dirty = false;
    std::map<NetEntityId, NetEntityRecord> entities;
    // Clients whose view box covers this chunk (owner included).
    std::set<int> viewers;
};

class ServerEntities {
    MapStorage *storage = nullptr;

    std::map<glm::ivec3, ServerChunkEntities, Compare_I_Vec3> chunks;
    // Entity id -> chunk holding it, so state/remove/pickup lookups are O(1)
    // instead of scanning every covered chunk. Maintained on every
    // insert/move/erase of the live table.
    std::unordered_map<NetEntityId, glm::ivec3> entity_chunk;
    // BODY records are simulated by their spawning client ("causer
    // simulates"), not by the chunk owner — ENTITY_STATE/REMOVE for a body
    // is accepted only from this fd. On that client's disconnect the sim
    // role hands over to the body's chunk owner (ENTITY_TRANSFER adopt=1)
    // or the record is dropped when the chunk is uncovered.
    std::unordered_map<NetEntityId, int> body_sim_fd;
    std::map<int, EntityClientView> clients;
    std::map<int, uint32_t> session_ids;
    // Per-client current role per chunk, for CHUNK_ROLE deltas.
    std::map<int, std::map<glm::ivec3, uint8_t, Compare_I_Vec3>> client_roles;

    uint32_t next_session_id = 1;
    uint32_t next_server_entity = 1; // server-minted ids: (0 << 32) | counter

    NetEntityId mint_server_id( );
    bool view_covers( const EntityClientView &v, const glm::ivec3 &chunk ) const;
    int distance_to( int fd, const glm::ivec3 &chunk ) const;
    uint8_t role_of( int fd, const glm::ivec3 &chunk ) const;
    void send_chunk_role( Server &server, int fd, const glm::ivec3 &chunk, uint8_t role );
    // Load a just-covered chunk's sidecars into records.
    void load_chunk_entities( ServerChunkEntities &ce, const glm::ivec3 &chunk );
    // Persist + drop an entity that landed in an uncovered chunk (spawn or
    // drift). Loads the sidecars, appends, writes back, evicts.
    void persist_stray_entity( const NetEntityRecord &rec, const glm::ivec3 &chunk );
    // Persist a covered-but-departing chunk's dirty sidecars, then erase it.
    void uncover_chunk( const glm::ivec3 &chunk );
    void persist_chunk_entities( ServerChunkEntities &ce, const glm::ivec3 &chunk );
    void mark_dirty( ServerChunkEntities &ce, NetEntityKind kind );
    // A NATURAL mob record gained TOUCHED: tombstone its roll index in the
    // home chunk's removed_natural (covered) or straight into its sidecar
    // (uncovered). Idempotent — only inserts when not already present.
    void note_touched_natural( NetEntityId id );
    // Recompute viewers + owner for the chunks in the symmetric difference
    // of old/new boxes, then re-check ownership for the intersection, and
    // push CHUNK_ROLE deltas to every client whose role changed.
    void recompute_boxes( Server &server, int fd, const EntityClientView &old_view, const EntityClientView &new_view );
    void apply_role_delta( Server &server, int fd, const glm::ivec3 &chunk );
    // Pick the best owner for a covered chunk: smallest chebyshev distance
    // from the viewer's player chunk; hysteresis keeps the current owner
    // while it still covers the chunk and is within +2 of best.
    int pick_owner( const ServerChunkEntities &ce, const glm::ivec3 &chunk ) const;
    // O(1) entity -> owning chunk lookup via entity_chunk.
    ServerChunkEntities *find_entity_chunk( NetEntityId id, glm::ivec3 *out_chunk );

  public:
    void init( MapStorage *storage );
    void on_client_connected( Server &server, int client_fd );
    void on_client_disconnected( Server &server, int client_fd );
    // Returns true when the message was an entity-protocol type (consumed).
    bool on_client_message( Server &server, int client_fd, NetMsgType type, const std::vector<uint8_t> &payload );
    // Notify that a client's player chunk may have changed (PLAYER_LOCATION).
    void on_player_position( Server &server, int client_fd, float x, float y, float z );
    // Persist every dirty chunk (flushAll / shutdown).
    void flush_all( );

    // Selftest hooks.
    int owner_of( const glm::ivec3 &chunk ) const {
        const auto it = this->chunks.find( chunk );
        return it == this->chunks.end( ) ? -1 : it->second.owner_fd;
    }
    size_t covered_chunk_count( ) const {
        return this->chunks.size( );
    }
    size_t entity_count_of( const glm::ivec3 &chunk ) const {
        const auto it = this->chunks.find( chunk );
        return it == this->chunks.end( ) ? 0 : it->second.entities.size( );
    }
    bool rolled_of( const glm::ivec3 &chunk ) const {
        const auto it = this->chunks.find( chunk );
        return it != this->chunks.end( ) && it->second.rolled;
    }
    size_t removed_natural_of( const glm::ivec3 &chunk ) const {
        const auto it = this->chunks.find( chunk );
        return it == this->chunks.end( ) ? 0 : it->second.removed_natural.size( );
    }
};
