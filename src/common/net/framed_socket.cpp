#include "common/net/framed_socket.hpp"

#include <cstdio>
#include <cstring>

#include "common/net/socket_compat.hpp"

#define fs_pr_debug( fmt, ... ) fprintf( stderr, "framed_socket:%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ )

void FramedSocket::reset_buffers( ) {
    had_error_ = false;
    send_queue_.clear( );
    send_offset_ = 0;
    recv_buf_.clear( );
    recv_consume_ = 0;
}

void FramedSocket::adopt( int fd ) {
    fd_ = fd;
    open_ = true;
    reset_buffers( );
}

void FramedSocket::adopt_manual( ) {
    fd_ = -1;
    open_ = true;
    reset_buffers( );
}

bool FramedSocket::send_message( NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload ) {
    if ( !open_ ) return false;

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

std::optional<std::vector<uint8_t>> FramedSocket::parse_next_frame( ) {
    const size_t avail = recv_buf_.size( ) - recv_consume_;
    if ( avail < HEADER_LEN ) {
        return std::nullopt;
    }
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
    if ( avail < frame_len ) {
        // Not enough bytes for the full frame yet.
        return std::nullopt;
    }
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
    if ( recv_consume_ == recv_buf_.size( ) ) {
        recv_buf_.clear( );
        recv_consume_ = 0;
    }
    return payload;
}

void FramedSocket::push_recv_bytes( const uint8_t *data, size_t len ) {
    // Drop already-consumed bytes so the buffer stays bounded.
    if ( recv_consume_ > 0 ) {
        recv_buf_.erase( recv_buf_.begin( ), recv_buf_.begin( ) + recv_consume_ );
        recv_consume_ = 0;
    }
    recv_buf_.insert( recv_buf_.end( ), data, data + len );
}

std::optional<std::vector<uint8_t>> FramedSocket::pop_send_frame( ) {
    if ( send_queue_.empty( ) ) {
        return std::nullopt;
    }
    std::vector<uint8_t> frame = std::move( send_queue_.front( ) );
    send_queue_.pop_front( );
    send_offset_ = 0;
    return frame;
}

int FramedSocket::flush( ) {
    // Batched writes: one vectored send gathers up to 64 queued frames. On a
    // short send, send_offset_ records how far into the front frame the
    // kernel got so the next flush resumes mid-frame. Frame order on the
    // wire is exactly queue order.
    static constexpr int MAX_IOV = 64;
    while ( !send_queue_.empty( ) ) {
        net_iovec iov[ MAX_IOV ];
        int iov_cnt = 0;
        bool first = true;
        for ( const std::vector<uint8_t> &frame : send_queue_ ) {
            if ( iov_cnt == MAX_IOV ) {
                break;
            }
            if ( first ) {
                // The front frame may be part-sent already.
                net_iov_set( iov[ iov_cnt ], frame.data( ) + send_offset_, frame.size( ) - send_offset_ );
                first = false;
            } else {
                net_iov_set( iov[ iov_cnt ], frame.data( ), frame.size( ) );
            }
            iov_cnt++;
        }
        const long nsent = net_sendv( fd_, iov, iov_cnt );
        if ( nsent < 0 ) {
            if ( net_err_would_block( net_last_error( ) ) ) {
                // Socket buffer full; try again next flush.
                return static_cast<int>( send_queue_.size( ) );
            }
            fs_pr_debug( "sendv error: %s", net_strerror( net_last_error( ) ) );
            had_error_ = true;
            return -1;
        }
        // Drop the frames the kernel took whole; a short send leaves the
        // front frame partially consumed (send_offset_ updated).
        size_t left = static_cast<size_t>( nsent );
        int fully_sent = 0;
        while ( fully_sent < iov_cnt ) {
            const size_t remaining = send_queue_.front( ).size( ) - send_offset_;
            if ( left < remaining ) {
                send_offset_ += left;
                break;
            }
            left -= remaining;
            send_queue_.pop_front( );
            send_offset_ = 0;
            fully_sent++;
        }
        if ( fully_sent < iov_cnt ) {
            // The kernel took fewer bytes than offered — out of socket
            // buffer; the rest goes on the next flush.
            return static_cast<int>( send_queue_.size( ) );
        }
        // Whole batch went out; loop if more than MAX_IOV frames queued.
    }
    return 0;
}

std::optional<std::vector<uint8_t>> FramedSocket::recv_message( ) {
    while ( true ) {
        auto payload = parse_next_frame( );
        if ( payload || had_error_ ) {
            return payload;
        }
        // Compact the buffer if there's consumed data at the front.
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
        const long n = recv( fd_, reinterpret_cast<char *>( recv_buf_.data( ) + old_size ), 65536, 0 );
        recv_buf_.resize( old_size + ( n > 0 ? n : 0 ) );
        if ( n < 0 ) {
            const int err = net_last_error( );
            if ( net_err_would_block( err ) ) {
                return std::nullopt; // no more data right now
            }
            fs_pr_debug( "recv error: %s", net_strerror( err ) );
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
