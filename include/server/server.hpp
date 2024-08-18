#pragma once

#include <glm.hpp>
#include <queue>
#include <common/constants.hpp>
#include <server/server_logic.hpp>

typedef enum { INVALID, PLAYER_LOCATION, BLOCK_UPDATE, CLIENT_INIT, PLAYER_CONNECTED, PLAYER_DISCONNECTED, CHUNK_DIFF } PacketType;

#define SERVER_BLOCK_DATA_SIZE 16
#define SERVER_BLOCK_CHUNK_DIFF_SIZE 10 // TODO this could be up to the total number of blocks in a chunk.... that would be a lot of data.
struct PacketType_DataPlayer {
    float x;
    float y;
    float z;
    float rotation[ 4 * 4 ];
};

struct PacketType_DataBlock {
    int x;
    int y;
    int z;
    char blockState[ SERVER_BLOCK_DATA_SIZE ];
};

struct PacketType_DataChunkDiff_Block {
    int blocks_index;
    char blockState[ SERVER_BLOCK_DATA_SIZE ];
};

struct PacketType_DataChunkDiff {
    int chunk_x;
    int chunk_y;
    int chunk_z;
    int num_used_updates;
    PacketType_DataChunkDiff_Block blockUpdates[ SERVER_BLOCK_CHUNK_DIFF_SIZE ];
};

struct NetPacket {
    PacketType type;
    int player_id;
    union {
        PacketType_DataBlock block;
        PacketType_DataPlayer player;
        PacketType_DataChunkDiff chunk_diff;
    } data;
};

struct ClientData {
    int connected;
    PacketType_DataPlayer player_data;
    std::queue<NetPacket> pending_sends;
};

class Server {
    ServerLogic server_logic;
    int inet_socket_fd;
    int epoll_fd = 0;
    ClientData *client_data;
    struct epoll_event *events;

    static int setup_inet_socket( int port );
    static void make_socket_non_blocking( int sockfd );
    void add_epoll( int client_fd );
    void del_epoll( int client_fd );
    void update_epoll( int client_fd );
    void handle_new_client_event( int inet_socket_fd );
    void handle_client_ready_for_read( int client_fd );
    void handle_client_ready_for_write( int client_fd );

  public:
    void init( int portnum );
    void server( );
    void queue_packet( int client_fd, NetPacket *packet );
    PacketType_DataPlayer *get_data_if_client_connected( int client_id );
};
