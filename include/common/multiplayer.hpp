#pragma once

#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/net/framed_socket.hpp"
#include "common/net/packet.hpp"
#include "common/net_containers.hpp"
#include "common/net_entities.hpp"
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

class ChunkLoader;
struct RepGameState;

class Multiplayer {
    friend int test_net_multiplayer_mode( );
    FramedSocket framed_socket;
    glm::vec3 prev_player_pos;
    glm::mat4 prev_rotation;
    float prev_eye_height = -1.0f;
    uint8_t prev_flags = 0xFF;
    float prev_health = -1.0f;
    friend class World;
    friend class NetEntities;

    int sockfd = -1;
    // WASM: browser WebSocket handle (EMSCRIPTEN_WEBSOCKET_T, an int).
    // Unused on other platforms.
    int ws_handle = -1;
    int portno = 0;
    std::string hostname;
    std::atomic<bool> active{ false };
    // True while an async connect is in flight (POSIX connect thread or WASM
    // WebSocket handshake); cleared on success (alongside active=true) and
    // on every failure path. Lets the overlay show "connecting" vs
    // "disconnected" during the bounded connect window.
    std::atomic<bool> connect_pending{ false };
    // Latched when the connection first comes up; never cleared. Lets
    // multiplayer_mode() tell "was connected and dropped" (still server
    // entities — don't touch local files) from "connect failed outright"
    // (purely local session — local entity files must stay on or every
    // drop/mob is lost at logout). Set wherever a transport assigns
    // active=true.
    std::atomic<bool> ever_connected{ false };
    std::thread connect_thread;
    // Peer protocol version from a frame that forced a disconnect; -1 when
    // the connection ended (or is running) without a mismatch. Read by the
    // ImGui overlay via protocol_mismatch_version().
    int mismatched_server_version = -1;

    // Diffs that arrived from the server before the client's terrain gen
    // finished for that chunk. Keyed by chunk_pos; applied when the chunk
    // finishes loading in ChunkLoader::render_chunks.
    std::map<glm::ivec3, NetChunkDiffResultPayload, Compare_I_Vec3> pending_diffs;

    void connect_async( const std::string &hostname );

    // Dispatch one complete frame payload (version|type|player_id|body).
    // Shared by the POSIX and WASM transports. Takes RepGameState (not just
    // World) because chest packets also touch inventories and chest UI state.
    void handle_frame( RepGameState &state, const std::vector<uint8_t> &payload );

    // Per-platform: push queued outbound frames to the transport, and mark
    // the connection dead / release transport resources. Implemented in the
    // platform multiplayer.cpp.
    void flush_outbound( );
    void disconnect( );

    // Defined in src/wasm/multiplayer.cpp: the emscripten WebSocket event
    // callbacks need access to framed_socket/active/ws_handle.
    friend struct WasmMultiplayerHooks;

  public:
    void init( const char *hostname, int port );
    void cleanup( );

    void process_events( RepGameState &state );
    void set_block( const glm::ivec3 &block_pos, BlockState blockState, uint8_t flags = 0 );
    // Send a world-time set request to the server (PageUp/PgDn skips). The
    // server adopts the value and broadcasts it back to all clients.
    void send_world_time( long world_time );
    // Entity sync hub (server arbitrates, owner client simulates). Bound to
    // this Multiplayer at construction; NetEntities::handle is called from
    // handle_frame for the entity-protocol message types.
    NetEntities entities{ this };
    // Container sync hub (CONTAINER_* wire family — server is authoritative,
    // clients write optimistically). Same dispatch model as `entities`.
    NetContainers containers{ this };
    // Multiplayer entity mode: a hostname was given AND the connect is
    // still in flight or ever succeeded. Local entity file persistence
    // keys off this so single player and multiplayer saves never mix.
    // Once connected the mode latches for the session — even across a
    // later drop — while a hostname whose connect failed outright leaves
    // a purely local session that must persist entities locally.
    bool multiplayer_mode( ) const {
        return !this->hostname.empty( ) &&
               ( this->connect_pending.load( ) || this->active.load( ) || this->ever_connected.load( ) );
    }
    // Queue + flush one typed payload; disconnects on send error. Used by
    // NetEntities and the chest_*-style helpers.
    void send_payload( NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload );
    // Serialize `payload` through its NetCodec and queue it via send_payload.
    // No-ops when not connected; senders should be one line around this.
    template <typename P>
    void send( NetMsgType type, const P &payload, int32_t player_id = 0 ) {
        if ( !this->active.load( ) ) {
            return;
        }
        PacketWriter w;
        NetCodec<P>::ser( w, payload );
        this->send_payload( type, player_id, w.take_buf( ) );
    }
    bool is_active( ) const { return this->active.load( ); }
    bool is_connecting( ) const { return this->connect_pending.load( ); }
    // The server's protocol version if we disconnected over a mismatch, -1 otherwise.
    int protocol_mismatch_version( ) const { return this->mismatched_server_version; }
    // Server the client is (trying to be) connected to; empty when
    // multiplayer was never started (single player).
    const std::string &server_hostname( ) const { return this->hostname; }
    int server_port( ) const { return this->portno; }
    void update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation, float eye_height, uint8_t flags, float health );
    void request_chunk( const glm::ivec3 &chunk_pos );
    // Returns false (and queues nothing) when not connected, so callers
    // retrying one-shot requests (initial_chunk_request_pending) can keep
    // their retry flag set.
    bool request_chunks_box( const glm::ivec3 &min, uint8_t sx, uint8_t sy, uint8_t sz );

    // Queue a chunk diff for a chunk that isn't loaded yet. Called from
    // process_events when get_chunk returns nullptr.
    void queue_pending_diff( const NetChunkDiffResultPayload &diff );
    // Apply any pending diffs for this chunk, then remove them from the
    // pending queue. Called from ChunkLoader::render_chunks after a chunk
    // finishes loading; boundary cells are mirrored into loaded neighbors
    // via chunkLoader.apply_diff_block.
    void apply_pending_diffs( ChunkLoader &chunkLoader, Chunk &chunk );
};
