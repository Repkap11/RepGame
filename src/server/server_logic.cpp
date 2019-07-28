
#include "server/server_logic.hpp"
#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void server_logic_on_client_connected( int client_fd ) {
    pr_debug( "%d", client_fd );

    // Tell the other clients about this new client
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            // Don't tell this client that itself joined.
            continue;
        }
        if ( server_is_client_connected( prop_id ) ) {
            pr_debug( "Notifying existing player:%d of new player:%d", prop_id, client_fd );
            NetPacket packet;
            // pr_debug( "Notifying2 existing player:%d of new player:%d", prop_id, client_fd );
            packet.type = PLAYER_CONNECTED;
            // pr_debug( "Notifying3 existing player:%d of new player:%d", prop_id, client_fd );
            packet.player_id = client_fd;
            // pr_debug( "Notifying4 existing player:%d of new player:%d", prop_id, client_fd );
            server_queue_packet( prop_id, packet );
            // pr_debug( "Notifying5 existing player:%d of new player:%d", prop_id, client_fd );
        }
    }

    // Tell this client about the other clients already online
    for ( int online_id = 0; online_id < MAX_CLIENT_FDS; online_id++ ) {
        if ( online_id == client_fd ) {
            // Don't tell this client that itself joined.
            continue;
        }
        if ( server_is_client_connected( online_id ) ) {
            NetPacket packet;
            packet.type = PLAYER_CONNECTED;
            packet.player_id = online_id;
            server_queue_packet( client_fd, packet );
            pr_debug( "Notifying new player:%d of existing player:%d", client_fd, online_id );
        }
    }
}

void server_logic_on_client_message( int client_fd, NetPacket &packet ) {
    // pr_debug( "Got message from:%d block:%d", client_fd, packet->blockID );
    if ( packet.type != INVALID ) {
        // Tell the other connected players about most types of messages, but mark the
        // packet as from the player that sent it.

        // Tell the other clients that this client disconnected.
        for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
            if ( prop_id == client_fd ) {
                // Don't tell this client that itself joined.
                continue;
            }
            if ( server_is_client_connected( prop_id ) ) {
                packet.player_id = client_fd;
                server_queue_packet( prop_id, packet );
            }
        }
    }
}

void server_logic_on_client_disconnected( int client_fd ) {
    //pr_debug( "%d", client_fd );
    // Tell the other clients that this client disconnected.
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            // Don't tell this client that itself joined.
            continue;
        }
        if ( server_is_client_connected( prop_id ) ) {
            NetPacket packet;
            packet.type = PLAYER_DISCONNECTED;
            packet.player_id = client_fd;
            server_queue_packet( prop_id, packet );
            pr_debug( "Notifying existing player:%d of removed player:%d", prop_id, client_fd );
        }
    }
}
