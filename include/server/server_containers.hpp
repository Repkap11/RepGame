#pragma once

// Server side of container sync — the CONTAINER_* wire family, mirroring
// the client's NetContainers hub (and the ServerEntities pattern of
// feature-owned state living outside ServerLogic).
//
// One `Set` per NetContainerKindDef carries the machinery every container
// kind shares: a lazy per-chunk contents cache backed by the kind's
// sidecar file (def->load/def->persist), a subscriber set per container
// position, one open container per client, optimistic per-slot writes with
// a correction snapshot on rejection, and loot-on-break. Kind-specific
// behaviour (furnace ticking, lit-block swap) hangs off the def's
// active/tick/lit hooks — adding a synced container kind is a new row in
// container.cpp, not another copy of this file.

#include <chrono>
#include <map>
#include <set>

#include <glm/glm.hpp>

#include "common/container.hpp"
#include "common/net/packet.hpp"

class Server;
class ServerLogic;
class PacketReader;

// One container's synced state in the server cache: the contents blob plus
// the write counter stamped into CONTENTS and accepted SET_SLOT frames.
struct ServerContainerEntry {
    ContainerContents contents;
    uint32_t revision = 0;
};

// All containers of ONE kind in one chunk, keyed by world block position —
// the same shape the kind's MapStorage sidecar uses. `states` records the
// last-seen block state per container so lit-swap kinds can rewrite the
// block without losing rotation; `progress_dirty` lists entries a tick
// mutated since the last throttled CONTENTS broadcast.
struct ServerContainerChunk {
    bool loaded = false;
    bool dirty = false;
    std::map<glm::ivec3, ServerContainerEntry, Compare_I_Vec3> entries;
    std::map<glm::ivec3, BlockState, Compare_I_Vec3> states;
    std::set<glm::ivec3, Compare_I_Vec3> progress_dirty;
};

class ServerContainers {
    ServerLogic *logic = nullptr;

    // One per NetContainerKindDef: chunk cache + subscriber bookkeeping.
    struct Set {
        const NetContainerKindDef *def;
        std::map<glm::ivec3, ServerContainerChunk, Compare_I_Vec3> chunks;
        // pos -> viewing client fds; client_fd -> open pos.
        std::map<glm::ivec3, std::set<int>, Compare_I_Vec3> subscribers;
        std::map<int, glm::ivec3> client_open;
        // Throttle for tick progress broadcasts (CONTENTS every N ticks).
        int broadcast_countdown = 0;
    };
    Set sets[ NET_CONTAINER_KIND_COUNT ];

    // Tick schedule (UPS_RATE cadence) — consulted only for kinds with a
    // `tick` hook.
    std::chrono::steady_clock::time_point next_tick;

    ServerContainerChunk &ensureChunk( Set &set, const glm::ivec3 &chunk_pos );
    // entries[pos] sized to the kind's slot/aux arity (creates on demand).
    ServerContainerEntry &entryFor( Set &set, ServerContainerChunk &chunk, const glm::ivec3 &pos );
    void persistChunk( Set &set, const glm::ivec3 &chunk_pos );
    bool chunkHasSubscribers( const Set &set, const glm::ivec3 &chunk_pos ) const;
    void sendContents( Server &server, int client_fd, const Set &set, const glm::ivec3 &pos, const ServerContainerEntry &entry );
    // Answers an unauthorized/invalid write with the authoritative snapshot
    // for that container — the optimistic client's state self-heals.
    void sendCorrection( Server &server, int client_fd, const Set &set, const glm::ivec3 &pos );
    void unsubscribe( Set &set, int client_fd );
    void handleOpen( Server &server, Set &set, int client_fd, const glm::ivec3 &pos );
    void handleSetSlot( Server &server, Set &set, int client_fd, const glm::ivec3 &pos, uint32_t slot_index, const InventorySlot &value );
    void handleRemoved( Server &server, Set &set, int client_fd, const glm::ivec3 &pos );
    void tickSet( Server &server, Set &set );

  public:
    void init( ServerLogic *logic );

    // CONTAINER_* dispatch; true when the type was consumed.
    bool on_client_message( Server &server, int client_fd, NetMsgType type, PacketReader &r );
    // BLOCK_UPDATE hook: learns/forgets container block states per kind and
    // runs loot-on-break when a write overwrites a container.
    void on_block_update( Server &server, int client_fd, const glm::ivec3 &pos, const BlockState &state );
    void on_client_disconnected( int client_fd );

    // Furnace ticking runs at UPS_RATE while some ticking container can
    // still change — an idle cached container doesn't earn a wake. Called
    // from ServerLogic::processTimers; up to 8 catch-up ticks run when a
    // message woke the loop anyway.
    void tick_due( Server &server, std::chrono::steady_clock::time_point now );
    bool any_active( ) const;
    // Folds next_tick into `wake` for nextWakeTimeoutMs when any_active.
    void next_wake( std::chrono::steady_clock::time_point &wake ) const;

    // Idle-save plumbing (ServerLogic::persistChunk / the wake loop):
    // persist writes each kind's dirty sidecar data for the chunk;
    // evict_unwatched drops kind caches no client is viewing.
    void persist_chunk( const glm::ivec3 &chunk_pos );
    void evict_unwatched( const glm::ivec3 &chunk_pos );
    void persist_all( );

    // Selftest/debug visibility.
    const ServerContainerEntry *find( NetContainerKind kind, const glm::ivec3 &pos ) const;
    int subscriber_count( NetContainerKind kind, const glm::ivec3 &pos ) const;
};
