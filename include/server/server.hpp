#pragma once

#include <glm.hpp>
#include <queue>
#include <common/constants.hpp>

typedef enum { INVALID, PLAYER_LOCATION, BLOCK_UPDATE, CLIENT_INIT, PLAYER_CONNECTED, PLAYER_DISCONNECTED, CHUNK_DIFF } PacketType;

#define SERVER_BLOCK_DATA_SIZE 16
#define SERVER_BLOCK_CHUNK_DIFF_SIZE 10 // TODO this could be up to the total number of blocks in a chunk.... that would be a lot of data.
typedef struct {
    float x;
    float y;
    float z;
    float rotation[ 4 * 4 ];
} PacketType_DataPlayer;

typedef struct {
    int x;
    int y;
    int z;
    char blockState[ SERVER_BLOCK_DATA_SIZE ];
} PacketType_DataBlock;

typedef struct {
    int blocks_index;
    char blockState[ SERVER_BLOCK_DATA_SIZE ];
} PacketType_DataChunkDiff_Block;

typedef struct {
    int chunk_x;
    int chunk_y;
    int chunk_z;
    int num_used_updates;
    PacketType_DataChunkDiff_Block blockUpdates[ SERVER_BLOCK_CHUNK_DIFF_SIZE ];
} PacketType_DataChunkDiff;

typedef struct {
    PacketType type;
    int player_id;
    union {
        PacketType_DataBlock block;
        PacketType_DataPlayer player;
        PacketType_DataChunkDiff chunk_diff;
    } data;
} NetPacket;

typedef struct {
    int connected;
    PacketType_DataPlayer player_data;
    std::queue<NetPacket> pending_sends;
} ClientData;

void server_queue_packet( int client_fd, NetPacket *packet );
PacketType_DataPlayer *server_get_data_if_client_connected( int client_id );
