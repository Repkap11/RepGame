#pragma once

// FramedSocket: sends/receives length-prefixed, magic-numbered, CRC-validated
// frames (see packet.hpp for the wire format).
//
// The frame codec is transport-agnostic. Two transports are supported:
//   - fd:  adopt() a connected nonblocking socket, then flush() writes and
//          recv_message() reads (POSIX only: Linux, Android, server).
//   - manual: adopt_manual(), feed inbound bytes via push_recv_bytes() and
//          drain outbound frames via pop_send_frame() (WASM WebSocket).
//
// Owns partial-read and partial-write buffers so callers never see partial
// frames.

#include <cstdint>
#include <optional>
#include <vector>

#include "common/net/packet.hpp"

class FramedSocket {
  public:
    FramedSocket( ) = default;

    // Adopt an already-connected, nonblocking fd. Does not take ownership of
    // the fd's lifecycle (caller still closes it); FramedSocket only uses it
    // for send/recv. POSIX only.
    void adopt( int fd );

    // Open in manual-transport mode (no fd). Used on WASM, where the caller
    // shuttles bytes to/from a browser WebSocket.
    void adopt_manual( );

    // Build a complete frame (header + payload + crc) from a typed payload
    // buffer and queue it for sending. Returns false if the socket is closed.
    bool send_message( NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload );

    // Convenience: build a frame with no type-specific payload (used by
    // PLAYER_CONNECTED / PLAYER_DISCONNECTED).
    bool send_empty( NetMsgType type, int32_t player_id );

    // fd transport only: drain the outbound queue. Returns:
    //   >= 0  progress made (bytes still queued, 0 = fully flushed)
    //   -1   fatal socket error; caller should tear down the connection
    int flush( );

    // fd transport only: try to receive a complete frame. Returns:
    //   a payload (without header) if a full, valid frame is available
    //   std::nullopt if no full frame yet (EAGAIN/EWOULDBLOCK or partial)
    //   Sets had_error=true on fatal error (bad magic/CRC/recv error/EOF);
    //   caller should check had_error() and tear down.
    std::optional<std::vector<uint8_t>> recv_message( );

    // Manual transport: append raw bytes received from the transport to the
    // inbound buffer, then call parse_next_frame() to extract frames.
    void push_recv_bytes( const uint8_t *data, size_t len );

    // Manual transport: pop the next complete outbound frame, or nullopt if
    // the queue is empty.
    std::optional<std::vector<uint8_t>> pop_send_frame( );

    // Parse the next complete frame out of the inbound buffer. Returns a
    // payload (without wire header), or nullopt if no complete frame is
    // buffered yet. Sets had_error on malformed frames.
    std::optional<std::vector<uint8_t>> parse_next_frame( );

    // Mark the socket closed (send_message will fail).
    void mark_closed( ) { open_ = false; }
    bool is_open( ) const { return open_; }

    // For manual transports to signal a transport-level failure (e.g. WS
    // send error), matching what fd errors do internally.
    void flag_error( ) { had_error_ = true; }

    // After recv_message/parse_next_frame returns nullopt, check this to
    // distinguish "no data yet" from "connection is dead".
    bool had_error( ) const { return had_error_; }
    void clear_error( ) { had_error_ = false; }

    bool has_outbound( ) const { return !send_queue_.empty( ); }

  private:
    int fd_ = -1;
    bool open_ = false;
    bool had_error_ = false;

    // Outbound: list of complete frames waiting to be sent, plus a byte
    // offset into the front frame for partial-write resumption (fd transport).
    std::vector<std::vector<uint8_t>> send_queue_;
    size_t send_offset_ = 0;

    // Inbound: accumulator for partial reads. We parse frames out of this
    // buffer as they complete.
    std::vector<uint8_t> recv_buf_;
    size_t recv_consume_ = 0; // bytes already parsed out of recv_buf_

    void reset_buffers( );

    // Header is: magic(4) crc(4) payload_len(4) = 12 bytes before payload.
    static constexpr size_t HEADER_LEN = 12;
    // payload starts with version(1) type(1) player_id(4) = 6 fixed bytes,
    // then type-specific bytes.
    static constexpr size_t PAYLOAD_HEADER_LEN = 6;
};
