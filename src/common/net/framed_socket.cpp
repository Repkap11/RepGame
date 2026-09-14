#include "common/net/framed_socket.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>

#define fs_pr_debug( fmt, ... ) fprintf( stderr, "framed_socket:%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ )

void FramedSocket::adopt( int fd ) {
    fd_ = fd;
    had_error_ = false;
    send_queue_.clear( );
    send_offset_ = 0;
    recv_buf_.clear( );
    recv_consume_ = 0;
}

bool FramedSocket::send_message( NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload ) {
    if ( fd_ < 0 ) return false;

    // Build the complete frame: magic | crc | payload_len | version | type | player_id | payload
    std::vector<uint8_t> frame;
    const size_t payload_len = PAYLOAD_HEADER_LEN + payload.size( );
    frame.resize( HEADER_LEN + payload_len );

    // payload_len field (little-endian)
    frame[ 8 ] = static_cast<uint8_t>( payload_len );
    frame[ 9 ] = static_cast<uint8_t>( payload_len >> 8 );
    frame[ 10 ] = static_cast<uint8_t>( payload_len >> 16 );
    frame[ 11 ] = static_cast<uint8_t>( payload_len >> 24 );

    // Fixed payload header: version | type | player_id
    frame[ 12 ] = NET_PROTOCOL_VERSION;
    frame[ 13 ] = static_cast<uint8_t>( type );
    frame[ 14 ] = static_cast<uint8_t>( player_id );
    frame[ 15 ] = static_cast<uint8_t>( player_id >> 8 );
    frame[ 16 ] = static_cast<uint8_t>( player_id >> 16 );
    frame[ 17 ] = static_cast<uint8_t>( player_id >> 24 );

    // Type-specific payload
    if ( !payload.empty( ) ) {
        std::memcpy( frame.data( ) + 18, payload.data( ), payload.size( ) );
    }

    // magic (little-endian)
    frame[ 0 ] = static_cast<uint8_t>( NET_MAGIC );
    frame[ 1 ] = static_cast<uint8_t>( NET_MAGIC >> 8 );
    frame[ 2 ] = static_cast<uint8_t>( NET_MAGIC >> 16 );
    frame[ 3 ] = static_cast<uint8_t>( NET_MAGIC >> 24 );

    // crc32 over everything after the crc field, i.e. [payload_len .. end]
    const uint32_t crc = net_crc32( frame.data( ) + 8, frame.size( ) - 8 );
    frame[ 4 ] = static_cast<uint8_t>( crc );
    frame[ 5 ] = static_cast<uint8_t>( crc >> 8 );
    frame[ 6 ] = static_cast<uint8_t>( crc >> 16 );
    frame[ 7 ] = static_cast<uint8_t>( crc >> 24 );

    send_queue_.push_back( std::move( frame ) );
    return true;
}

bool FramedSocket::send_empty( NetMsgType type, int32_t player_id ) {
    return send_message( type, player_id, { } );
}

int FramedSocket::flush( ) {
    while ( !send_queue_.empty( ) ) {
        std::vector<uint8_t> &front = send_queue_.front( );
        const size_t remaining = front.size( ) - send_offset_;
        const char *send_start = reinterpret_cast<const char *>( front.data( ) ) + send_offset_;

        ssize_t nsent = send( fd_, send_start, remaining, 0 );
        if ( nsent < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // Socket buffer full; try again next flush.
                return static_cast<int>( send_queue_.size( ) );
            }
            fs_pr_debug( "send error: %s", strerror( errno ) );
            had_error_ = true;
            return -1;
        }
        send_offset_ += static_cast<size_t>( nsent );
        if ( send_offset_ < front.size( ) ) {
            // Partial write; rest goes on the next flush.
            return static_cast<int>( send_queue_.size( ) );
        }
        send_queue_.erase( send_queue_.begin( ) );
        send_offset_ = 0;
    }
    return 0;
}

std::optional<std::vector<uint8_t>> FramedSocket::recv_message( ) {
    while ( true ) {
        // Try to parse a complete frame out of recv_buf_ starting at recv_consume_.
        const size_t avail = recv_buf_.size( ) - recv_consume_;
        if ( avail >= HEADER_LEN ) {
            const uint8_t *p = recv_buf_.data( ) + recv_consume_;
            // Validate magic.
            const uint32_t magic = static_cast<uint32_t>( p[ 0 ] )
                                   | ( static_cast<uint32_t>( p[ 1 ] ) << 8 )
                                   | ( static_cast<uint32_t>( p[ 2 ] ) << 16 )
                                   | ( static_cast<uint32_t>( p[ 3 ] ) << 24 );
            if ( magic != NET_MAGIC ) {
                fs_pr_debug( "bad magic: 0x%08x (expected 0x%08x)", magic, NET_MAGIC );
                had_error_ = true;
                return std::nullopt;
            }
            // payload_len
            const uint32_t payload_len = static_cast<uint32_t>( p[ 8 ] )
                                          | ( static_cast<uint32_t>( p[ 9 ] ) << 8 )
                                          | ( static_cast<uint32_t>( p[ 10 ] ) << 16 )
                                          | ( static_cast<uint32_t>( p[ 11 ] ) << 24 );
            if ( payload_len < PAYLOAD_HEADER_LEN || payload_len > NET_MAX_PAYLOAD_LEN ) {
                fs_pr_debug( "bad payload_len: %u", payload_len );
                had_error_ = true;
                return std::nullopt;
            }
            const size_t frame_len = HEADER_LEN + payload_len;
            if ( avail >= frame_len ) {
                // Full frame present. Verify CRC over [payload_len .. end].
                const uint32_t stored_crc = static_cast<uint32_t>( p[ 4 ] )
                                             | ( static_cast<uint32_t>( p[ 5 ] ) << 8 )
                                             | ( static_cast<uint32_t>( p[ 6 ] ) << 16 )
                                             | ( static_cast<uint32_t>( p[ 7 ] ) << 24 );
                const uint32_t calc_crc = net_crc32( p + 8, frame_len - 8 );
                if ( stored_crc != calc_crc ) {
                    fs_pr_debug( "crc mismatch: stored 0x%08x calc 0x%08x", stored_crc, calc_crc );
                    had_error_ = true;
                    return std::nullopt;
                }
                // Extract the payload (without the wire header) and advance.
                std::vector<uint8_t> payload( p + HEADER_LEN, p + frame_len );
                recv_consume_ += frame_len;
                // Compact the buffer if we've consumed a lot, to keep it bounded.
                if ( recv_consume_ >= 4096 && recv_consume_ == recv_buf_.size( ) ) {
                    recv_buf_.clear( );
                    recv_consume_ = 0;
                }
                return payload;
            }
            // Not enough bytes for the full frame yet; fall through to recv more.
        }

        // Compact the buffer if there's a lot of consumed data at the front.
        if ( recv_consume_ > 0 ) {
            recv_buf_.erase( recv_buf_.begin( ), recv_buf_.begin( ) + recv_consume_ );
            recv_consume_ = 0;
        }

        // Read more bytes from the socket.
        // Grow capacity if needed; read up to 64KB at a time.
        size_t old_size = recv_buf_.size( );
        if ( recv_buf_.capacity( ) - old_size < 4096 ) {
            recv_buf_.reserve( old_size + 65536 );
        }
        recv_buf_.resize( old_size + 65536 );
        ssize_t n = recv( fd_, recv_buf_.data( ) + old_size, 65536, 0 );
        recv_buf_.resize( old_size + ( n > 0 ? n : 0 ) );
        if ( n < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                return std::nullopt; // no more data right now
            }
            fs_pr_debug( "recv error: %s", strerror( errno ) );
            had_error_ = true;
            return std::nullopt;
        }
        if ( n == 0 ) {
            // Peer closed the connection.
            fs_pr_debug( "peer closed connection" );
            had_error_ = true;
            return std::nullopt;
        }
        // Loop back to try parsing again with the new bytes.
    }
}
