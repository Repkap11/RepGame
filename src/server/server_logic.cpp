
#include "server/server_logic.hpp"
#include "common/chunk.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/file_utils.hpp"

#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerLogic::init( const char *world_name ) {
    char *dir = getRepGamePath( );
    snprintf( this->map_name, CHUNK_NAME_MAX_LENGTH, "%s%s%s", dir, REPGAME_PATH_DIVIDOR, world_name );
    pr_debug( "Loading map from:%s", this->map_name );
    mkdir_p( map_name );
    free( dir );
}

void ServerLogic::on_client_connected( Server &server, int client_fd ) {
    pr_debug( "%d", client_fd );

    // Tell the other clients about this new client
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            // Don't tell this client that itself joined.
            continue;
        }
        if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
            pr_debug( "Notifying existing player:%d of new player:%d", prop_id, client_fd );
            NetPacket packet;
            // pr_debug( "Notifying2 existing player:%d of new player:%d", prop_id, client_fd );
            packet.type = PLAYER_CONNECTED;
            // pr_debug( "Notifying3 existing player:%d of new player:%d", prop_id, client_fd );
            packet.player_id = client_fd;
            // pr_debug( "Notifying4 existing player:%d of new player:%d", prop_id, client_fd );
            server.queue_packet( prop_id, &packet );
            // pr_debug( "Notifying5 existing player:%d of new player:%d", prop_id, client_fd );
        }
    }

    // Tell this client about the other clients already online
    for ( int online_id = 0; online_id < MAX_CLIENT_FDS; online_id++ ) {
        if ( online_id == client_fd ) {
            // Don't tell this client that itself joined.
            continue;
        }
        PacketType_DataPlayer *playerData = server.get_data_if_client_connected( online_id );
        if ( playerData != NULL ) {
            NetPacket packet;
            packet.type = PacketType::CLIENT_INIT;
            packet.player_id = online_id;
            packet.data.player = *playerData;
            server.queue_packet( client_fd, &packet );
            pr_debug( "Notifying new player:%d of existing player:%d", client_fd, online_id );
        }
    }
}

void ServerLogic::record_block( const glm::ivec3 &chunk_offset, int block_index, BlockState &block_state ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    if ( snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z ) != CHUNK_NAME_MAX_LENGTH ) {
    }
    auto &chunk_cache = this->world_cache[ file_name ];
    chunk_cache[ block_index ] = block_state;
    pr_debug( "Stored a block in:%s ", file_name );
}

void ServerLogic::respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    if ( snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z ) != CHUNK_NAME_MAX_LENGTH ) {
    }
    const auto it_world = this->world_cache.find( file_name );
    if ( it_world == this->world_cache.end( ) ) {
        return;
    }
    const std::map<int, BlockState> &chunk_cache = it_world->second;
    NetPacket packet;
    packet.data.chunk_diff.chunk_x = chunk_offset.x;
    packet.data.chunk_diff.chunk_y = chunk_offset.y;
    packet.data.chunk_diff.chunk_z = chunk_offset.z;
    packet.type = PacketType::CHUNK_DIFF_RESULT;
    int packet_index = 0;
    int total_packets_sent = 0;
    for ( auto [ blocks_index, blockState ] : chunk_cache ) {
        PacketType_DataChunkDiff_Block &packet_data = packet.data.chunk_diff.blockUpdates[ packet_index ];
        packet_data.blocks_index = blocks_index;
        packet_data.blockState = blockState;
        packet_index++;
        if ( packet_index >= SERVER_BLOCK_CHUNK_DIFF_SIZE ) {
            packet.data.chunk_diff.num_used_updates = packet_index;
            server.queue_packet( client_fd, &packet );
            // pr_debug( "Responding to client:%d with %d blocks", client_fd, packet_index );
            packet_index = 0;
            total_packets_sent += 1;
        }
    }
    if ( packet_index != 0 ) {
        packet.data.chunk_diff.num_used_updates = packet_index;
        server.queue_packet( client_fd, &packet );
        total_packets_sent += 1;
        // pr_debug( "Responding to client:%d with %d blocks", client_fd, packet_index );
    }
    pr_debug( "Responded to:%s with total packets:%d for blocks:%ld", file_name, total_packets_sent, chunk_cache.size( ) );
}

void ServerLogic::on_client_message( Server &server, int client_fd, NetPacket *packet ) {

    // pr_debug( "Got message from:%d block:%d", client_fd, packet->blockID );
    if ( packet->type == PacketType::BLOCK_UPDATE ) {
        PacketType_DataBlock &block_data = packet->data.block;
        glm::ivec3 block_pos = glm::ivec3( block_data.x, block_data.y, block_data.z );
        glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
        glm::ivec3 diff = block_pos - ( chunk_pos * CHUNK_SIZE_I );
        int blocks_index = Chunk::get_index_from_coords( diff );
        this->record_block( chunk_pos, blocks_index, block_data.blockState );
    } else if ( packet->type == PacketType::CHUNK_DIFF_REQUEST ) {
        glm::ivec3 chunk_pos = glm::ivec3( packet->data.chunk_diff.chunk_x, packet->data.chunk_diff.chunk_y, packet->data.chunk_diff.chunk_z );
        this->respondToChunkRequest( server, client_fd, chunk_pos );
    }

    // Forward packets from a client to all other clients.
    if ( packet->type == PacketType::BLOCK_UPDATE || packet->type == PacketType::PLAYER_LOCATION ) {
        // Tell the other connected players about most types of messages, but mark the
        // packet as from the player that sent it.

        // Tell the other clients that this client disconnected.
        for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
            if ( prop_id == client_fd ) {
                // Don't tell this client that itself joined.
                continue;
            }
            if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
                packet->player_id = client_fd;
                server.queue_packet( prop_id, packet );
            }
        }
    }
}

void ServerLogic::on_client_disconnected( Server &server, int client_fd ) {
    // pr_debug( "%d", client_fd );
    //  Tell the other clients that this client disconnected.
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            // Don't tell this client that itself joined.
            continue;
        }
        if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
            NetPacket packet;
            packet.type = PLAYER_DISCONNECTED;
            packet.player_id = client_fd;
            server.queue_packet( prop_id, &packet );
            pr_debug( "Notifying existing player:%d of removed player:%d", prop_id, client_fd );
        }
    }
}
