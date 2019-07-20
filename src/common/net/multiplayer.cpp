#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"

int sockfd;
int portno;
int n;

char block_send_buffer[ 256 ];
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

        return;
    } else {
        // We connected!
        active = true;
    }
}

void server_set_block( int place, int block_x, int block_y, int block_z, BlockID blockID ) {
    if ( active ) {
        // Log what the player is doing
        if ( place ) {
            pr_debug( "Player placed %i at %i, %i, %i", blockID, block_x, block_y, block_z );
        } else {
            pr_debug( "Player broke the block at %i, %i, %i", block_x, block_y, block_z );
        }

        // Send updates to the server
        bzero( block_send_buffer, 256 );
        sprintf( block_send_buffer, "%i,%i,%i:%i", block_x, block_y, block_z, blockID );
        int status = write( sockfd, block_send_buffer, strlen( block_send_buffer ) );
        if ( status < 0 ) {
            pr_debug( "Unable to send message to socket" );
        }

        bzero( block_send_buffer, 256 );
    }
}

void multiplayer_cleanup( ) {
    pr_debug( "closing multiplayer" );
    if ( active ) {
        close( sockfd );
        active = false;
    }
}