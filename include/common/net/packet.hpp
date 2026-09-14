#pragma once

// Framed, validated, platform-independent wire protocol for multiplayer.
//
// Each message on the wire is:
//   [ uint32 magic      = NET_MAGIC ("RGPT" = 0x52475054) ]
//   [ uint32 crc32      of everything after this field ]
//   [ uint32 payload_len  bytes following this field ]
//   [ payload: protocol_version(1) | msg_type(1) | player_id(4) | type-specific ]
//
// All multi-byte fields are little-endian on the wire. Readers/writers use
// explicit shift/mask so the format is identical on any host endianness
// (Linux, Android, future Windows/WASM).

#include <cstdint>
#include <cstring>
#include <vector>

#include "common/block.hpp"
#include "common/constants.hpp"

// CHUNK_BLOCK_SIZE is defined in chunk.hpp as (CHUNK_SIZE_INTERNAL_X *
// CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_Z) = 34*34*34 = 39304. We avoid
// pulling chunk.hpp (which drags in renderer headers) into the net module, so
// restate the bound here and assert it matches.
static constexpr int32_t NET_CHUNK_BLOCK_SIZE = ( CHUNK_SIZE_X + 2 ) * ( CHUNK_SIZE_Y + 2 ) * ( CHUNK_SIZE_Z + 2 );

// Wire-format constants. Keep in sync with framed_socket.cpp.
static constexpr uint32_t NET_MAGIC = 0x52475054u; // 'R','G','P','T' little-endian
static constexpr uint8_t NET_PROTOCOL_VERSION = 1;
// Largest payload we will accept. Chunk diffs dominate; a chunk can have at
// most CHUNK_BLOCK_SIZE (~35k) diffs, each ~12 bytes -> ~420KB. Round up.
static constexpr uint32_t NET_MAX_PAYLOAD_LEN = 1u << 20; // 1 MiB
// Cap on each dimension of a CHUNK_DIFF_REQUEST box. 64^3 = 262k chunks is
// far beyond any realistic load, but bounds server memory per request.
static constexpr uint8_t NET_MAX_BOX_DIM = 64;

// Message types. Values are stable on the wire; do not renumber.
enum class NetMsgType : uint8_t {
    PLAYER_LOCATION = 0,
    BLOCK_UPDATE = 1,
    CLIENT_INIT = 2,
    PLAYER_CONNECTED = 3,
    PLAYER_DISCONNECTED = 4,
    CHUNK_DIFF_REQUEST = 5,
    CHUNK_DIFF_RESULT = 6,
};

// ---------------------------------------------------------------------------
// PacketWriter: append-only little-endian byte buffer.
// ---------------------------------------------------------------------------
class PacketWriter {
  public:
    PacketWriter( ) = default;
    explicit PacketWriter( NetMsgType type, int32_t player_id );

    void write_u8( uint8_t v );
    void write_u32( uint32_t v );
    void write_i32( int32_t v );
    void write_f32( float v );
    void write_bytes( const void *data, size_t len );

    // Serialize a BlockState field-by-field (no padding/endianness drift).
    void write_block_state( const BlockState &bs );

    const std::vector<uint8_t> &buf( ) const { return buf_; }
    std::vector<uint8_t> take_buf( ) { return std::move( buf_ ); }

  private:
    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// PacketReader: bounds-checked little-endian reader over a fixed buffer.
// Every read returns false on overflow; callers should bail on false.
// ---------------------------------------------------------------------------
class PacketReader {
  public:
    PacketReader( const uint8_t *data, size_t len )
        : data_( data ), len_( len ), pos_( 0 ) {
    }

    bool read_u8( uint8_t &out );
    bool read_u32( uint32_t &out );
    bool read_i32( int32_t &out );
    bool read_f32( float &out );
    bool read_bytes( void *out, size_t n );
    bool read_block_state( BlockState &out );

    bool eof( ) const { return pos_ == len_; }
    size_t remaining( ) const { return len_ - pos_; }

  private:
    const uint8_t *data_;
    size_t len_;
    size_t pos_;
};

// ---------------------------------------------------------------------------
// CRC32 (IEEE 802.3). Table-based; initialized on first use.
// ---------------------------------------------------------------------------
uint32_t net_crc32( const uint8_t *data, size_t len );

// ---------------------------------------------------------------------------
// Typed payload (de)serialization. Each writes/reads only the type-specific
// payload (after the fixed header of version|type|player_id). The header is
// applied by framed_socket when building/validating a complete frame.
// ---------------------------------------------------------------------------

// PLAYER_LOCATION / CLIENT_INIT share the same payload shape.
struct NetPlayerPayload {
    float x, y, z;
    float rotation[ 16 ];
};
void net_serialize_player( PacketWriter &w, const NetPlayerPayload &p );
bool net_deserialize_player( PacketReader &r, NetPlayerPayload &p );

// BLOCK_UPDATE
struct NetBlockUpdatePayload {
    int32_t x, y, z;
    BlockState blockState;
};
void net_serialize_block_update( PacketWriter &w, const NetBlockUpdatePayload &p );
bool net_deserialize_block_update( PacketReader &r, NetBlockUpdatePayload &p );

// CHUNK_DIFF_REQUEST: a box of chunks.
struct NetChunkDiffRequestPayload {
    int32_t min_x, min_y, min_z;
    uint8_t size_x, size_y, size_z;
};
void net_serialize_chunk_diff_request( PacketWriter &w, const NetChunkDiffRequestPayload &p );
bool net_deserialize_chunk_diff_request( PacketReader &r, NetChunkDiffRequestPayload &p );

// CHUNK_DIFF_RESULT: variable-length list of (index, BlockState) diffs.
// One frame per chunk. Caller validates num_diffs and each index against
// CHUNK_BLOCK_SIZE before applying.
struct NetChunkDiffEntry {
    uint32_t blocks_index;
    BlockState blockState;
};
struct NetChunkDiffResultPayload {
    int32_t chunk_x, chunk_y, chunk_z;
    uint32_t num_diffs;
    // Not owned; points into the reader's buffer after deserialize, or into a
    // caller-owned vector when serializing. Kept simple: deserialize copies
    // into this vector, serialize reads from it.
    std::vector<NetChunkDiffEntry> diffs;
};
void net_serialize_chunk_diff_result( PacketWriter &w, const NetChunkDiffResultPayload &p );
bool net_deserialize_chunk_diff_result( PacketReader &r, NetChunkDiffResultPayload &p );
