#pragma once

#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/item.hpp"
#include "common/net/framed_socket.hpp"
#include "common/net/packet.hpp"
#include "common/net_containers.hpp"
#include "common/net_entities.hpp"
#include <atomic>
#include <chrono>
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
    uint32_t prev_held_item = 0;
    uint64_t prev_riding_cart = 0;
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

    // Liveness stamps: send_payload marks every outbound frame, handle_frame
    // marks every inbound one. NetEntities::tick reads them to emit
    // HEARTBEATs on outbound silence and to drop the connection on inbound
    // silence (epoch = no frame yet — armed to "now" on the first tick while
    // connected).
    std::chrono::steady_clock::time_point last_send;
    std::chrono::steady_clock::time_point last_recv;

    // Diffs that arrived from the server before the client's terrain gen
    // finished for that chunk. Keyed by chunk_pos; applied when the chunk
    // finishes loading in ChunkLoader::render_chunks.
    std::map<glm::ivec3, NetChunkDiffResultPayload, Compare_I_Vec3> pending_diffs;
    // Same staging for SIGN_TEXT: the server's per-chunk sign frames can
    // arrive while the chunk is still generating. Entries that name a
    // non-sign cell (stale race with a break) are dropped at apply time.
    std::map<glm::ivec3, std::vector<NetSignTextPayload>, Compare_I_Vec3> pending_signs;

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
    // Send a sign-text edit request. The server validates/sanitizes/stores
    // and broadcasts the stored text back (the sender included), so the
    // commit path also applies locally for immediate feedback.
    void send_sign_text( const glm::ivec3 &pos, const SignText &text );
    // Send one chat line (PLAYER/EMOTE/WHISPER; `target` only matters for
    // WHISPER). The server stamps `from` from its own name table.
    void send_chat( uint8_t kind, const std::string &target, const std::string &text );
    // Publish a display-name change. The server sanitizes + dedupes and
    // broadcasts the effective name back; player_names[self_player_id]
    // then holds the server-accepted spelling.
    void send_player_name( const std::string &name );
    // Effective display names keyed by fd, fed by PLAYER_NAME frames
    // (initial replay + join/rename broadcasts). Entries vanish on
    // PLAYER_DISCONNECTED; an fd without an entry shows "Player<fd>".
    std::map<int32_t, std::string> player_names;
    // This client's own fd, learned from the WELCOME frame's player_id.
    int32_t self_player_id = -1;
    // The name table entry for `player_id`, or nullptr when unnamed.
    const std::string *name_of( int32_t player_id ) const;
    // Case-insensitive name -> fd lookup over player_names (whisper
    // completion); false when no player matches.
    bool find_player( const std::string &name_ci, int32_t &player_id ) const;
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
    void update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation, float eye_height, uint8_t flags, float health, ItemID held_item, uint64_t riding_cart );
    // Returns false (and queues nothing) when not connected, so callers
    // retrying one-shot requests (initial_chunk_request_pending) or
    // coverage bookkeeping (slot_diff_req) can retry / stay unstamped.
    bool request_chunk( const glm::ivec3 &chunk_pos );
    bool request_chunks_box( const glm::ivec3 &min, uint8_t sx, uint8_t sy, uint8_t sz, bool initial = false );

    // Queue a chunk diff for a chunk that isn't loaded yet. Called from
    // process_events when get_chunk returns nullptr.
    void queue_pending_diff( const NetChunkDiffResultPayload &diff );
    // Size of the pending-diff queue (MPLOAD trace / diagnostics).
    size_t pending_diffs_count( ) const { return this->pending_diffs.size( ); }
    // Apply any pending diffs for this chunk, then remove them from the
    // pending queue. Called from ChunkLoader::render_chunks after a chunk
    // finishes loading; boundary cells are mirrored into loaded neighbors
    // via chunkLoader.apply_diff_block. Returns true when an entry existed.
    bool apply_pending_diffs( ChunkLoader &chunkLoader, Chunk &chunk );
    // Same drain for queued SIGN_TEXT frames: writes straight into the
    // chunk's sign map (text whose cell isn't a sign block is dropped —
    // a break/edit race resolved in the server's favour).
    bool apply_pending_signs( Chunk &chunk );
};
