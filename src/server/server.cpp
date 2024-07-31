#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "server/server.hpp"
#include "server/server_logic.hpp"

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

#define INET_SOCKET_BACKLOG 64

int epoll_fd = 0;

ClientData *client_data;

int server_setup_inet_socket( int port ) {
    int sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( sockfd < 0 ) {
        pr_debug( "ERROR opening socket" );
    }

    // This helps avoid spurious EADDRINUSE when the previous instance of this server died.
    int opt = 1;
    if ( setsockopt( sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ) ) < 0 ) {
        pr_debug( "setsockopt" );
    }

    struct sockaddr_in serv_addr;
    memset( &serv_addr, 0, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons( port );

    if ( bind( sockfd, ( struct sockaddr * )&serv_addr, sizeof( serv_addr ) ) < 0 ) {
        pr_debug( "ERROR on binding" );
    }
    if ( listen( sockfd, INET_SOCKET_BACKLOG ) < 0 ) {
        pr_debug( "ERROR on listen" );
    }
    return sockfd;
}

void make_socket_non_blocking( int sockfd ) {
    int flags = fcntl( sockfd, F_GETFL, 0 );
    if ( flags == -1 ) {
        pr_debug( "fcntl F_GETFL" );
    }

    if ( fcntl( sockfd, F_SETFL, flags | O_NONBLOCK ) == -1 ) {
        pr_debug( "fcntl F_SETFL O_NONBLOCK" );
    }
}

void server_add_epoll( int client_fd ) {
    struct epoll_event accept_event;
    accept_event.data.fd = client_fd;
    accept_event.events = EPOLLOUT | EPOLLET;
    if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, client_fd, &accept_event ) < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_ADD" );
    }
    // pr_debug( "Epoll add:%d", client_fd );
    return;
}

void server_del_epoll( int client_fd ) {
    if ( epoll_ctl( epoll_fd, EPOLL_CTL_DEL, client_fd, NULL ) < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_DEL" );
    }
    // pr_debug( "Epoll del:%d", client_fd );
    return;
}
void server_update_epoll( int client_fd ) {
    struct epoll_event event = { 0, { 0 } };
    event.data.fd = client_fd;
    event.events = EPOLLET;
    int size = client_data[ client_fd ].pending_sends.size( );
    if ( size == 1 ) {
        event.events |= EPOLLOUT;
        // pr_debug( "Update to out" );
    } else if ( size == 0 ) {
        event.events |= EPOLLIN;
        // pr_debug( "Update to in" );
    } else {
        return;
    }
    // pr_debug( "About to epoll_ctl mod" );
    int status = epoll_ctl( epoll_fd, EPOLL_CTL_MOD, client_fd, &event );
    if ( status < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_MOD status:%d:%s", status, strerror( status ) );
    }
    // pr_debug( "About to epoll_ctl mod done" );
}

void server_handle_new_client_event( int inet_socket_fd ) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof( client_addr );
    int client_fd = accept( inet_socket_fd, ( struct sockaddr * )&client_addr, &client_addr_len );
    if ( client_fd < 0 ) {
        if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
            // This can happen due to the nonblocking socket mode; in this
            // case don't do anything, but print a notice (since these events
            // are extremely rare and interesting to observe...)
            pr_debug( "accept returned EAGAIN or EWOULDBLOCK" );
        }
        pr_debug( "Failed to get client FD" );
        return;
    }
    if ( client_fd >= MAX_CLIENT_FDS ) {
        pr_debug( "socket fd (%d) >= MAX 0NT_FDS (%d)", client_fd, MAX_CLIENT_FDS );
    }
    make_socket_non_blocking( client_fd );
    server_add_epoll( client_fd );
    client_data[ client_fd ].connected = 1;
    server_logic_on_client_connected( client_fd );
}

void server_handle_client_ready_for_read( int client_fd ) {
    // pr_debug( "server_handle_client_ready_for_read" );
    int disconnected = 0;
    while ( true ) {
        NetPacket packet;
        int nbytes = recv( client_fd, &packet, sizeof( NetPacket ), 0 );
        if ( nbytes == 0 ) {
            // The client disconnected.
            disconnected = 1;
            break;
        } else if ( nbytes < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // The socket is not *really* ready for recv; wait until it is.
                break;
            }
            pr_debug( "recv got negitive bytes, darn" );
            break;
        } else if ( nbytes != sizeof( NetPacket ) ) {
            pr_debug( "Only got a partial packet, darn" );
            break;
        }
        if ( packet.type == PLAYER_LOCATION || packet.type == PLAYER_CONNECTED ) {
            client_data[ client_fd ].player_data = packet.data.player;
        }
        server_logic_on_client_message( client_fd, &packet );
    }
    if ( disconnected ) {
        std::queue<NetPacket> empty;
        std::swap( client_data[ client_fd ].pending_sends, empty );
        server_logic_on_client_disconnected( client_fd );
        client_data[ client_fd ].connected = 0;
        server_del_epoll( client_fd );
        close( client_fd );
    } else {
        server_update_epoll( client_fd );
    }

    return;
}

void server_queue_packet( int client_fd, NetPacket *packet ) {
    client_data[ client_fd ].pending_sends.push( *packet );
    // pr_debug( "Queueing packet on %d length:%ld", client_fd, client_data[ client_fd ].pending_sends.size( ) );
    server_update_epoll( client_fd );
    // pr_debug( "Got packet done with server_update_epoll" );
}

PacketType_DataPlayer *server_get_data_if_client_connected( int client_id ) {
    if ( !client_data[ client_id ].connected ) {
        return NULL;
    }
    return &client_data[ client_id ].player_data;
}

void server_handle_client_ready_for_write( int client_fd ) {
    // pr_debug( "server_handle_client_ready_for_write" );
    std::queue<NetPacket> &pending_sends = client_data[ client_fd ].pending_sends;
    while ( !pending_sends.empty( ) ) {
        NetPacket &packet = pending_sends.front( );
        pending_sends.pop( );
        int nsent = send( client_fd, &packet, sizeof( NetPacket ), 0 );
        if ( nsent == -1 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                break;
            } else {
                pr_debug( "send" );
            }
        }
        if ( nsent < ( int )sizeof( NetPacket ) ) {
            pr_debug( "Sent less than a packet" );
            break;
        }
    }
    server_update_epoll( client_fd );
}

int main( int argc, const char **argv ) {
    // setvbuf( stdout, NULL, _IONBF, 0 );

    int portnum = 25566;
    if ( argc >= 2 ) {
        portnum = atoi( argv[ 1 ] );
    }
    pr_debug( "Serving on port %d", portnum );

    int inet_socket_fd = server_setup_inet_socket( portnum );

    make_socket_non_blocking( inet_socket_fd );

    epoll_fd = epoll_create1( 0 );
    if ( epoll_fd < 0 ) {
        pr_debug( "epoll_create1" );
    }

    struct epoll_event accept_event;
    accept_event.data.fd = inet_socket_fd;
    accept_event.events = EPOLLIN;
    if ( epoll_ctl( epoll_fd, EPOLL_CTL_ADD, inet_socket_fd, &accept_event ) < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_ADD" );
    }

    client_data = ( ClientData * )calloc( MAX_CLIENT_FDS, sizeof( ClientData ) );
    for ( int i = 0; i < MAX_CLIENT_FDS; i++ ) {
        new ( &client_data[ i ].pending_sends ) std::queue<NetPacket>( );
        // client_data[ i ].pending_sends = std::queue<NetPacket>( );
    }
    struct epoll_event *events = ( struct epoll_event * )calloc( MAX_CLIENT_FDS, sizeof( struct epoll_event ) );
    if ( events == NULL ) {
        pr_debug( "Unable to allocate memory for epoll_events" );
    }
    while ( 1 ) {
        int num_ready = epoll_wait( epoll_fd, events, MAX_CLIENT_FDS, -1 );
        // pr_debug( "epoll returned: %d", num_ready );
        for ( int i = 0; i < num_ready; i++ ) {
            int client_fd = events[ i ].data.fd;
            if ( events[ i ].events & EPOLLERR ) {
                pr_debug( "epoll_wait returned EPOLLERR:%d", client_fd );
            }

            if ( client_fd == inet_socket_fd ) {
                // The listening socket is ready; this means a new peer is connecting.
                server_handle_new_client_event( inet_socket_fd );
            } else {
                uint32_t epoll_events = events[ i ].events;
                // A peer socket is ready.
                if ( epoll_events & EPOLLIN ) {
                    // Ready for reading.
                    server_handle_client_ready_for_read( client_fd );
                } else if ( epoll_events & EPOLLOUT ) {
                    // Ready for writing.
                    server_handle_client_ready_for_write( client_fd );
                }
            }
        }
    }
}