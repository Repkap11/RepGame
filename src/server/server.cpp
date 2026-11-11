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

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "server/server.hpp"
#include "server/server_logic.hpp"
#include "common/net/mpload_trace.hpp"
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
    clientData.player_data.eye_height = PLAYER_EYE_HEIGHT;
    clientData.socket.adopt( client_fd );
    this->server_logic.on_client_connected( *this, client_fd );
}

// MPLOAD trace (REPGAME_MPLOAD_TRACE): inbound accounting drained once per
// second by Server::serve into a srv_stats line. Static file scope — the
// read loop and the epoll loop both live here.
static int mpload_in_frames = 0;
static std::map<NetMsgType, int> mpload_in_by_type;

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
        if ( mpload_trace_enabled( ) ) {
            mpload_in_frames++;
            mpload_in_by_type[ type ]++;
        }
        if ( version != NET_PROTOCOL_VERSION ) {
            pr_debug( "Protocol version mismatch: client_fd %d got %u expected %u", client_fd, version, NET_PROTOCOL_VERSION );
            disconnected = 1;
            break;
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
                client_data[ client_fd ].player_data.eye_height = p.eye_height;
                client_data[ client_fd ].player_data.flags = p.flags;
                client_data[ client_fd ].player_data.health = p.health;
                client_data[ client_fd ].player_data.held_item = p.held_item;
            }
        }

        if ( net_trace_enabled( ) ) {
            pr_debug( "net_in  %s fd=%d n=%zu", net_msg_type_name( type ), client_fd, payload->size( ) );
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
    if ( net_trace_enabled( ) ) {
        pr_debug( "net_out %s fd=%d n=%zu", net_msg_type_name( type ), client_fd, payload.size( ) );
    }
    this->client_data[ client_fd ].socket.send_message( type, player_id, payload );
    this->update_epoll( client_fd );
}

void Server::queue_empty( int client_fd, NetMsgType type, int32_t player_id ) {
    if ( net_trace_enabled( ) ) {
        pr_debug( "net_out %s fd=%d (empty)", net_msg_type_name( type ), client_fd );
    }
    this->client_data[ client_fd ].socket.send_empty( type, player_id );
    this->update_epoll( client_fd );
}

void Server::broadcast( NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload, int except_fd ) {
    for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
        if ( fd != except_fd && this->get_data_if_client_connected( fd ) != NULL ) {
            this->queue_message( fd, type, player_id, payload );
        }
    }
}

void Server::broadcast_to( const std::set<int> &fds, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload, int except_fd ) {
    for ( int fd : fds ) {
        if ( fd != except_fd && fd >= 0 && fd < MAX_CLIENT_FDS &&
             this->get_data_if_client_connected( fd ) != NULL ) {
            this->queue_message( fd, type, player_id, payload );
        }
    }
}

void Server::broadcast_empty( NetMsgType type, int32_t player_id, int except_fd ) {
    for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
        if ( fd != except_fd && this->get_data_if_client_connected( fd ) != NULL ) {
            this->queue_empty( fd, type, player_id );
        }
    }
}

PacketType_DataPlayer *Server::get_data_if_client_connected( int client_id ) {
    if ( !this->client_data[ client_id ].connected ) {
        return NULL;
    }
    return &this->client_data[ client_id ].player_data;
}

void Server::handle_client_ready_for_write( int client_fd ) {
    FramedSocket &socket = this->client_data[ client_fd ].socket;
    const bool trace = mpload_trace_enabled( );
    const size_t queued_before = trace ? socket.outbound_frames( ) : 0;
    const double flush_t0 = trace ? mpload_now_ms( ) : 0.0;
    socket.flush( );
    if ( trace ) {
        const size_t left = socket.outbound_frames( );
        const size_t sent = queued_before > left ? queued_before - left : 0;
        if ( sent > 0 || left > 0 ) {
            mpload_tracef( "srv_flush fd=%d sent_frames=%zu left=%zu us=%lld",
                           client_fd, sent, left,
                           ( long long )( ( mpload_now_ms( ) - flush_t0 ) * 1000.0 ) );
        }
    }
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

void Server::init( int portnum, const char *world_name ) {
    this->server_logic.init( world_name );
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
    const bool trace = mpload_trace_enabled( );
    double last_stats_ms = 0.0;
    double last_mem_ms = 0.0;
    long loop_iters = 0;
    while ( this->epoll_fd >= 0 ) {
        loop_iters++;
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
        this->server_logic.processTimers( *this );
        if ( trace ) {
            const double now_ms = mpload_now_ms( );
            if ( now_ms - last_stats_ms >= 1000.0 ) {
                bool any_client = false;
                for ( int fd = 0; fd < MAX_CLIENT_FDS && !any_client; fd++ ) {
                    any_client = this->client_data[ fd ].connected;
                }
                if ( any_client ) {
                    // Top 5 inbound frame types by count this second.
                    std::vector<std::pair<NetMsgType, int>> by_type( mpload_in_by_type.begin( ), mpload_in_by_type.end( ) );
                    std::sort( by_type.begin( ), by_type.end( ), []( const auto &a, const auto &b ) { return a.second > b.second; } );
                    std::string list;
                    for ( size_t i = 0; i < by_type.size( ) && i < 5; i++ ) {
                        char entry[ 64 ];
                        snprintf( entry, sizeof( entry ), "%s%s:%d", i ? "," : "",
                                  net_msg_type_name( by_type[ i ].first ), by_type[ i ].second );
                        list += entry;
                    }
                    mpload_tracef( "srv_stats in_frames=%d in_by_type=%s loop_iters=%ld",
                                   mpload_in_frames, list.c_str( ), loop_iters );
                }
                mpload_in_frames = 0;
                mpload_in_by_type.clear( );
                loop_iters = 0;
                last_stats_ms = now_ms;
            }
            // Slower cadence: memory/dirty-state snapshot every 10 s
            // (file-index and diff-cache fields join this line in later
            // phases).
            if ( now_ms - last_mem_ms >= 10000.0 ) {
                long rss_kb = -1;
                FILE *statm = fopen( "/proc/self/statm", "r" );
                if ( statm != nullptr ) {
                    long long pages = 0;
                    if ( fscanf( statm, "%*lld %lld", &pages ) == 1 ) {
                        rss_kb = static_cast<long>( pages * sysconf( _SC_PAGESIZE ) / 1024 );
                    }
                    fclose( statm );
                }
                mpload_tracef( "srv_mem rss_kb=%ld world_cache=%zu dirty_cells=%zu known_empty=%zu",
                               rss_kb, this->server_logic.worldCacheChunks( ),
                               this->server_logic.worldCacheDirtyCells( ),
                               this->server_logic.knownEmptyCount( ) );
                last_mem_ms = now_ms;
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

int server_selftest( );

int main( int argc, const char **argv ) {
    int portnum = 25566;
    if ( argc >= 2 ) {
        if ( strcmp( argv[ 1 ], "--selftest" ) == 0 ) {
            return server_selftest( );
        }
        portnum = atoi( argv[ 1 ] );
    }
    pr_debug( "Serving on port %d", portnum );

    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = sig_handler;
    sigemptyset( &sigIntHandler.sa_mask );
    sigIntHandler.sa_flags = 0;

    sigaction( SIGINT, &sigIntHandler, NULL );
    // systemd stop/restart sends SIGTERM (and a dying terminal sends SIGHUP):
    // route both through the same graceful shutdown so flushAll runs.
    sigaction( SIGTERM, &sigIntHandler, NULL );
    sigaction( SIGHUP, &sigIntHandler, NULL );
    // A peer vanishing between the last poll and the next flush must not kill
    // the server — sends already pass MSG_NOSIGNAL; this covers the rest.
    signal( SIGPIPE, SIG_IGN );

    server.init( portnum );
    server.serve( );
    server.cleanup( );
}
