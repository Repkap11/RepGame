
#include "server/server_logic.hpp"
#include "common/chunk.hpp"
#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

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
            packet.type = CLIENT_INIT;
            packet.player_id = online_id;
            packet.data.player = *playerData;
            server.queue_packet( client_fd, &packet );
            pr_debug( "Notifying new player:%d of existing player:%d", client_fd, online_id );
        }
    }
}

void ServerLogic::record_block( const glm::ivec3 &chunk_pos, int block_index ) {
    
}

void ServerLogic::on_client_message( Server &server, int client_fd, NetPacket *packet ) {

    // pr_debug( "Got message from:%d block:%d", client_fd, packet->blockID );
    if ( packet->type == PacketType::BLOCK_UPDATE ) {
        glm::ivec3 block_pos = glm::ivec3( packet->data.block.x, packet->data.block.y, packet->data.block.z );
        glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
        glm::ivec3 diff = block_pos - ( chunk_pos * CHUNK_SIZE_I );
        int blocks_index = Chunk::get_index_from_coords( diff );
        this->record_block( chunk_pos, blocks_index );
    }
    if ( packet->type != INVALID ) {
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
