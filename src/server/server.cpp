#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "server/server.hpp"
#include "common/net/multiplayer.hpp"

void dostuff( int ); /* function prototype */

void error( const char *msg ) {
    perror( msg );
    exit( 1 );
}

int main( int argc, char *argv[] ) {
    int sockfd, newsockfd, portno, pid;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( sockfd < 0 ) {
        error( "ERROR opening socket" );
    }

    bzero( ( char * )&serv_addr, sizeof( serv_addr ) );
    portno = atoi( "25566" );
    printf( "Starting RepGame Server on port %i \n", portno );

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons( portno );
    if ( bind( sockfd, ( struct sockaddr * )&serv_addr, sizeof( serv_addr ) ) < 0 )
        error( "ERROR on binding" );
    listen( sockfd, 5 );
    clilen = sizeof( cli_addr );
    while ( 1 ) {
        newsockfd = accept( sockfd, ( struct sockaddr * )&cli_addr, &clilen );
        if ( newsockfd < 0 )
            error( "ERROR on accept" );
        pid = fork( );
        if ( pid < 0 )
            error( "ERROR on fork" );
        if ( pid == 0 ) {
            close( sockfd );
            dostuff( newsockfd );
            exit( 0 );
        } else
            close( newsockfd );
    } /* end of while */
    close( sockfd );
    return 0; /* we never get here */
}

void dostuff( int sock ) {
    pr_debug( "Handler for socket %i", sock );
    long previous_sequence_nbr = -1;

    while ( 1 ) {
        NetPacket update;

        int status = read( sock, &update, sizeof( NetPacket ) );
        if ( status < 0 ) {
            error( "ERROR reading from socket" );
        }

        if ( previous_sequence_nbr != update.sequence ) {
            previous_sequence_nbr = update.sequence;
            if ( update.type == CLIENT_INIT ) {
                pr_debug( "CI: Client Connected!" );
            }

            if ( update.type == BLOCK_UPDATE ) {
                pr_debug( "BU: %i, %i, %i: %i", update.x, update.y, update.z, update.blockID );
            }

            if ( update.type == PLAYER_LOCATION ) {
                pr_debug( "PU: Player Update" );
            }
        }
    }
}