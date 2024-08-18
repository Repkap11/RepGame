#pragma once

#include <glm.hpp>
#include "common/block.hpp"

class Server;
struct NetPacket;

class ServerLogic {
    void record_block( const glm::ivec3 &chunk_pos, int block_index, BlockState &block_state );

  public:
    void on_client_connected( Server &server, int client_fd );
    void on_client_message( Server &server, int client_fd, NetPacket *packet );
    void on_client_disconnected( Server &server, int client_fd );
};