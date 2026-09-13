#pragma once

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "server/server.hpp"
#include <atomic>
#include <queue>
#include <string>
#include <thread>

class Multiplayer {
    NetPacket pending_packet;
    int pending_packet_len = 0;
    int pending_send_len = 0;
    std::queue<NetPacket> send_queue;
    glm::vec3 prev_player_pos;
    glm::mat4 prev_rotation;
    friend class World;

    int sockfd = -1;
    int portno = 0;
    std::atomic<bool> active{ false };
    std::thread connect_thread;

    void flush_send_queue( );
    void send_packet( const NetPacket &update );
    void connect_async( const std::string &hostname );

  public:
    void init( const char *hostname, int port );
    void cleanup( );

    void process_events( World &world );
    void set_block( const glm::ivec3 &block_pos, BlockState blockState );
    void update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation );
    void request_chunk( const glm::ivec3 &chunk_pos );
};
