#pragma once

// Socket portability shim over the POSIX/Winsock divide. Include ONLY from
// .cpp files — on Windows it pulls in winsock2.h, which must precede
// windows.h and clashes if it leaks into headers other TUs include.
//
// Everything here takes/returns plain int fds; on Windows they are SOCKET
// handles cast to int (fine for the small values we use — SOCKET is a
// uintptr but real values fit in int on every platform mingw targets).

#if defined( REPGAME_WINDOWS )

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstring>

inline bool net_startup( ) {
    // WSAStartup is cheap and must precede any other Winsock call; a
    // function-local static runs it exactly once.
    static const bool ok = [] {
        WSADATA wsa;
        return WSAStartup( MAKEWORD( 2, 2 ), &wsa ) == 0;
    }( );
    return ok;
}

inline int net_socket_tcp( ) {
    SOCKET s = socket( AF_INET, SOCK_STREAM, 0 );
    return s == INVALID_SOCKET ? -1 : static_cast<int>( s );
}

inline int net_close( int fd ) {
    return closesocket( static_cast<SOCKET>( fd ) );
}

inline bool net_set_nonblocking( int fd ) {
    u_long mode = 1;
    return ioctlsocket( static_cast<SOCKET>( fd ), FIONBIO, &mode ) == 0;
}

inline int net_last_error( ) {
    return WSAGetLastError( );
}

inline bool net_err_would_block( int e ) {
    return e == WSAEWOULDBLOCK;
}

inline bool net_err_connect_in_progress( int e ) {
    // Windows reports a nonblocking connect as WSAEWOULDBLOCK.
    return e == WSAEWOULDBLOCK;
}

inline const char *net_strerror( int e ) {
    static thread_local char buf[ 256 ];
    DWORD n = FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                              nullptr, e, 0, buf, sizeof( buf ), nullptr );
    if ( n == 0 ) {
        snprintf( buf, sizeof( buf ), "error %d", e );
    } else {
        while ( n > 0 && ( buf[ n - 1 ] == '\r' || buf[ n - 1 ] == '\n' ) ) {
            buf[ --n ] = '\0';
        }
    }
    return buf;
}

// >0 connect completed (check SO_ERROR), 0 timed out, <0 wait failed.
// Uses select() with the socket in BOTH writefds and exceptfds — a failed
// connect is reported via exceptfds on Windows (WSAPoll misses it on older
// Win10).
inline int net_wait_connect( int fd, int timeout_ms ) {
    fd_set wfds, efds;
    FD_ZERO( &wfds );
    FD_ZERO( &efds );
    FD_SET( static_cast<SOCKET>( fd ), &wfds );
    FD_SET( static_cast<SOCKET>( fd ), &efds );
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = ( timeout_ms % 1000 ) * 1000;
    return select( 0, nullptr, &wfds, &efds, &tv );
}

using net_iovec = WSABUF;

inline void net_iov_set( net_iovec &iov, const void *base, size_t len ) {
    iov.buf = const_cast<char *>( static_cast<const char *>( base ) );
    iov.len = static_cast<ULONG>( len );
}

// Bytes sent, or -1 (caller reads net_last_error()).
inline long net_sendv( int fd, net_iovec *iov, int cnt ) {
    DWORD sent = 0;
    if ( WSASend( static_cast<SOCKET>( fd ), iov, static_cast<DWORD>( cnt ),
                  &sent, 0, nullptr, nullptr ) != 0 ) {
        return -1;
    }
    return static_cast<long>( sent );
}

#else // POSIX

#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// Keep send() from raising SIGPIPE on POSIX (kills the whole process when a
// peer vanishes mid-write); not all targets define the flag.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

inline bool net_startup( ) {
    return true;
}

inline int net_socket_tcp( ) {
    return socket( AF_INET, SOCK_STREAM, 0 );
}

inline int net_close( int fd ) {
    return close( fd );
}

inline bool net_set_nonblocking( int fd ) {
    int flags = fcntl( fd, F_GETFL );
    if ( flags < 0 ) {
        return false;
    }
    return fcntl( fd, F_SETFL, flags | O_NONBLOCK ) == 0;
}

inline int net_last_error( ) {
    return errno;
}

inline bool net_err_would_block( int e ) {
    return e == EAGAIN || e == EWOULDBLOCK;
}

inline bool net_err_connect_in_progress( int e ) {
    return e == EINPROGRESS;
}

inline const char *net_strerror( int e ) {
    return strerror( e );
}

// >0 connect completed (check SO_ERROR), 0 timed out, <0 wait failed.
inline int net_wait_connect( int fd, int timeout_ms ) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    return poll( &pfd, 1, timeout_ms );
}

using net_iovec = struct iovec;

inline void net_iov_set( net_iovec &iov, const void *base, size_t len ) {
    iov.iov_base = const_cast<void *>( base );
    iov.iov_len = len;
}

// Bytes sent, or -1 (caller reads net_last_error()).
inline long net_sendv( int fd, net_iovec *iov, int cnt ) {
    struct msghdr msg;
    memset( &msg, 0, sizeof( msg ) );
    msg.msg_iov = iov;
    msg.msg_iovlen = static_cast<size_t>( cnt );
    return static_cast<long>( sendmsg( fd, &msg, MSG_NOSIGNAL ) );
}

#endif
