#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/multiplayer.hpp"

void Multiplayer::init( const char *hostname, int port ) {
    pr_debug( "using server %s:%i", hostname, port );

    struct sockaddr_in serv_addr;
    struct hostent *server;

    this->active = false;
    this->portno = port;
    this->sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( this->sockfd < 0 ) {
        pr_debug( "Unable to allocate socket" );
        return;
    }
    server = gethostbyname( hostname );
    if ( server == NULL ) {
        pr_debug( "Unable to get hostname from string" );
        return;
    }

    bzero( ( char * )&serv_addr, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;

    bcopy( ( char * )server->h_addr, ( char * )&serv_addr.sin_addr.s_addr, server->h_length );

    serv_addr.sin_port = htons( this->portno );
    if ( connect( this->sockfd, ( struct sockaddr * )&serv_addr, sizeof( serv_addr ) ) < 0 ) {
        pr_debug( "Multiplayer failed to connect" );
        return;
    } else {
        // We connected!
        this->active = true;

        int flags = fcntl( this->sockfd, F_GETFL );
        int status = fcntl( this->sockfd, F_SETFL, flags | O_NONBLOCK );
        if ( status < 0 ) {
            pr_debug( "Unable to set non-blocking" );
            return;
        }
    }
}

void Multiplayer::process_events( World &world ) {
    if ( this->active ) {
        while ( true ) {
            // Send updates to the server
            NetPacket update;
            int status = read( this->sockfd, &update, sizeof( NetPacket ) );
            if ( status < 0 ) {
                // This is fine, it just means there are no messages;
                return;
            } else {
                if ( update.type == PacketType::CHUNK_DIFF_RESULT ) {
                    glm::ivec3 chunk_pos = glm::ivec3( update.data.chunk_diff.chunk_x, update.data.chunk_diff.chunk_y, update.data.chunk_diff.chunk_z );
                    Chunk *chunk_prt = world.chunkLoader.get_chunk( chunk_pos );
                    if ( chunk_prt == NULL ) {
                        // This chunk is not loaded anymore, ignore this update.
                        continue;
                    }
                    Chunk &chunk = *chunk_prt;
                    for ( int i = 0; i < update.data.chunk_diff.num_used_updates; ++i ) {
                        const PacketType_DataChunkDiff_Block &net_block = update.data.chunk_diff.blockUpdates[ i ];
                        chunk.set_block_by_index( net_block.blocks_index, &net_block.blockState );
                    }
                    // update.data.;
                    // world.set_loaded_block();
                } else if ( update.type == PacketType::BLOCK_UPDATE ) {
                    BlockState &blockState = update.data.block.blockState;
                    pr_debug( "Read message: block:%d", blockState.id );
                    glm::ivec3 block_pos = glm::ivec3( update.data.block.x, update.data.block.y, update.data.block.z );
                    world.set_loaded_block( block_pos, blockState );
                } else if ( update.type == PacketType::CLIENT_INIT ) {
                    world.multiplayer_avatars.add( update.player_id );
                    glm::mat4 rotation = glm::make_mat4( update.data.player.rotation );
                    world.multiplayer_avatars.update_position( update.player_id, update.data.player.x, update.data.player.y, update.data.player.z, rotation );
                } else if ( update.type == PacketType::PLAYER_LOCATION ) {
                    // pr_debug( "Updating player location:%d", update.player_id );
                    glm::mat4 rotation = glm::make_mat4( update.data.player.rotation );
                    world.multiplayer_avatars.update_position( update.player_id, update.data.player.x, update.data.player.y, update.data.player.z, rotation );
                } else if ( update.type == PacketType::PLAYER_CONNECTED ) {
                    pr_debug( "Updating player connected:%d", update.player_id );
                    world.multiplayer_avatars.add( update.player_id );
                } else if ( update.type == PacketType::PLAYER_DISCONNECTED ) {
                    pr_debug( "Updating player disconected:%d", update.player_id );
                    world.multiplayer_avatars.remove( update.player_id );
                } else {
                    pr_debug( "Saw unexpected packet:%d from:%d", update.type, update.player_id );
                }
            }
        }
    } else {
        // pr_debug( "Not this->active..." );
    }
}

void Multiplayer::send_packet( const NetPacket &update ) {
    // Send updates to the server
    int status = write( this->sockfd, ( void * )&update, sizeof( NetPacket ) );
    if ( status < 0 ) {
        pr_debug( "Unable to send message to socket" );
    }
}

void Multiplayer::set_block( const glm::ivec3 &block_pos, BlockState blockState ) {
    if ( !this->active ) {
        return;
    }
    // Log what the player is doing
    if ( blockState.id != AIR ) {
        // pr_debug( "Player placed %i at %i, %i, %i", blockState.id, block_x, block_y, block_z );
    } else {
        // pr_debug( "Player broke the block at %i, %i, %i", block_x, block_y, block_z );
    }
    NetPacket update;
    update.type = PacketType::BLOCK_UPDATE;
    update.data.block.x = block_pos.x;
    update.data.block.y = block_pos.y;
    update.data.block.z = block_pos.z;
    memcpy( &update.data.block.blockState, &blockState, sizeof( BlockState ) );

    this->send_packet( update );
}

void Multiplayer::request_chunk( const glm::ivec3 &chunk_pos ) {
    if ( !this->active ) {
        return;
    }
    NetPacket update;
    update.type = PacketType::CHUNK_DIFF_REQUEST;
    update.data.chunk_diff.chunk_x = chunk_pos.x;
    update.data.chunk_diff.chunk_y = chunk_pos.y;
    update.data.chunk_diff.chunk_z = chunk_pos.z;
    this->send_packet( update );
}

glm::vec3 prev_player_pos;
glm::mat4 prev_rotation;

void Multiplayer::update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation ) {
    if ( !this->active ) {
        return;
    }
    if ( prev_player_pos == player_pos && prev_rotation == rotation ) {
        return;
    }
    prev_player_pos = player_pos;
    prev_rotation = rotation;
    NetPacket update;
    update.type = PacketType::PLAYER_LOCATION;
    update.data.player.x = player_pos.x;
    update.data.player.y = player_pos.y;
    update.data.player.z = player_pos.z;
    memcpy( update.data.player.rotation, ( float * )glm::value_ptr( rotation ), sizeof( glm::mat4 ) );

    this->send_packet( update );
}

void Multiplayer::cleanup( ) {
    pr_debug( "closing multiplayer" );
    if ( this->active ) {
        close( this->sockfd );
        this->active = false;
    }
}