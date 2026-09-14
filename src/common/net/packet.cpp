#include "common/net/packet.hpp"

#include "common/constants.hpp"

#include <cstdio>

// Keep NET_CHUNK_BLOCK_SIZE in sync with CHUNK_BLOCK_SIZE in chunk.hpp.
static_assert( NET_CHUNK_BLOCK_SIZE == ( CHUNK_SIZE_X + 2 ) * ( CHUNK_SIZE_Y + 2 ) * ( CHUNK_SIZE_Z + 2 ),
               "NET_CHUNK_BLOCK_SIZE mismatch" );

// Local pr_debug so this module is self-contained on both client and server.
#define net_pr_debug( fmt, ... ) fprintf( stderr, "net:%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ )

// ---------------------------------------------------------------------------
// PacketWriter
// ---------------------------------------------------------------------------
PacketWriter::PacketWriter( NetMsgType type, int32_t player_id ) {
    // Reserve the fixed header so the first real write lands after it.
    // Header layout: magic(4) crc(4) payload_len(4) version(1) type(1) player_id(4) = 18 bytes.
    buf_.resize( 18 );
    buf_[ 12 ] = NET_PROTOCOL_VERSION;
    buf_[ 13 ] = static_cast<uint8_t>( type );
    // player_id little-endian
    buf_[ 14 ] = static_cast<uint8_t>( player_id );
    buf_[ 15 ] = static_cast<uint8_t>( player_id >> 8 );
    buf_[ 16 ] = static_cast<uint8_t>( player_id >> 16 );
    buf_[ 17 ] = static_cast<uint8_t>( player_id >> 24 );
}

void PacketWriter::write_u8( uint8_t v ) {
    buf_.push_back( v );
}

void PacketWriter::write_u32( uint32_t v ) {
    buf_.push_back( static_cast<uint8_t>( v ) );
    buf_.push_back( static_cast<uint8_t>( v >> 8 ) );
    buf_.push_back( static_cast<uint8_t>( v >> 16 ) );
    buf_.push_back( static_cast<uint8_t>( v >> 24 ) );
}

void PacketWriter::write_i32( int32_t v ) {
    write_u32( static_cast<uint32_t>( v ) );
}

void PacketWriter::write_f32( float v ) {
    uint32_t u;
    std::memcpy( &u, &v, sizeof( u ) );
    write_u32( u );
}

void PacketWriter::write_bytes( const void *data, size_t len ) {
    const auto *bytes = static_cast<const uint8_t *>( data );
    buf_.insert( buf_.end( ), bytes, bytes + len );
}

void PacketWriter::write_block_state( const BlockState &bs ) {
    // Field-by-field; BlockState is { BlockID id; unsigned char rotation;
    // int current_redstone_power; BlockID display_id; }. We serialize the
    // logical fields, not the raw struct, so padding/endianness can't drift.
    write_u32( static_cast<uint32_t>( bs.id ) );
    write_u8( bs.rotation );
    write_i32( bs.current_redstone_power );
    write_u32( static_cast<uint32_t>( bs.display_id ) );
}

// ---------------------------------------------------------------------------
// PacketReader
// ---------------------------------------------------------------------------
bool PacketReader::read_u8( uint8_t &out ) {
    if ( pos_ + 1 > len_ ) return false;
    out = data_[ pos_++ ];
    return true;
}

bool PacketReader::read_u32( uint32_t &out ) {
    if ( pos_ + 4 > len_ ) return false;
    out = static_cast<uint32_t>( data_[ pos_ ] )
          | ( static_cast<uint32_t>( data_[ pos_ + 1 ] ) << 8 )
          | ( static_cast<uint32_t>( data_[ pos_ + 2 ] ) << 16 )
          | ( static_cast<uint32_t>( data_[ pos_ + 3 ] ) << 24 );
    pos_ += 4;
    return true;
}

bool PacketReader::read_i32( int32_t &out ) {
    uint32_t u;
    if ( !read_u32( u ) ) return false;
    out = static_cast<int32_t>( u );
    return true;
}

bool PacketReader::read_f32( float &out ) {
    uint32_t u;
    if ( !read_u32( u ) ) return false;
    std::memcpy( &out, &u, sizeof( out ) );
    return true;
}

bool PacketReader::read_bytes( void *out, size_t n ) {
    if ( pos_ + n > len_ ) return false;
    std::memcpy( out, data_ + pos_, n );
    pos_ += n;
    return true;
}

bool PacketReader::read_block_state( BlockState &out ) {
    uint32_t id, display_id;
    uint8_t rotation;
    int32_t power;
    if ( !read_u32( id ) ) return false;
    if ( !read_u8( rotation ) ) return false;
    if ( !read_i32( power ) ) return false;
    if ( !read_u32( display_id ) ) return false;
    out.id = static_cast<BlockID>( id );
    out.rotation = rotation;
    out.current_redstone_power = power;
    out.display_id = static_cast<BlockID>( display_id );
    return true;
}

// ---------------------------------------------------------------------------
// CRC32 (IEEE 802.3, reflected). Table built on first use.
// ---------------------------------------------------------------------------
static uint32_t crc_table[ 256 ];
static bool crc_table_init = false;

static void init_crc_table( ) {
    for ( uint32_t i = 0; i < 256; i++ ) {
        uint32_t c = i;
        for ( int k = 0; k < 8; k++ ) {
            c = ( c & 1 ) ? ( 0xEDB88320u ^ ( c >> 1 ) ) : ( c >> 1 );
        }
        crc_table[ i ] = c;
    }
    crc_table_init = true;
}

uint32_t net_crc32( const uint8_t *data, size_t len ) {
    if ( !crc_table_init ) {
        init_crc_table( );
    }
    uint32_t crc = 0xFFFFFFFFu;
    for ( size_t i = 0; i < len; i++ ) {
        crc = crc_table[ ( crc ^ data[ i ] ) & 0xFF ] ^ ( crc >> 8 );
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Typed payload (de)serialization.
// ---------------------------------------------------------------------------

void net_serialize_player( PacketWriter &w, const NetPlayerPayload &p ) {
    w.write_f32( p.x );
    w.write_f32( p.y );
    w.write_f32( p.z );
    for ( int i = 0; i < 16; i++ ) {
        w.write_f32( p.rotation[ i ] );
    }
}

bool net_deserialize_player( PacketReader &r, NetPlayerPayload &p ) {
    if ( !r.read_f32( p.x ) ) return false;
    if ( !r.read_f32( p.y ) ) return false;
    if ( !r.read_f32( p.z ) ) return false;
    for ( int i = 0; i < 16; i++ ) {
        if ( !r.read_f32( p.rotation[ i ] ) ) return false;
    }
    return true;
}

void net_serialize_block_update( PacketWriter &w, const NetBlockUpdatePayload &p ) {
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
    w.write_block_state( p.blockState );
}

bool net_deserialize_block_update( PacketReader &r, NetBlockUpdatePayload &p ) {
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    if ( !r.read_block_state( p.blockState ) ) return false;
    return true;
}

void net_serialize_chunk_diff_request( PacketWriter &w, const NetChunkDiffRequestPayload &p ) {
    w.write_i32( p.min_x );
    w.write_i32( p.min_y );
    w.write_i32( p.min_z );
    w.write_u8( p.size_x );
    w.write_u8( p.size_y );
    w.write_u8( p.size_z );
}

bool net_deserialize_chunk_diff_request( PacketReader &r, NetChunkDiffRequestPayload &p ) {
    if ( !r.read_i32( p.min_x ) ) return false;
    if ( !r.read_i32( p.min_y ) ) return false;
    if ( !r.read_i32( p.min_z ) ) return false;
    if ( !r.read_u8( p.size_x ) ) return false;
    if ( !r.read_u8( p.size_y ) ) return false;
    if ( !r.read_u8( p.size_z ) ) return false;
    return true;
}

void net_serialize_chunk_diff_result( PacketWriter &w, const NetChunkDiffResultPayload &p ) {
    w.write_i32( p.chunk_x );
    w.write_i32( p.chunk_y );
    w.write_i32( p.chunk_z );
    w.write_u32( p.num_diffs );
    for ( uint32_t i = 0; i < p.num_diffs; i++ ) {
        w.write_u32( p.diffs[ i ].blocks_index );
        w.write_block_state( p.diffs[ i ].blockState );
    }
}

bool net_deserialize_chunk_diff_result( PacketReader &r, NetChunkDiffResultPayload &p ) {
    if ( !r.read_i32( p.chunk_x ) ) return false;
    if ( !r.read_i32( p.chunk_y ) ) return false;
    if ( !r.read_i32( p.chunk_z ) ) return false;
    if ( !r.read_u32( p.num_diffs ) ) return false;
    // Sanity bound before allocating: a chunk can't have more diffs than blocks.
    if ( p.num_diffs > static_cast<uint32_t>( NET_CHUNK_BLOCK_SIZE ) ) {
        net_pr_debug( "CHUNK_DIFF_RESULT num_diffs:%u > CHUNK_BLOCK_SIZE:%d", p.num_diffs, NET_CHUNK_BLOCK_SIZE );
        return false;
    }
    p.diffs.resize( p.num_diffs );
    for ( uint32_t i = 0; i < p.num_diffs; i++ ) {
        if ( !r.read_u32( p.diffs[ i ].blocks_index ) ) return false;
        if ( !r.read_block_state( p.diffs[ i ].blockState ) ) return false;
    }
    return true;
}
