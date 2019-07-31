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
#include "common/net/multiplayer.hpp"

int sockfd;
int portno;
int n;

char block_recv_buffer[ 256 ];

bool active = false;

void multiplayer_init( const char *hostname, int port ) {
    pr_debug( "using server %s:%i", hostname, port );

    struct sockaddr_in serv_addr;
    struct hostent *server;

    portno = port;
    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( sockfd < 0 ) {
        fprintf( stderr, "Unable to allocate socket \n" );
        return;
    }
    server = gethostbyname( hostname );
    if ( server == NULL ) {
        fprintf( stderr, "Unable to get hostname from string \n" );
        return;
    }

    bzero( ( char * )&serv_addr, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;

    bcopy( ( char * )server->h_addr, ( char * )&serv_addr.sin_addr.s_addr, server->h_length );

    serv_addr.sin_port = htons( portno );
    if ( connect( sockfd, ( struct sockaddr * )&serv_addr, sizeof( serv_addr ) ) < 0 ) {
        pr_debug( "Multiplayer failed to eonnect" );
        return;
    } else {
        // We connected!
        active = true;

        int flags = fcntl( sockfd, F_GETFL );
        int status = fcntl( sockfd, F_SETFL, flags | O_NONBLOCK );
        if ( status < 0 ) {
            fprintf( stderr, "Unable to set non-blocking \n" );
            return;
        }
    }
}

void multiplayer_process_events( World *world ) {
    if ( active ) {
        while ( true ) {
            // Send updates to the server
            NetPacket update;
            int status = read( sockfd, &update, sizeof( NetPacket ) );
            if ( status < 0 ) {
                // This is fine, it just means there are no messages;
                // pr_debug( "Unable to read message from socket" );
                return;
            } else {
                if ( update.type == BLOCK_UPDATE ) {
                    pr_debug( "Read message: block:%d", update.data.block.blockID );
                    world_set_loaded_block( world, update.data.block.x, update.data.block.y, update.data.block.z, ( BlockID )update.data.block.blockID );
                } else if ( update.type == PLAYER_LOCATION ) {
                    // pr_debug( "Updating player location:%d", update.player_id );
                    glm::mat4 rotation = glm::make_mat4( update.data.player.rotation );
                    mobs_update_position( &world->mobs, update.player_id, update.data.player.x, update.data.player.y, update.data.player.z, rotation );
                } else if ( update.type == PLAYER_CONNECTED ) {
                    pr_debug( "Updating player connected:%d", update.player_id );
                    mobs_add_mob( &world->mobs, update.player_id );
                } else if ( update.type == PLAYER_DISCONNECTED ) {
                    pr_debug( "Updating player disconected:%d", update.player_id );
                    mobs_remove_mob( &world->mobs, update.player_id );
                } else {
                    pr_debug( "Saw unexpected packet:%d from:%d", update.type, update.player_id );
                }
            }
        }
    } else {
        // pr_debug( "Not active..." );
    }
}

void multiplayer_set_block_send_packet( NetPacket *update ) {
    // Send updates to the server
    int status = write( sockfd, ( void * )update, sizeof( NetPacket ) );
    if ( status < 0 ) {
        pr_debug( "Unable to send message to socket" );
    }
}

void multiplayer_set_block( int place, int block_x, int block_y, int block_z, BlockID blockID ) {
    if ( active ) {
        // Log what the player is doing
        if ( place ) {
            pr_debug( "Player placed %i at %i, %i, %i", blockID, block_x, block_y, block_z );
        } else {
            pr_debug( "Player broke the block at %i, %i, %i", block_x, block_y, block_z );
        }
        NetPacket update;
        update.type = BLOCK_UPDATE;
        update.data.block.x = block_x;
        update.data.block.y = block_y;
        update.data.block.z = block_z;
        update.data.block.blockID = blockID;

        multiplayer_set_block_send_packet( &update );
    }
}

void multiplayer_update_players_position( float player_x, float player_y, float player_z, glm::mat4 &rotation ) {
    if ( active ) {
        NetPacket update;
        update.type = PLAYER_LOCATION;
        update.data.player.x = player_x;
        update.data.player.y = player_y;
        update.data.player.z = player_z;
        memcpy( update.data.player.rotation, ( float * )glm::value_ptr( rotation ), sizeof( glm::mat4 ) );

        multiplayer_set_block_send_packet( &update );
    }
}

void multiplayer_cleanup( ) {
    pr_debug( "closing multiplayer" );
    if ( active ) {
        close( sockfd );
        active = false;
    }
}