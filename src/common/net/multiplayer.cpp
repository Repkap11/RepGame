#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <assert.h>
#include <string.h>
#include <ctime>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"

int s = -1;
char buf[ 10000 ];

void multiplayer_init( ) {
    pr_debug( "init_multicast - starting broadcast connections" );

    sockaddr_in si_me, si_other;

    s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if ( s == -1 ) {
        return;
    }

    int port = 4448;
    int broadcast = 1;

    setsockopt( s, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast );

    memset( &si_me, 0, sizeof( si_me ) );
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons( port );
    si_me.sin_addr.s_addr = INADDR_ANY;

    // Bind to the wildcard ip on whatever port so we get everything
    assert(::bind( s, ( sockaddr * )&si_me, sizeof( sockaddr ) ) != -1 );

    printf( "Send message to broadcast\n" );
    strcpy( buf, "client_init" );
    unsigned slen = sizeof( sockaddr );
    send( s, buf, sizeof( buf ) - 1, 0 );
}

void broadcast_chunk_update( int chunk_x, int chunk_y, int chunk_z, int block_x, int block_y, int block_z, BlockID blockID ) {
    if ( s != -1 ) {
        pr_debug( "mrepka: Chunk: %i, %i, %i, Block: %i, %i, %i is set to blockid: %i", chunk_x, chunk_y, chunk_z, block_x, block_y, block_z, blockID );
        sprintf( buf, "%i,%i,%i,%i,%i,%i,%i", chunk_x, chunk_y, chunk_z, block_x, block_y, block_z, blockID );
        send( s, buf, sizeof( buf ) - 1, 0 );
    }
}

void multiplayer_cleanup( ) {
    pr_debug( "mrepka: Cleaning up multiplayer udp multicast socket" );
    s = -1;
}