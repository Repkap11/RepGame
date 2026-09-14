#pragma once

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/net/framed_socket.hpp"
#include "common/net/packet.hpp"
#include <atomic>
#include <map>
#include <string>
#include <thread>

class Multiplayer {
    FramedSocket framed_socket;
    glm::vec3 prev_player_pos;
    glm::mat4 prev_rotation;
    friend class World;

    int sockfd = -1;
    int portno = 0;
    std::atomic<bool> active{ false };
    std::thread connect_thread;

    // Diffs that arrived from the server before the client's terrain gen
    // finished for that chunk. Keyed by chunk_pos; applied when the chunk
    // finishes loading in ChunkLoader::render_chunks.
    std::map<glm::ivec3, NetChunkDiffResultPayload, Compare_I_Vec3> pending_diffs;

    void connect_async( const std::string &hostname );

  public:
    void init( const char *hostname, int port );
    void cleanup( );

    void process_events( World &world );
    void set_block( const glm::ivec3 &block_pos, BlockState blockState );
    void update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation );
    void request_chunk( const glm::ivec3 &chunk_pos );
    void request_chunks_box( const glm::ivec3 &min, uint8_t sx, uint8_t sy, uint8_t sz );

    // Queue a chunk diff for a chunk that isn't loaded yet. Called from
    // process_events when get_chunk returns nullptr.
    void queue_pending_diff( const NetChunkDiffResultPayload &diff );
    // Apply any pending diffs for this chunk, then remove them from the
    // pending queue. Called from ChunkLoader::render_chunks after a chunk
    // finishes loading.
    void apply_pending_diffs( Chunk &chunk );
};
