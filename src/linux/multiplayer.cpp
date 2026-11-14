// Native socket multiplayer transport, shared by Linux/ARM/SteamFrame and
// (via the socket_compat shim) the mingw Windows client.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/constants.hpp"
#include "common/multiplayer.hpp"
#include "common/net/mpload_trace.hpp"
#include "common/net/packet.hpp"
#include "common/net/socket_compat.hpp"

void Multiplayer::init( const char *hostname, const int port ) {
    pr_debug( "using server %s:%i", hostname, port );

    if ( !net_startup( ) ) {
        pr_debug( "Unable to initialize networking" );
        return;
    }

    this->active = false;
    this->portno = port;
    this->hostname = hostname;
    this->prev_player_pos = glm::vec3( 0.0f );
    this->prev_rotation = glm::mat4( 1.0f );
    this->mismatched_server_version = -1;

    // Create the socket on the main thread so we own its lifecycle. The
    // background connect thread uses this->sockfd but never closes it; on
    // shutdown cleanup() closes it to interrupt any pending wait.
    this->sockfd = net_socket_tcp( );
    if ( this->sockfd < 0 ) {
        pr_debug( "Unable to allocate socket" );
        this->sockfd = -1;
        return;
    }
    // Diff/block updates are latency-sensitive — skip Nagle.
    int opt = 1;
    if ( setsockopt( this->sockfd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>( &opt ), sizeof( opt ) ) < 0 ) {
        pr_debug( "setsockopt TCP_NODELAY: %s", net_strerror( net_last_error( ) ) );
    }

    // Perform DNS + nonblocking connect + poll on a background thread so a
    // down/unreachable server cannot stall startup.
    this->connect_pending = true;
    this->connect_thread = std::thread( &Multiplayer::connect_async, this, std::string( hostname ) );
}

void Multiplayer::connect_async( const std::string &hostname ) {
    // Cleared on every exit path (success or failure) so is_connecting()
    // only reads true while the attempt is actually in flight.
    struct ConnectPendingGuard {
        std::atomic<bool> &flag;
        ~ConnectPendingGuard( ) { flag = false; }
    } pending_guard{ this->connect_pending };

    struct addrinfo hints;
    struct addrinfo *result = nullptr;

    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai_status = getaddrinfo( hostname.c_str( ), nullptr, &hints, &result );
    if ( gai_status != 0 ) {
        pr_debug( "Unable to resolve hostname: %s", gai_strerror( gai_status ) );
        return;
    }

    struct sockaddr_in serv_addr;
    memset( &serv_addr, 0, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    memcpy( &serv_addr.sin_addr, &reinterpret_cast<struct sockaddr_in *>( result->ai_addr )->sin_addr, sizeof( serv_addr.sin_addr ) );
    serv_addr.sin_port = htons( this->portno );
    freeaddrinfo( result );

    // Make the socket nonblocking before connect so we can bound the wait.
    if ( !net_set_nonblocking( this->sockfd ) ) {
        pr_debug( "Unable to set non-blocking" );
        return;
    }

    int rc = connect( this->sockfd, reinterpret_cast<sockaddr *>( &serv_addr ), sizeof( serv_addr ) );
    if ( rc == 0 ) {
        // Immediate success (e.g. localhost).
        this->framed_socket.adopt( this->sockfd );
        this->active = true;
        this->ever_connected = true;
        return;
    }
    if ( rc < 0 && !net_err_connect_in_progress( net_last_error( ) ) ) {
        pr_debug( "Multiplayer failed to connect: %s", net_strerror( net_last_error( ) ) );
        return;
    }

    // Wait for the connect to complete, with a bounded timeout.
    int pr = net_wait_connect( this->sockfd, MULTIPLAYER_CONNECT_TIMEOUT_MS );
    if ( pr <= 0 ) {
        if ( pr == 0 ) {
            pr_debug( "Multiplayer failed to connect: timed out after %d ms", MULTIPLAYER_CONNECT_TIMEOUT_MS );
        } else {
            pr_debug( "Multiplayer failed to connect: wait error: %s", net_strerror( net_last_error( ) ) );
        }
        return;
    }

    // Check whether the async connect actually succeeded.
    int so_error = 0;
    socklen_t so_len = sizeof( so_error );
    if ( getsockopt( this->sockfd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>( &so_error ), &so_len ) < 0 ) {
        pr_debug( "Multiplayer failed to connect: getsockopt error: %s", net_strerror( net_last_error( ) ) );
        return;
    }
    if ( so_error != 0 ) {
        pr_debug( "Multiplayer failed to connect: %s", net_strerror( so_error ) );
        return;
    }

    // Connected. Adopt the fd into the FramedSocket and publish via the
    // atomic with release ordering so the game thread, on observing
    // active==true, also sees the initialized socket.
    this->framed_socket.adopt( this->sockfd );
    this->active = true;
    this->ever_connected = true;
}

void Multiplayer::flush_outbound( ) {
    this->framed_socket.flush( );
}

void Multiplayer::disconnect( ) {
    this->active = false;
    this->connect_pending = false;
    this->framed_socket.mark_closed( );
    if ( this->sockfd >= 0 ) {
        net_close( this->sockfd );
        this->sockfd = -1;
    }
}

void Multiplayer::process_events( RepGameState &state ) {
    if ( !this->active ) {
        return;
    }
    if ( mpload_trace_enabled( ) ) {
        static bool once = false;
        if ( !once ) {
            once = true;
            mpload_tracef( "connected" );
        }
    }
    // Drain any pending sends first so the outbound queue doesn't grow unbounded.
    this->framed_socket.flush( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error, disconnecting" );
        this->disconnect( );
        return;
    }

    const bool trace = mpload_trace_enabled( );
    const double pe_t0 = trace ? mpload_now_ms( ) : 0.0;
    int event_count = 0;
    int frames = 0, diffs = 0;
    bool capped = false;
    while ( event_count < 100 ) {
        event_count++;
        auto payload = this->framed_socket.recv_message( );
        if ( this->framed_socket.had_error( ) ) {
            pr_debug( "Multiplayer recv error, disconnecting" );
            this->framed_socket.clear_error( );
            this->disconnect( );
            return;
        }
        if ( !payload ) {
            // No more complete frames right now.
            break;
        }
        frames++;
        if ( trace && payload->size( ) > 1 &&
             static_cast<NetMsgType>( ( *payload )[ 1 ] ) == NetMsgType::CHUNK_DIFF_RESULT ) {
            diffs++;
        }
        this->handle_frame( state, *payload );
        if ( !this->active ) {
            return;
        }
    }
    // The cap stopped the loop only when 100 real frames were handled.
    capped = frames >= 100;
    if ( trace && frames > 0 ) {
        mpload_tracef( "pe frames=%d capped=%d us=%lld diffs=%d",
                       frames, capped ? 1 : 0, ( long long )( mpload_now_ms( ) * 1000.0 - pe_t0 * 1000.0 ), diffs );
    }
}

void Multiplayer::cleanup( ) {
    pr_debug( "closing multiplayer" );
    // If the connect thread is still running (server unreachable, wait in
    // progress), close the fd to interrupt its wait, then join. The connect
    // thread never closes the fd, so this is safe.
    if ( this->connect_thread.joinable( ) ) {
        if ( !this->active.load( ) && this->sockfd >= 0 ) {
            net_close( this->sockfd );
            this->sockfd = -1;
        }
        this->connect_thread.join( );
    }
    if ( this->active.load( ) && this->sockfd >= 0 ) {
        net_close( this->sockfd );
        this->sockfd = -1;
        this->active = false;
    }
    this->connect_pending = false;
    this->pending_diffs.clear( );
}
