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
    // pr_debug( "Epoll add:%d", client_fd );
    return;
}

void Server::del_epoll( int client_fd ) {
    if ( epoll_ctl( this->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL ) < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_DEL" );
    }
    // pr_debug( "Epoll del:%d", client_fd );
    return;
}
void Server::update_epoll( int client_fd ) {
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
    int status = epoll_ctl( this->epoll_fd, EPOLL_CTL_MOD, client_fd, &event );
    if ( status < 0 ) {
        pr_debug( "epoll_ctl EPOLL_CTL_MOD status:%d:%s", status, strerror( status ) );
    }
    // pr_debug( "About to epoll_ctl mod done" );
}

void Server::handle_new_client_event( int inet_socket_fd ) {
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
    Server::make_socket_non_blocking( client_fd );
    Server::add_epoll( client_fd );
    ClientData &clientData = client_data[ client_fd ];
    clientData.connected = 1;
    clientData.pending_receive_len = 0;
    clientData.pending_send_len = 0;
    this->server_logic.on_client_connected( *this, client_fd );
}

void Server::handle_client_ready_for_read( int client_fd ) {
    // pr_debug( "server_handle_client_ready_for_read" );
    int disconnected = 0;
    while ( true ) {
        NetPacket &packet = client_data[ client_fd ].pending_receive;
        int &pending_receive_len = client_data[ client_fd ].pending_receive_len;
        char *recv_start = ( ( char * )&packet ) + pending_receive_len;
        size_t size_needed = sizeof( NetPacket ) - pending_receive_len;
        int nbytes = recv( client_fd, recv_start, size_needed, 0 );
        if ( nbytes == 0 ) {
            // The client disconnected.
            disconnected = 1;
            break;
        } else if ( nbytes < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // The socket is not *really* ready for recv; wait until it is.
                break;
            }
            pr_debug( "recv got negitive bytes:%d, darn errno:%d", nbytes, errno );
            break;
        }
        pending_receive_len += nbytes;
        if ( pending_receive_len != sizeof( NetPacket ) ) {
            continue;
        }
        pending_receive_len = 0;
        if ( packet.type == PLAYER_LOCATION || packet.type == PLAYER_CONNECTED ) {
            client_data[ client_fd ].player_data = packet.data.player;
        }
        this->server_logic.on_client_message( *this, client_fd, &packet );
    }
    if ( disconnected ) {
        std::queue<NetPacket> empty;
        client_data[ client_fd ].pending_sends.swap( empty );
        this->server_logic.on_client_disconnected( *this, client_fd );
        client_data[ client_fd ].connected = 0;
        Server::del_epoll( client_fd );
        close( client_fd );
    } else {
        Server::update_epoll( client_fd );
    }

    return;
}

void Server::queue_packet( int client_fd, NetPacket *packet ) {
    this->client_data[ client_fd ].pending_sends.push( *packet );
    // pr_debug( "Queueing packet on %d length:%ld", client_fd, client_data[ client_fd ].pending_sends.size( ) );
    this->update_epoll( client_fd );
    // pr_debug( "Got packet done with server_update_epoll" );
}

PacketType_DataPlayer *Server::get_data_if_client_connected( int client_id ) {
    if ( !this->client_data[ client_id ].connected ) {
        return NULL;
    }
    return &this->client_data[ client_id ].player_data;
}

void Server::handle_client_ready_for_write( int client_fd ) {
    // pr_debug( "server_handle_client_ready_for_write" );
    std::queue<NetPacket> &pending_sends = this->client_data[ client_fd ].pending_sends;
    int &pending_send_len = this->client_data[ client_fd ].pending_send_len;
    while ( !pending_sends.empty( ) ) {
        NetPacket &packet = pending_sends.front( );
        char *send_start = ( ( char * )&packet ) + pending_send_len;
        size_t size_needed = sizeof( NetPacket ) - pending_send_len;

        int nsent = send( client_fd, send_start, size_needed, 0 );
        if ( nsent < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                break;
            } else {
                pr_debug( "send" );
            }
        }
        pending_send_len += nsent;
        if ( pending_send_len < ( int )sizeof( NetPacket ) ) {
            continue;
        }
        pending_sends.pop( );
        pending_send_len = 0;
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

    this->client_data = new ClientData[ MAX_CLIENT_FDS ];
    this->events = new struct epoll_event[ MAX_CLIENT_FDS ];
    if ( events == NULL ) {
        pr_debug( "Unable to allocate memory for epoll_events" );
    }
}

void Server::cleanup( ) {
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
        int num_ready = epoll_wait( this->epoll_fd, this->events, MAX_CLIENT_FDS, -1 );
        // pr_debug( "epoll returned: %d", num_ready );
        for ( int i = 0; i < num_ready; i++ ) {
            int client_fd = this->events[ i ].data.fd;
            if ( this->events[ i ].events & EPOLLERR ) {
                pr_debug( "epoll_wait returned EPOLLERR:%d", client_fd );
                std::queue<NetPacket> empty;
                // std::swap( client_data[ client_fd ].pending_sends, empty );
                client_data[ client_fd ].pending_sends.swap( empty );
                this->server_logic.on_client_disconnected( *this, client_fd );
                client_data[ client_fd ].connected = 0;
                Server::del_epoll( client_fd );
                close( client_fd );
                continue;
            }

            if ( client_fd == inet_socket_fd ) {
                // The listening socket is ready; this means a new peer is connecting.
                this->handle_new_client_event( inet_socket_fd );
            } else {
                uint32_t epoll_events = events[ i ].events;
                // A peer socket is ready.
                if ( epoll_events & EPOLLIN ) {
                    // Ready for reading.
                    this->handle_client_ready_for_read( client_fd );
                } else if ( epoll_events & EPOLLOUT ) {
                    // Ready for writing.
                    this->handle_client_ready_for_write( client_fd );
                }
            }
        }
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
    // setvbuf( stdout, NULL, _IONBF, 0 );

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