#pragma once

#include <glm.hpp>
#include <queue>
#include <common/constants.hpp>

typedef enum { INVALID, PLAYER_LOCATION, BLOCK_UPDATE, CLIENT_INIT, PLAYER_CONNECTED, PLAYER_DISCONNECTED } PacketType;

#define SERVER_BLOCK_DATA_SIZE 16

typedef struct {
    float x;
    float y;
    float z;
    float rotation[ 4 * 4 ];
} PacketType_DataPlayer;

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
        PacketType_DataPlayer player;
    } data;
} NetPacket;

typedef struct {
    int connected;
    PacketType_DataPlayer player_data;
    std::queue<NetPacket> pending_sends;
} ClientData;

void server_queue_packet( int client_fd, NetPacket *packet );
PacketType_DataPlayer *server_get_data_if_client_connected( int client_id );
