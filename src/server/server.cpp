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
#include "common/net/packet.hpp"

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

#define INET_SOCKET_BACKLOG 64

int Server::setup_inet_socket( int port ) {
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

void Server::make_socket_non_blocking( int sockfd ) {
    int flags = fcntl( sockfd, F_GETFL, 0 );
    if ( flags == -1 ) {
        pr_debug( "fcntl F_GETFL" );
    }

    if ( fcntl( sockfd, F_SETFL, flags | O_NONBLOCK ) == -1 ) {
        pr_debug( "fcntl F_SETFL O_NONBLOCK" );
    }
}

void Server::add_epoll( int client_fd ) {
    struct epoll_event accept_event;
    accept_event.data.fd = client_fd;
    accept_event.events = EPOLLOUT | EPOLLET;
    if ( epoll_ctl( this->epoll_fd, EPOLL_CTL_ADD, client_fd, &accept_event ) < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_ADD" );
    }
    return;
}

void Server::del_epoll( int client_fd ) {
    if ( epoll_ctl( this->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL ) < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_DEL" );
    }
    return;
}
void Server::update_epoll( int client_fd ) {
    struct epoll_event event = { 0, { 0 } };
    event.data.fd = client_fd;
    event.events = EPOLLET;
    bool has_outbound = this->client_data[ client_fd ].socket.has_outbound( );
    if ( has_outbound ) {
        event.events |= EPOLLOUT;
    } else {
        event.events |= EPOLLIN;
    }
    int status = epoll_ctl( this->epoll_fd, EPOLL_CTL_MOD, client_fd, &event );
    if ( status < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_MOD status:%d:%s", status, strerror( status ) );
    }
}

void Server::handle_new_client_event( int inet_socket_fd ) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof( client_addr );
    int client_fd = accept( inet_socket_fd, ( struct sockaddr * )&client_addr, &client_addr_len );
    if ( client_fd < 0 ) {
        if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
            pr_debug( "accept returned EAGAIN or EWOULDBLOCK" );
        }
        pr_debug( "Failed to get client FD" );
        return;
    }
    if ( client_fd >= MAX_CLIENT_FDS ) {
        pr_debug( "socket fd (%d) >= MAX_CLIENT_FDS (%d)", client_fd, MAX_CLIENT_FDS );
        close( client_fd );
        return;
    }
    Server::make_socket_non_blocking( client_fd );
    Server::add_epoll( client_fd );
    ClientData &clientData = client_data[ client_fd ];
    clientData.connected = 1;
    clientData.socket.adopt( client_fd );
    this->server_logic.on_client_connected( *this, client_fd );
}

void Server::handle_client_ready_for_read( int client_fd ) {
    int disconnected = 0;
    while ( true ) {
        auto payload = this->client_data[ client_fd ].socket.recv_message( );
        if ( this->client_data[ client_fd ].socket.had_error( ) ) {
            // recv error or peer closed or bad frame.
            this->client_data[ client_fd ].socket.clear_error( );
            disconnected = 1;
            break;
        }
        if ( !payload ) {
            // No more complete frames right now.
            break;
        }
        // Parse the fixed payload header: version(1) type(1) player_id(4)
        if ( payload->size( ) < 6 ) {
            pr_debug( "Received too-short frame payload: %zu", payload->size( ) );
            continue;
        }
        const uint8_t version = ( *payload )[ 0 ];
        const NetMsgType type = static_cast<NetMsgType>( ( *payload )[ 1 ] );
        const int32_t player_id = static_cast<int32_t>(
            static_cast<uint32_t>( ( *payload )[ 2 ] )
            | ( static_cast<uint32_t>( ( *payload )[ 3 ] ) << 8 )
            | ( static_cast<uint32_t>( ( *payload )[ 4 ] ) << 16 )
            | ( static_cast<uint32_t>( ( *payload )[ 5 ] ) << 24 ) );
        if ( version != NET_PROTOCOL_VERSION ) {
            pr_debug( "Protocol version mismatch: got %u expected %u", version, NET_PROTOCOL_VERSION );
            continue;
        }

        // Store player position for PLAYER_LOCATION so on_client_connected can
        // send it to new joiners.
        if ( type == NetMsgType::PLAYER_LOCATION ) {
            PacketReader r( payload->data( ) + 6, payload->size( ) - 6 );
            NetPlayerPayload p;
            if ( net_deserialize_player( r, p ) ) {
                client_data[ client_fd ].player_data.x = p.x;
                client_data[ client_fd ].player_data.y = p.y;
                client_data[ client_fd ].player_data.z = p.z;
                memcpy( client_data[ client_fd ].player_data.rotation, p.rotation, sizeof( p.rotation ) );
            }
        }

        std::vector<uint8_t> type_payload( payload->begin( ) + 6, payload->end( ) );
        this->server_logic.on_client_message( *this, client_fd, type, player_id, type_payload );
    }
    if ( disconnected ) {
        this->server_logic.on_client_disconnected( *this, client_fd );
        client_data[ client_fd ].connected = 0;
        Server::del_epoll( client_fd );
        close( client_fd );
    } else {
        Server::update_epoll( client_fd );
    }
}

void Server::queue_message( int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload ) {
    this->client_data[ client_fd ].socket.send_message( type, player_id, payload );
    this->update_epoll( client_fd );
}

void Server::queue_empty( int client_fd, NetMsgType type, int32_t player_id ) {
    this->client_data[ client_fd ].socket.send_empty( type, player_id );
    this->update_epoll( client_fd );
}

PacketType_DataPlayer *Server::get_data_if_client_connected( int client_id ) {
    if ( !this->client_data[ client_id ].connected ) {
        return NULL;
    }
    return &this->client_data[ client_id ].player_data;
}

void Server::handle_client_ready_for_write( int client_fd ) {
    this->client_data[ client_fd ].socket.flush( );
    if ( this->client_data[ client_fd ].socket.had_error( ) ) {
        this->client_data[ client_fd ].socket.clear_error( );
        this->server_logic.on_client_disconnected( *this, client_fd );
        client_data[ client_fd ].connected = 0;
        Server::del_epoll( client_fd );
        close( client_fd );
        return;
    }
    this->update_epoll( client_fd );
}

void Server::init( int portnum ) {
    this->server_logic.init( "Server1" );
    this->inet_socket_fd = Server::setup_inet_socket( portnum );
    Server::make_socket_non_blocking( inet_socket_fd );

    this->epoll_fd = epoll_create1( 0 );
    if ( this->epoll_fd < 0 ) {
        pr_debug( "epoll_create1" );
    }

    struct epoll_event accept_event;
    accept_event.data.fd = inet_socket_fd;
    accept_event.events = EPOLLIN;
    if ( epoll_ctl( this->epoll_fd, EPOLL_CTL_ADD, inet_socket_fd, &accept_event ) < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_ADD" );
    }

    this->client_data = new ClientData[ MAX_CLIENT_FDS ]( );
    if ( this->client_data == NULL ) {
        pr_debug( "Unable to allocate memory for client_data" );
    }
    this->events = new struct epoll_event[ MAX_CLIENT_FDS ]( );
    if ( this->events == NULL ) {
        pr_debug( "Unable to allocate memory for epoll_events" );
    }
}

void Server::cleanup( ) {
    // Save any unsaved dirty chunks before shutting down (handles SIGINT).
    this->server_logic.flushAll( );
    if ( this->epoll_fd >= 0 ) {
        close( this->epoll_fd );
        this->epoll_fd = -1;
    }
    close( this->inet_socket_fd );
    delete[] this->client_data;
    delete[] this->events;
}

void Server::serve( ) {
    while ( this->epoll_fd >= 0 ) {
        int timeout_ms = this->server_logic.nextWakeTimeoutMs( );
        int num_ready = epoll_wait( this->epoll_fd, this->events, MAX_CLIENT_FDS, timeout_ms );
        for ( int i = 0; i < num_ready; i++ ) {
            int client_fd = this->events[ i ].data.fd;
            if ( this->events[ i ].events & EPOLLERR ) {
                pr_debug( "epoll_wait returned EPOLLERR:%d", client_fd );
                this->server_logic.on_client_disconnected( *this, client_fd );
                client_data[ client_fd ].connected = 0;
                Server::del_epoll( client_fd );
                close( client_fd );
                continue;
            }

            if ( client_fd == inet_socket_fd ) {
                this->handle_new_client_event( inet_socket_fd );
            } else {
                uint32_t epoll_events = events[ i ].events;
                if ( epoll_events & EPOLLIN ) {
                    this->handle_client_ready_for_read( client_fd );
                } else if ( epoll_events & EPOLLOUT ) {
                    this->handle_client_ready_for_write( client_fd );
                }
            }
        }
        // Process expired idle-save timers regardless of whether epoll
        // returned events or a timeout. This is cheap (O(k log n) where k
        // is the number of chunks whose timers just expired).
        this->server_logic.processTimers( );
    }
    pr_debug( "Server Exiting" );
}

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

void Server::kill( ) {
    int fd = this->epoll_fd;
    this->epoll_fd = -1;
    close( fd );
}

static Server server;

void sig_handler( int s ) {
    printf( "Caught signal %d\n", s );
    server.kill( );
}

int main( int argc, const char **argv ) {
    int portnum = 25566;
    if ( argc >= 2 ) {
        portnum = atoi( argv[ 1 ] );
    }
    pr_debug( "Serving on port %d", portnum );

    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = sig_handler;
    sigemptyset( &sigIntHandler.sa_mask );
    sigIntHandler.sa_flags = 0;

    sigaction( SIGINT, &sigIntHandler, NULL );

    server.init( portnum );
    server.serve( );
    server.cleanup( );
}
