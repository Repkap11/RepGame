#pragma once

#include <glm.hpp>
#include "common/block.hpp"
#include "common/constants.hpp"

#include <map>
#include <string>

class Server;
struct NetPacket;

class ServerLogic {
    char map_name[ CHUNK_NAME_MAX_LENGTH ];
    std::map<std::string, std::map<int, BlockState>> world_cache;

    void record_block( const glm::ivec3 &chunk_offset, int block_index, BlockState &block_state );
    void respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset );

  public:
    void init( const char *world_name );
    void on_client_connected( Server &server, int client_fd );
    void on_client_message( Server &server, int client_fd, NetPacket *packet );
    void on_client_disconnected( Server &server, int client_fd );
};