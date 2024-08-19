#pragma once

#include <glm.hpp>
#include "common/block.hpp"
#include "common/constants.hpp"
#include "common/utils/map_storage.hpp"

#include <map>
#include <string>

class Server;
struct NetPacket;
// struct BlockState;

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

class ServerLogic {
    MapStorage map_storage;
    std::map<glm::ivec3, std::map<int, BlockState>, Compare_I_Vec3> world_cache;

    void record_block( const glm::ivec3 &block_pos, BlockState &block_state );
    void respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset );
    void persistCache( );
    std::map<int, BlockState> *loadIntoCache( const glm::ivec3 &chunk_offset );

  public:
    void init( const char *world_name );
    void on_client_connected( Server &server, int client_fd );
    void on_client_message( Server &server, int client_fd, NetPacket *packet );
    void on_client_disconnected( Server &server, int client_fd );
};