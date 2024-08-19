
#include "server/server_logic.hpp"
#include "common/utils/file_utils.hpp"
#include "common/RepGame.hpp"

#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerLogic::init( const char *world_name ) {
    this->map_storage.init( world_name );
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

void ServerLogic::record_block( const glm::ivec3 &block_pos, BlockState &block_state ) {
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
    glm::ivec3 diff = block_pos - ( chunk_pos * CHUNK_SIZE_I );
    int block_index = Chunk::get_index_from_coords( diff );

    // auto &chunk_cache = this->world_cache[ chunk_pos ];
    // chunk_cache[ block_index ] = block_state;
    // pr_debug( "Updating center chunk: %d %d %d  %d", chunk_pos.x, chunk_pos.y, chunk_pos.z, block_index );

    for ( int i = -1; i < 2; i++ ) {
        const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
        if ( !needs_update_x ) {
            continue;
        }
        for ( int j = -1; j < 2; j++ ) {
            const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
            if ( !needs_update_y ) {
                continue;
            }
            for ( int k = -1; k < 2; k++ ) {
                const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                if ( !needs_update_z ) {
                    continue;
                }

                // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );

                glm::ivec3 new_chunk_pos = chunk_pos + glm::ivec3( i * needs_update_x, j * needs_update_y, k * needs_update_z );
                glm::ivec3 new_diff = block_pos - ( new_chunk_pos * CHUNK_SIZE_I );
                int new_block_index = Chunk::get_index_from_coords( new_diff );

                pr_debug( "Updating chunk: %d %d %d  %d", new_chunk_pos.x, new_chunk_pos.y, new_chunk_pos.z, new_block_index );
                auto &new_chunk_cache = this->world_cache[ new_chunk_pos ];
                new_chunk_cache[ new_block_index ] = block_state;
            }
        }
    }
}

void ServerLogic::respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset ) {

    const auto it_world = this->world_cache.find( chunk_offset );

    std::map<int, BlockState> *chunk_cache_prt;
    if ( it_world == this->world_cache.end( ) ) {
        chunk_cache_prt = this->loadIntoCache( chunk_offset );
    } else {
        chunk_cache_prt = &it_world->second;
    }
    if ( chunk_cache_prt == NULL ) {
        // We don't have any blocks for this chunk.
        return;
    }
    const std::map<int, BlockState> &chunk_cache = *chunk_cache_prt;
    NetPacket packet;
    packet.data.chunk_diff.chunk_x = chunk_offset.x;
    packet.data.chunk_diff.chunk_y = chunk_offset.y;
    packet.data.chunk_diff.chunk_z = chunk_offset.z;
    packet.type = PacketType::CHUNK_DIFF_RESULT;
    int packet_index = 0;
    // int total_packets_sent = 0;
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
            // total_packets_sent += 1;
        }
    }
    if ( packet_index != 0 ) {
        packet.data.chunk_diff.num_used_updates = packet_index;
        server.queue_packet( client_fd, &packet );
        // total_packets_sent += 1;
        // pr_debug( "Responding to client:%d with %d blocks", client_fd, packet_index );
    }
    // pr_debug( "Respond with total packets:%d for blocks:%ld", total_packets_sent, chunk_cache.size( ) );
}

void ServerLogic::persistCache( ) {
    BlockState *blocks = ( BlockState * )malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) );

    for ( auto [ chunk_pos, chunk_cache ] : this->world_cache ) {
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
            blocks[ i ] = BLOCK_STATE_LAST_BLOCK_ID;
        }
        for ( auto [ chunk_index, blockState ] : chunk_cache ) {
            blocks[ chunk_index ] = blockState;
        }
        this->map_storage.persist_dirty_blocks( chunk_pos, blocks );
        // pr_debug( "Saving chunk:%d %d %d", chunk_pos.x, chunk_pos.y, chunk_pos.z );
    }
    free( blocks );
    pr_debug( "World saved" );
    this->world_cache.clear( );
}
std::map<int, BlockState> *ServerLogic::loadIntoCache( const glm::ivec3 &chunk_offset ) {
    BlockState *blocks = ( BlockState * )malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) );
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
        blocks[ i ] = BLOCK_STATE_LAST_BLOCK_ID;
    }
    int dirty = 0; // They can be dirty if they are in the old chunk format, we don't really care.
    int ret = this->map_storage.load_blocks( chunk_offset, blocks, dirty, true );
    if ( ret == 0 ) {
        free( blocks );
        // Couldn't read anything...
        return NULL;
    }
    std::map<int, BlockState> &chunk_cache = this->world_cache[ chunk_offset ];
    pr_debug( "found chunk:%d %d %d size:%ld", chunk_offset.x, chunk_offset.y, chunk_offset.z, chunk_cache.size( ) );
    chunk_cache.clear( );
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
        BlockState &blockState = blocks[ i ];
        if ( blockState.id != LAST_BLOCK_ID ) {
            chunk_cache[ i ] = blockState;
        }
    }
    free( blocks );
    return &chunk_cache;
}

void ServerLogic::on_client_message( Server &server, int client_fd, NetPacket *packet ) {

    // pr_debug( "Got message from:%d block:%d", client_fd, packet->blockID );
    if ( packet->type == PacketType::BLOCK_UPDATE ) {
        PacketType_DataBlock &block_data = packet->data.block;
        glm::ivec3 block_pos = glm::ivec3( block_data.x, block_data.y, block_data.z );
        this->record_block( block_pos, block_data.blockState );
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
    // A client is gone, this is a good time to save the world.
    this->persistCache( );
}
