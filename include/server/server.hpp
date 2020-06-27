#ifndef HEADER_SERVER_H
#define HEADER_SERVER_H

#include <glm.hpp>

#define MAX_CLIENT_FDS 100

typedef enum { INVALID, PLAYER_LOCATION, BLOCK_UPDATE, CLIENT_INIT, PLAYER_CONNECTED, PLAYER_DISCONNECTED } PacketType;

#define SERVER_BLOCK_DATA_SIZE 16

typedef struct {
    PacketType type;
    int player_id;
    union {
        struct {
            int x;
            int y;
            int z;
            char blockState[ SERVER_BLOCK_DATA_SIZE ];
        } block;
        struct {
            float x;
            float y;
            float z;
            float rotation[ 4 * 4 ];
        } player;
    } data;
} NetPacket;

void server_queue_packet( int client_fd, NetPacket *packet );
int server_is_client_connected( int client_id );

#endif
