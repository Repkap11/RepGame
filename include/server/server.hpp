#pragma once

#include <glm/glm.hpp>
#include <queue>
#include <set>
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
    float eye_height;
    uint8_t flags; // NET_PLAYER_FLAG_* (targetable etc.)
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
    void init( int portnum, const char *world_name = "Server1" );
    void serve( );
    void kill( );
    void cleanup( );
    void queue_message( int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload );
    void queue_empty( int client_fd, NetMsgType type, int32_t player_id );
    // Serialize `payload` through its NetCodec and queue it for one client.
    template <typename P>
    void send( int client_fd, NetMsgType type, const P &payload, int32_t player_id = 0 ) {
        PacketWriter w;
        NetCodec<P>::ser( w, payload );
        this->queue_message( client_fd, type, player_id, w.take_buf( ) );
    }
    // Broadcast helpers. `player_id` is stamped into each frame's payload
    // header — pass the originating client_fd for relays, 0 for
    // server-originated messages. `except_fd` (-1 = none) skips one
    // recipient (typically the originator of a relay).
    void broadcast( NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload, int except_fd = -1 );
    void broadcast_to( const std::set<int> &fds, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload, int except_fd = -1 );
    void broadcast_empty( NetMsgType type, int32_t player_id, int except_fd = -1 );
    PacketType_DataPlayer *get_data_if_client_connected( int client_id );

    // Selftest hooks (src/server/server_selftest.cpp): drive the logic layer
    // and inspect client outboxes without real networking.
    ServerLogic &test_logic( ) {
        return this->server_logic;
    }
    ClientData &test_client( int client_fd ) {
        return this->client_data[ client_fd ];
    }
    void test_add_epoll( int client_fd ) {
        this->add_epoll( client_fd );
    }
    void test_del_epoll( int client_fd ) {
        this->del_epoll( client_fd );
    }
    void test_handle_client_read( int client_fd ) {
        this->handle_client_ready_for_read( client_fd );
    }
};
