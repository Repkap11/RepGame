#pragma once

#include <glm.hpp>
#include <queue>
#include <common/constants.hpp>
#include <common/block.hpp>
#include <common/net/framed_socket.hpp>
#include <server/server_logic.hpp>

// Legacy PacketType enum kept temporarily for reference during migration.
// The new wire protocol uses NetMsgType (see common/net/packet.hpp).

// Player data storage (still used for tracking connected clients' positions).
struct PacketType_DataPlayer {
    float x;
    float y;
    float z;
    float rotation[ 4 * 4 ];
};

struct ClientData {
    int connected;
    PacketType_DataPlayer player_data;
    FramedSocket socket;
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
    void serve( );
    void kill( );
    void cleanup( );
    void queue_message( int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload );
    void queue_empty( int client_fd, NetMsgType type, int32_t player_id );
    PacketType_DataPlayer *get_data_if_client_connected( int client_id );
};
