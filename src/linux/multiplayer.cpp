#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/constants.hpp"
#include "common/multiplayer.hpp"
#include "common/net/packet.hpp"

void Multiplayer::init( const char *hostname, const int port ) {
    pr_debug( "using server %s:%i", hostname, port );

    this->active = false;
    this->portno = port;
    this->prev_player_pos = glm::vec3( 0.0f );
    this->prev_rotation = glm::mat4( 1.0f );

    // Create the socket on the main thread so we own its lifecycle. The
    // background connect thread uses this->sockfd but never closes it; on
    // shutdown cleanup() closes it to interrupt any pending poll().
    this->sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( this->sockfd < 0 ) {
        pr_debug( "Unable to allocate socket" );
        this->sockfd = -1;
        return;
    }

    // Perform DNS + nonblocking connect + poll on a background thread so a
    // down/unreachable server cannot stall startup.
    this->connect_thread = std::thread( &Multiplayer::connect_async, this, std::string( hostname ) );
}

void Multiplayer::connect_async( const std::string &hostname ) {
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
    int flags = fcntl( this->sockfd, F_GETFL );
    if ( flags < 0 ) {
        pr_debug( "Unable to get socket flags" );
        return;
    }
    if ( fcntl( this->sockfd, F_SETFL, flags | O_NONBLOCK ) < 0 ) {
        pr_debug( "Unable to set non-blocking" );
        return;
    }

    int rc = connect( this->sockfd, reinterpret_cast<sockaddr *>( &serv_addr ), sizeof( serv_addr ) );
    if ( rc == 0 ) {
        // Immediate success (e.g. localhost).
        this->framed_socket.adopt( this->sockfd );
        this->active = true;
        return;
    }
    if ( rc < 0 && errno != EINPROGRESS ) {
        pr_debug( "Multiplayer failed to connect: %s", strerror( errno ) );
        return;
    }

    // Wait for the socket to become writable, with a bounded timeout.
    struct pollfd pfd;
    pfd.fd = this->sockfd;
    pfd.events = POLLOUT;
    int pr = poll( &pfd, 1, MULTIPLAYER_CONNECT_TIMEOUT_MS );
    if ( pr <= 0 ) {
        if ( pr == 0 ) {
            pr_debug( "Multiplayer failed to connect: timed out after %d ms", MULTIPLAYER_CONNECT_TIMEOUT_MS );
        } else {
            pr_debug( "Multiplayer failed to connect: poll error: %s", strerror( errno ) );
        }
        return;
    }

    // Check whether the async connect actually succeeded.
    int so_error = 0;
    socklen_t so_len = sizeof( so_error );
    if ( getsockopt( this->sockfd, SOL_SOCKET, SO_ERROR, &so_error, &so_len ) < 0 ) {
        pr_debug( "Multiplayer failed to connect: getsockopt error: %s", strerror( errno ) );
        return;
    }
    if ( so_error != 0 ) {
        pr_debug( "Multiplayer failed to connect: %s", strerror( so_error ) );
        return;
    }

    // Connected. Adopt the fd into the FramedSocket and publish via the
    // atomic with release ordering so the game thread, on observing
    // active==true, also sees the initialized socket.
    this->framed_socket.adopt( this->sockfd );
    this->active = true;
}

void Multiplayer::flush_outbound( ) {
    this->framed_socket.flush( );
}

void Multiplayer::disconnect( ) {
    this->active = false;
    this->framed_socket.mark_closed( );
    if ( this->sockfd >= 0 ) {
        close( this->sockfd );
        this->sockfd = -1;
    }
}

void Multiplayer::process_events( World &world ) {
    if ( !this->active ) {
        return;
    }
    // Drain any pending sends first so the outbound queue doesn't grow unbounded.
    this->framed_socket.flush( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error, disconnecting" );
        this->disconnect( );
        return;
    }

    int event_count = 0;
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
            return;
        }
        this->handle_frame( world, *payload );
    }
}

void Multiplayer::cleanup( ) {
    pr_debug( "closing multiplayer" );
    // If the connect thread is still running (server unreachable, poll in
    // progress), close the fd to interrupt its poll(), then join. The connect
    // thread never closes the fd, so this is safe.
    if ( this->connect_thread.joinable( ) ) {
        if ( !this->active.load( ) && this->sockfd >= 0 ) {
            close( this->sockfd );
            this->sockfd = -1;
        }
        this->connect_thread.join( );
    }
    if ( this->active.load( ) && this->sockfd >= 0 ) {
        close( this->sockfd );
        this->sockfd = -1;
        this->active = false;
    }
    this->pending_diffs.clear( );
}
