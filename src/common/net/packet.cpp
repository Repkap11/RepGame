#include "common/net/packet.hpp"

#include "common/constants.hpp"
#include "common/item.hpp"
#include "common/utils/env_config.hpp"

#include <algorithm>
#include <cmath>
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

void PacketWriter::write_u16( uint16_t v ) {
    buf_.push_back( static_cast<uint8_t>( v ) );
    buf_.push_back( static_cast<uint8_t>( v >> 8 ) );
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

void PacketWriter::write_u64( uint64_t u ) {
    for ( int i = 0; i < 8; i++ ) {
        buf_.push_back( static_cast<uint8_t>( u >> ( i * 8 ) ) );
    }
}

void PacketWriter::write_i64( int64_t v ) {
    write_u64( static_cast<uint64_t>( v ) );
}

void PacketWriter::write_f32( float v ) {
    uint32_t u;
    std::memcpy( &u, &v, sizeof( u ) );
    write_u32( u );
}

void PacketWriter::write_f64( double v ) {
    uint64_t u;
    std::memcpy( &u, &v, sizeof( u ) );
    write_u64( u );
}

void PacketWriter::write_bytes( const void *data, size_t len ) {
    const auto *bytes = static_cast<const uint8_t *>( data );
    buf_.insert( buf_.end( ), bytes, bytes + len );
}

void PacketWriter::write_block_state( const BlockState &bs ) {
    // Field-by-field; BlockState is { BlockID id; unsigned char rotation;
    // int current_redstone_power; BlockID display_id; unsigned char
    // waterlogged; }. We serialize the logical fields, not the raw struct, so
    // padding/endianness can't drift.
    write_u32( static_cast<uint32_t>( bs.id ) );
    write_u8( bs.rotation );
    write_i32( bs.current_redstone_power );
    write_u32( static_cast<uint32_t>( bs.display_id ) );
    write_u8( bs.waterlogged );
}

// ---------------------------------------------------------------------------
// PacketReader
// ---------------------------------------------------------------------------
bool PacketReader::read_u8( uint8_t &out ) {
    if ( pos_ + 1 > len_ ) return false;
    out = data_[ pos_++ ];
    return true;
}

bool PacketReader::read_u16( uint16_t &out ) {
    if ( pos_ + 2 > len_ ) return false;
    out = static_cast<uint16_t>( data_[ pos_ ] )
          | static_cast<uint16_t>( static_cast<uint16_t>( data_[ pos_ + 1 ] ) << 8 );
    pos_ += 2;
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

bool PacketReader::read_u64( uint64_t &out ) {
    if ( pos_ + 8 > len_ ) return false;
    uint64_t u = 0;
    for ( int i = 0; i < 8; i++ ) {
        u |= static_cast<uint64_t>( data_[ pos_ + i ] ) << ( i * 8 );
    }
    pos_ += 8;
    out = u;
    return true;
}

bool PacketReader::read_i64( int64_t &out ) {
    uint64_t u;
    if ( !read_u64( u ) ) return false;
    out = static_cast<int64_t>( u );
    return true;
}

bool PacketReader::read_f32( float &out ) {
    uint32_t u;
    if ( !read_u32( u ) ) return false;
    std::memcpy( &out, &u, sizeof( out ) );
    return true;
}

bool PacketReader::read_f64( double &out ) {
    uint64_t u;
    if ( !read_u64( u ) ) return false;
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
    out.id = ( id == LEGACY_EMPTY_BLOCK_ID || id >= LAST_BLOCK_ID ) ? AIR : static_cast<BlockID>( id );
    out.rotation = rotation;
    out.current_redstone_power = power;
    out.display_id = ( display_id == LEGACY_EMPTY_BLOCK_ID || display_id >= LAST_BLOCK_ID ) ? out.id : static_cast<BlockID>( display_id );
    uint8_t waterlogged = 0;
    read_u8( waterlogged ); // older peers don't send it — absence means dry
    out.waterlogged = waterlogged;
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
    w.write_f32( p.eye_height );
    w.write_u8( p.flags );
    w.write_f32( p.health );
    w.write_u32( p.held_item );
}

bool net_deserialize_player( PacketReader &r, NetPlayerPayload &p ) {
    if ( !r.read_f32( p.x ) ) return false;
    if ( !r.read_f32( p.y ) ) return false;
    if ( !r.read_f32( p.z ) ) return false;
    for ( int i = 0; i < 16; i++ ) {
        if ( !r.read_f32( p.rotation[ i ] ) ) return false;
    }
    if ( !r.read_f32( p.eye_height ) ) return false;
    p.flags = 0;
    r.read_u8( p.flags ); // older peers don't send it
    p.health = -1.0f;     // sentinel: peer predates health (or truncated)
    r.read_f32( p.health );
    p.held_item = LAST_BLOCK_ID; // empty hand: peer predates held_item
    r.read_u32( p.held_item );
    return true;
}

void net_serialize_block_update( PacketWriter &w, const NetBlockUpdatePayload &p ) {
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
    w.write_block_state( p.blockState );
    w.write_u8( p.flags );
}

bool net_deserialize_block_update( PacketReader &r, NetBlockUpdatePayload &p ) {
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    if ( !r.read_block_state( p.blockState ) ) return false;
    p.flags = 0;
    r.read_u8( p.flags ); // older peers don't send it
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

// InventorySlot on the wire: { u32 item_id, i32 quantity }.
static void net_write_inventory_slot( PacketWriter &w, const InventorySlot &slot ) {
    w.write_u32( slot.item_id );
    w.write_i32( slot.quantity );
}

static bool net_read_inventory_slot( PacketReader &r, InventorySlot &slot ) {
    uint32_t item_id;
    int32_t quantity;
    if ( !r.read_u32( item_id ) ) return false;
    if ( !r.read_i32( quantity ) ) return false;
    if ( !item_id_valid( item_id ) ) {
        // Unknown/out-of-range id: normalize to the empty slot sentinel.
        item_id = LAST_BLOCK_ID;
        quantity = 0;
    }
    slot.item_id = item_id;
    slot.quantity = quantity;
    return true;
}

void net_serialize_container_pos( PacketWriter &w, const NetContainerPosPayload &p ) {
    w.write_u8( static_cast<uint8_t>( p.kind ) );
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
}

bool net_deserialize_container_pos( PacketReader &r, NetContainerPosPayload &p ) {
    uint8_t kind;
    if ( !r.read_u8( kind ) ) return false;
    if ( net_container_kind_def( kind ) == nullptr ) {
        net_pr_debug( "container msg bad kind:%u", kind );
        return false;
    }
    p.kind = static_cast<NetContainerKind>( kind );
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    return true;
}

void net_serialize_container_contents( PacketWriter &w, const NetContainerContentsPayload &p ) {
    w.write_u8( static_cast<uint8_t>( p.kind ) );
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
    w.write_u32( p.revision );
    w.write_u8( static_cast<uint8_t>( p.slots.size( ) ) );
    for ( const InventorySlot &slot : p.slots ) {
        net_write_inventory_slot( w, slot );
    }
    w.write_u8( static_cast<uint8_t>( p.aux.size( ) ) );
    for ( const int32_t a : p.aux ) {
        w.write_i32( a );
    }
}

bool net_deserialize_container_contents( PacketReader &r, NetContainerContentsPayload &p ) {
    uint8_t kind;
    if ( !r.read_u8( kind ) ) return false;
    const NetContainerKindDef *def = net_container_kind_def( kind );
    if ( def == nullptr ) {
        net_pr_debug( "CONTAINER_CONTENTS bad kind:%u", kind );
        return false;
    }
    p.kind = static_cast<NetContainerKind>( kind );
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    if ( !r.read_u32( p.revision ) ) return false;
    // Cap before allocating, then require the kind's exact arity so a
    // kind/payload confusion can't silently desync the stream.
    uint8_t n_slots;
    if ( !r.read_u8( n_slots ) ) return false;
    if ( n_slots > NET_CONTAINER_MAX_SLOTS || n_slots != def->slots ) {
        net_pr_debug( "CONTAINER_CONTENTS kind:%u n_slots:%u != %u", kind, n_slots, def->slots );
        return false;
    }
    p.slots.resize( n_slots );
    for ( int i = 0; i < n_slots; i++ ) {
        if ( !net_read_inventory_slot( r, p.slots[ i ] ) ) return false;
    }
    uint8_t n_aux;
    if ( !r.read_u8( n_aux ) ) return false;
    if ( n_aux > NET_CONTAINER_MAX_AUX || n_aux != def->aux ) {
        net_pr_debug( "CONTAINER_CONTENTS kind:%u n_aux:%u != %u", kind, n_aux, def->aux );
        return false;
    }
    p.aux.resize( n_aux );
    for ( int i = 0; i < n_aux; i++ ) {
        if ( !r.read_i32( p.aux[ i ] ) ) return false;
    }
    return true;
}

void net_serialize_container_set_slot( PacketWriter &w, const NetContainerSetSlotPayload &p ) {
    w.write_u8( static_cast<uint8_t>( p.kind ) );
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
    w.write_u32( p.slot_index );
    net_write_inventory_slot( w, p.slot_value );
    w.write_u32( p.revision );
}

bool net_deserialize_container_set_slot( PacketReader &r, NetContainerSetSlotPayload &p ) {
    uint8_t kind;
    if ( !r.read_u8( kind ) ) return false;
    const NetContainerKindDef *def = net_container_kind_def( kind );
    if ( def == nullptr ) {
        net_pr_debug( "CONTAINER_SET_SLOT bad kind:%u", kind );
        return false;
    }
    p.kind = static_cast<NetContainerKind>( kind );
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    if ( !r.read_u32( p.slot_index ) ) return false;
    if ( p.slot_index >= def->slots ) {
        net_pr_debug( "CONTAINER_SET_SLOT kind:%u bad slot_index:%u (max:%u)", kind, p.slot_index, def->slots );
        return false;
    }
    if ( !net_read_inventory_slot( r, p.slot_value ) ) return false;
    if ( !r.read_u32( p.revision ) ) return false;
    return true;
}

void net_serialize_world_time( PacketWriter &w, const NetWorldTimePayload &p ) {
    w.write_i64( p.world_time );
}

bool net_deserialize_world_time( PacketReader &r, NetWorldTimePayload &p ) {
    return r.read_i64( p.world_time );
}

// ---------------------------------------------------------------------------
// Entity sync.
// ---------------------------------------------------------------------------

void net_write_entity_record( PacketWriter &w, const NetEntityRecord &rec ) {
    w.write_u64( rec.id );
    w.write_u8( static_cast<uint8_t>( rec.kind ) );
    w.write_f64( rec.x );
    w.write_f64( rec.y );
    w.write_f64( rec.z );
    w.write_f32( rec.vx );
    w.write_f32( rec.vy );
    w.write_f32( rec.vz );
    w.write_f32( rec.yaw );
    w.write_u32( rec.type );
    w.write_i32( rec.count );
    w.write_f32( rec.health );
    w.write_i32( rec.age_ticks );
    w.write_i32( rec.pickup_delay );
    w.write_u32( rec.flags );
    w.write_block_state( rec.block_state );
    w.write_i32( rec.fuse_ticks );
}

bool net_read_entity_record( PacketReader &r, NetEntityRecord &rec ) {
    if ( !r.read_u64( rec.id ) ) return false;
    uint8_t kind;
    if ( !r.read_u8( kind ) ) return false;
    rec.kind = static_cast<NetEntityKind>( kind );
    if ( !r.read_f64( rec.x ) ) return false;
    if ( !r.read_f64( rec.y ) ) return false;
    if ( !r.read_f64( rec.z ) ) return false;
    if ( !r.read_f32( rec.vx ) ) return false;
    if ( !r.read_f32( rec.vy ) ) return false;
    if ( !r.read_f32( rec.vz ) ) return false;
    if ( !r.read_f32( rec.yaw ) ) return false;
    if ( !r.read_u32( rec.type ) ) return false;
    if ( !r.read_i32( rec.count ) ) return false;
    if ( !r.read_f32( rec.health ) ) return false;
    if ( !r.read_i32( rec.age_ticks ) ) return false;
    if ( !r.read_i32( rec.pickup_delay ) ) return false;
    if ( !r.read_u32( rec.flags ) ) return false;
    if ( !r.read_block_state( rec.block_state ) ) return false;
    if ( !r.read_i32( rec.fuse_ticks ) ) return false;
    return true;
}

void net_serialize_welcome( PacketWriter &w, const NetWelcomePayload &p ) {
    w.write_u32( p.session_id );
}

bool net_deserialize_welcome( PacketReader &r, NetWelcomePayload &p ) {
    return r.read_u32( p.session_id );
}

void net_serialize_view( PacketWriter &w, const NetViewPayload &p ) {
    w.write_i32( p.cx );
    w.write_i32( p.cy );
    w.write_i32( p.cz );
    w.write_u8( p.radius_xz );
    w.write_u8( p.radius_y );
}

bool net_deserialize_view( PacketReader &r, NetViewPayload &p ) {
    if ( !r.read_i32( p.cx ) ) return false;
    if ( !r.read_i32( p.cy ) ) return false;
    if ( !r.read_i32( p.cz ) ) return false;
    if ( !r.read_u8( p.radius_xz ) ) return false;
    if ( !r.read_u8( p.radius_y ) ) return false;
    return true;
}

void net_serialize_chunk_role( PacketWriter &w, const NetChunkRolePayload &p ) {
    w.write_i32( p.cx );
    w.write_i32( p.cy );
    w.write_i32( p.cz );
    w.write_u8( p.role );
    w.write_u8( p.populated );
    w.write_u32( static_cast<uint32_t>( p.records.size( ) ) );
    for ( const NetEntityRecord &rec : p.records ) {
        net_write_entity_record( w, rec );
    }
    w.write_u8( static_cast<uint8_t>( p.removed_natural.size( ) ) );
    for ( const uint8_t index : p.removed_natural ) {
        w.write_u8( index );
    }
}

bool net_deserialize_chunk_role( PacketReader &r, NetChunkRolePayload &p ) {
    if ( !r.read_i32( p.cx ) ) return false;
    if ( !r.read_i32( p.cy ) ) return false;
    if ( !r.read_i32( p.cz ) ) return false;
    if ( !r.read_u8( p.role ) ) return false;
    if ( !r.read_u8( p.populated ) ) return false;
    uint32_t n;
    if ( !r.read_u32( n ) ) return false;
    if ( n > NET_ENTITY_MAX_LIST ) {
        net_pr_debug( "CHUNK_ROLE n:%u > max:%u", n, NET_ENTITY_MAX_LIST );
        return false;
    }
    p.records.resize( n );
    for ( uint32_t i = 0; i < n; i++ ) {
        if ( !net_read_entity_record( r, p.records[ i ] ) ) return false;
    }
    p.removed_natural.clear( );
    uint8_t removed_n;
    if ( !r.read_u8( removed_n ) ) return false;
    p.removed_natural.reserve( removed_n );
    for ( uint32_t i = 0; i < removed_n; i++ ) {
        uint8_t index;
        if ( !r.read_u8( index ) ) return false;
        p.removed_natural.push_back( index );
    }
    return true;
}

static void write_i16( PacketWriter &w, const int32_t v ) {
    const int32_t c = std::max<int32_t>( -32767, std::min<int32_t>( 32767, v ) );
    w.write_u16( static_cast<uint16_t>( static_cast<int16_t>( c ) ) );
}

void net_state_write_entry( PacketWriter &w, const NetEntityStateEntry &e ) {
    uint8_t mask = e.present & ( NET_SE_POS_ABS | NET_SE_POS_DELTA | NET_SE_VEL | //
                                 NET_SE_YAW | NET_SE_COUNT | NET_SE_HEALTH | NET_SE_FLAGS );
    glm::dvec3 d( 0.0 );
    if ( mask & NET_SE_POS_DELTA ) {
        d = glm::dvec3( e.x, e.y, e.z ) - e.baseline;
        // A delta past the i16 range falls back to an absolute pos.
        if ( glm::any( glm::greaterThan( glm::abs( d ), glm::dvec3( 127.5 ) ) ) ) {
            mask = static_cast<uint8_t>( ( mask & ~NET_SE_POS_DELTA ) | NET_SE_POS_ABS );
        }
    }
    w.write_u8( mask );
    w.write_u64( e.id );
    w.write_u8( e.kind );
    if ( mask & NET_SE_POS_ABS ) {
        w.write_f64( e.x );
        w.write_f64( e.y );
        w.write_f64( e.z );
    } else if ( mask & NET_SE_POS_DELTA ) {
        write_i16( w, lround( d.x * NET_STATE_POS_SCALE ) );
        write_i16( w, lround( d.y * NET_STATE_POS_SCALE ) );
        write_i16( w, lround( d.z * NET_STATE_POS_SCALE ) );
    }
    if ( mask & NET_SE_VEL ) {
        write_i16( w, lround( e.vx * NET_STATE_VEL_SCALE ) );
        write_i16( w, lround( e.vy * NET_STATE_VEL_SCALE ) );
        write_i16( w, lround( e.vz * NET_STATE_VEL_SCALE ) );
    }
    if ( mask & NET_SE_YAW ) {
        const float norm = e.yaw - 360.0f * floorf( e.yaw / 360.0f );
        w.write_u8( static_cast<uint8_t>( lround( norm * ( 256.0f / 360.0f ) ) & 0xFF ) );
    }
    if ( mask & NET_SE_COUNT ) {
        w.write_i32( e.count );
    }
    if ( mask & NET_SE_HEALTH ) {
        w.write_f32( e.health );
    }
    if ( mask & NET_SE_FLAGS ) {
        w.write_u32( e.flags );
    }
}

bool net_state_read_entry( PacketReader &r, NetEntityStateEntry &e, const glm::dvec3 &baseline ) {
    uint8_t mask;
    if ( !r.read_u8( mask ) ) {
        return false;
    }
    e.present = mask;
    e.baseline = baseline;
    if ( !r.read_u64( e.id ) || !r.read_u8( e.kind ) ) {
        return false;
    }
    if ( mask & NET_SE_POS_ABS ) {
        if ( !r.read_f64( e.x ) || !r.read_f64( e.y ) || !r.read_f64( e.z ) ) {
            return false;
        }
    } else if ( mask & NET_SE_POS_DELTA ) {
        uint16_t dx, dy, dz;
        if ( !r.read_u16( dx ) || !r.read_u16( dy ) || !r.read_u16( dz ) ) {
            return false;
        }
        e.x = baseline.x + static_cast<int16_t>( dx ) / NET_STATE_POS_SCALE;
        e.y = baseline.y + static_cast<int16_t>( dy ) / NET_STATE_POS_SCALE;
        e.z = baseline.z + static_cast<int16_t>( dz ) / NET_STATE_POS_SCALE;
    }
    if ( mask & NET_SE_VEL ) {
        uint16_t vx, vy, vz;
        if ( !r.read_u16( vx ) || !r.read_u16( vy ) || !r.read_u16( vz ) ) {
            return false;
        }
        e.vx = static_cast<int16_t>( vx ) / static_cast<float>( NET_STATE_VEL_SCALE );
        e.vy = static_cast<int16_t>( vy ) / static_cast<float>( NET_STATE_VEL_SCALE );
        e.vz = static_cast<int16_t>( vz ) / static_cast<float>( NET_STATE_VEL_SCALE );
    }
    if ( mask & NET_SE_YAW ) {
        uint8_t y;
        if ( !r.read_u8( y ) ) {
            return false;
        }
        e.yaw = static_cast<float>( y ) * ( 360.0f / 256.0f );
    }
    if ( mask & NET_SE_COUNT && !r.read_i32( e.count ) ) {
        return false;
    }
    if ( mask & NET_SE_HEALTH && !r.read_f32( e.health ) ) {
        return false;
    }
    if ( mask & NET_SE_FLAGS && !r.read_u32( e.flags ) ) {
        return false;
    }
    return true;
}

void net_serialize_entity_state( PacketWriter &w, const NetEntityStatePayload &p ) {
    w.write_u16( static_cast<uint16_t>( p.entries.size( ) ) );
    for ( const NetEntityStateEntry &e : p.entries ) {
        net_state_write_entry( w, e );
    }
}

bool net_deserialize_entity_state( PacketReader &r, NetEntityStatePayload &p, NetStateBaselineFn baseline_fn, void *ctx ) {
    uint16_t n;
    if ( !r.read_u16( n ) ) return false;
    if ( n > NET_ENTITY_MAX_LIST ) {
        net_pr_debug( "ENTITY_STATE n:%u > max:%u", n, NET_ENTITY_MAX_LIST );
        return false;
    }
    p.entries.resize( n );
    for ( uint16_t i = 0; i < n; i++ ) {
        NetEntityStateEntry &e = p.entries[ i ];
        glm::dvec3 baseline( 0.0 );
        if ( baseline_fn != nullptr ) {
            // The delta baseline keys off id+kind — peek the entry header
            // (mask u8, id u64, kind u8) without consuming it.
            PacketReader peek = r;
            uint8_t m, k;
            NetEntityId id;
            if ( peek.read_u8( m ) && peek.read_u64( id ) && peek.read_u8( k ) ) {
                baseline_fn( ctx, id, k, baseline );
            }
        }
        if ( !net_state_read_entry( r, e, baseline ) ) {
            return false;
        }
    }
    return true;
}

bool net_deserialize_entity_state( PacketReader &r, NetEntityStatePayload &p ) {
    return net_deserialize_entity_state( r, p, nullptr, nullptr );
}

void net_serialize_entity_remove( PacketWriter &w, const NetEntityRemovePayload &p ) {
    w.write_u64( p.id );
    w.write_u8( p.reason );
}

bool net_deserialize_entity_remove( PacketReader &r, NetEntityRemovePayload &p ) {
    if ( !r.read_u64( p.id ) ) return false;
    if ( !r.read_u8( p.reason ) ) return false;
    return true;
}

void net_serialize_entity_transfer( PacketWriter &w, const NetEntityTransferPayload &p ) {
    net_write_entity_record( w, p.record );
    w.write_u8( p.adopt );
}

bool net_deserialize_entity_transfer( PacketReader &r, NetEntityTransferPayload &p ) {
    if ( !net_read_entity_record( r, p.record ) ) return false;
    if ( !r.read_u8( p.adopt ) ) return false;
    return true;
}

void net_serialize_entity_action( PacketWriter &w, const NetEntityActionPayload &p ) {
    w.write_u64( p.id );
    w.write_u8( p.action );
    w.write_f32( p.damage );
    w.write_f64( p.ax );
    w.write_f64( p.ay );
    w.write_f64( p.az );
    w.write_u8( p.attacker_survival );
    w.write_u32( p.item );
}

bool net_deserialize_entity_action( PacketReader &r, NetEntityActionPayload &p ) {
    if ( !r.read_u64( p.id ) ) return false;
    if ( !r.read_u8( p.action ) ) return false;
    if ( !r.read_f32( p.damage ) ) return false;
    if ( !r.read_f64( p.ax ) ) return false;
    if ( !r.read_f64( p.ay ) ) return false;
    if ( !r.read_f64( p.az ) ) return false;
    if ( !r.read_u8( p.attacker_survival ) ) return false;
    if ( !r.read_u32( p.item ) ) return false;
    return true;
}

void net_serialize_pickup_request( PacketWriter &w, const NetPickupRequestPayload &p ) {
    w.write_u64( p.id );
    w.write_i32( p.room );
}

bool net_deserialize_pickup_request( PacketReader &r, NetPickupRequestPayload &p ) {
    if ( !r.read_u64( p.id ) ) return false;
    if ( !r.read_i32( p.room ) ) return false;
    return true;
}

void net_serialize_pickup_result( PacketWriter &w, const NetPickupResultPayload &p ) {
    w.write_u64( p.id );
    w.write_u32( p.item );
    w.write_i32( p.granted );
}

bool net_deserialize_pickup_result( PacketReader &r, NetPickupResultPayload &p ) {
    if ( !r.read_u64( p.id ) ) return false;
    if ( !r.read_u32( p.item ) ) return false;
    if ( !r.read_i32( p.granted ) ) return false;
    return true;
}

void net_serialize_player_hit( PacketWriter &w, const NetPlayerHitPayload &p ) {
    w.write_i32( p.target_player_id );
    w.write_f64( p.fx );
    w.write_f64( p.fy );
    w.write_f64( p.fz );
    w.write_f32( p.damage );
}

bool net_deserialize_player_hit( PacketReader &r, NetPlayerHitPayload &p ) {
    if ( !r.read_i32( p.target_player_id ) ) return false;
    if ( !r.read_f64( p.fx ) ) return false;
    if ( !r.read_f64( p.fy ) ) return false;
    if ( !r.read_f64( p.fz ) ) return false;
    if ( !r.read_f32( p.damage ) ) return false;
    return true;
}

void net_serialize_explosion( PacketWriter &w, const NetExplosionPayload &p ) {
    w.write_f64( p.x );
    w.write_f64( p.y );
    w.write_f64( p.z );
    w.write_f32( p.radius );
}

bool net_deserialize_explosion( PacketReader &r, NetExplosionPayload &p ) {
    if ( !r.read_f64( p.x ) ) return false;
    if ( !r.read_f64( p.y ) ) return false;
    if ( !r.read_f64( p.z ) ) return false;
    if ( !r.read_f32( p.radius ) ) return false;
    return true;
}

void net_serialize_chunk_populated( PacketWriter &w, const NetChunkPopulatedPayload &p ) {
    w.write_i32( p.cx );
    w.write_i32( p.cy );
    w.write_i32( p.cz );
}

bool net_deserialize_chunk_populated( PacketReader &r, NetChunkPopulatedPayload &p ) {
    if ( !r.read_i32( p.cx ) ) return false;
    if ( !r.read_i32( p.cy ) ) return false;
    if ( !r.read_i32( p.cz ) ) return false;
    return true;
}

void net_serialize_sign_text( PacketWriter &w, const NetSignTextPayload &p ) {
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
    for ( int i = 0; i < SIGN_LINES; i++ ) {
        const std::string &line = p.text.lines[ i ];
        const size_t n = std::min( line.size( ), static_cast<size_t>( SIGN_LINE_MAX ) );
        w.write_u8( static_cast<uint8_t>( n ) );
        w.write_bytes( line.data( ), n );
    }
}

bool net_deserialize_sign_text( PacketReader &r, NetSignTextPayload &p ) {
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    for ( int i = 0; i < SIGN_LINES; i++ ) {
        uint8_t n;
        if ( !r.read_u8( n ) ) return false;
        // The bound check runs before the read/reserve so a corrupt length
        // can't force a big allocation or eat the rest of the frame.
        if ( n > SIGN_LINE_MAX ) {
            net_pr_debug( "SIGN_TEXT line:%d len:%u > %u", i, n, SIGN_LINE_MAX );
            return false;
        }
        p.text.lines[ i ].resize( n );
        if ( !r.read_bytes( p.text.lines[ i ].data( ), n ) ) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Codec registry: deterministic fill samples + the per-type table
// (see the registry comment in packet.hpp for the rules).
// ---------------------------------------------------------------------------

static void net_fill_record( NetEntityRecord &rec ) {
    rec.id = 0x1122334455667788ull;
    rec.kind = NetEntityKind::MOB;
    rec.x = 1.5;
    rec.y = 64.0;
    rec.z = -2.5;
    rec.vx = 0.1f;
    rec.vy = -0.2f;
    rec.vz = 0.3f;
    rec.yaw = 1.0f;
    rec.type = 3;
    rec.count = 2;
    rec.health = 9.5f;
    rec.age_ticks = -100;
    rec.pickup_delay = 40;
    rec.flags = NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_HURT;
    rec.block_state = { SAND, 0, 0, SAND, 0 };
    rec.fuse_ticks = 30;
}

void NetCodec<NetPlayerPayload>::fill( NetPlayerPayload &p ) {
    p.x = 12.5f;
    p.y = -4.25f;
    p.z = 1000.5f;
    for ( int i = 0; i < 16; i++ ) {
        p.rotation[ i ] = static_cast<float>( i ) + 0.5f;
    }
    p.eye_height = 1.5f;
    p.flags = NET_PLAYER_FLAG_TARGETABLE;
    p.health = 17.5f;
    p.held_item = STICK;
}
void NetCodec<NetBlockUpdatePayload>::fill( NetBlockUpdatePayload &p ) {
    p.x = 100;
    p.y = -20;
    p.z = 3000;
    p.blockState = { STONE, 3, 7, DIRT, 1 };
    p.flags = NET_BU_FLAG_NO_DEBRIS;
}
void NetCodec<NetChunkDiffRequestPayload>::fill( NetChunkDiffRequestPayload &p ) {
    p.min_x = -3;
    p.min_y = 0;
    p.min_z = 7;
    p.size_x = 4;
    p.size_y = 2;
    p.size_z = 5;
}
void NetCodec<NetChunkDiffResultPayload>::fill( NetChunkDiffResultPayload &p ) {
    p.chunk_x = 1;
    p.chunk_y = -1;
    p.chunk_z = 2;
    p.num_diffs = 2;
    p.diffs.push_back( { 5, { GRASS, 1, 0, GRASS, 0 } } );
    p.diffs.push_back( { 77, { WATER, 0, 2, WATER, 1 } } );
}
void NetCodec<NetContainerPosPayload>::fill( NetContainerPosPayload &p ) {
    p.kind = NetContainerKind::CHEST;
    p.x = 64;
    p.y = 32;
    p.z = -128;
}
void NetCodec<NetContainerContentsPayload>::fill( NetContainerContentsPayload &p ) {
    p.kind = NetContainerKind::FURNACE;
    p.x = -1;
    p.y = 9;
    p.z = 12;
    p.revision = 7;
    p.slots = { { STONE, 5 }, { WHEAT, 2 }, { STICK, 1 } };
    p.aux = { 42, 99 };
}
void NetCodec<NetContainerSetSlotPayload>::fill( NetContainerSetSlotPayload &p ) {
    p.kind = NetContainerKind::CHEST;
    p.x = 4;
    p.y = 5;
    p.z = 6;
    p.slot_index = 3;
    p.slot_value = { STONE, 8 };
    p.revision = 11;
}
void NetCodec<NetWorldTimePayload>::fill( NetWorldTimePayload &p ) {
    p.world_time = 123456789;
}
void NetCodec<NetWelcomePayload>::fill( NetWelcomePayload &p ) {
    p.session_id = 7;
}
void NetCodec<NetViewPayload>::fill( NetViewPayload &p ) {
    p.cx = -4;
    p.cy = 0;
    p.cz = 9;
    p.radius_xz = NET_ENTITY_RADIUS_XZ;
    p.radius_y = NET_ENTITY_RADIUS_Y;
}
void NetCodec<NetChunkRolePayload>::fill( NetChunkRolePayload &p ) {
    p.cx = 11;
    p.cy = -2;
    p.cz = 33;
    p.role = NET_ROLE_OWNER;
    p.populated = 1;
    NetEntityRecord rec;
    net_fill_record( rec );
    p.records.push_back( rec );
    rec.id = 0x22;
    rec.kind = NetEntityKind::ITEM;
    p.records.push_back( rec );
    p.removed_natural.push_back( 0 );
    p.removed_natural.push_back( 3 );
    p.removed_natural.push_back( 7 );
}
void NetCodec<NetEntityRecord>::fill( NetEntityRecord &p ) {
    net_fill_record( p );
}
void NetCodec<NetEntityStatePayload>::fill( NetEntityStatePayload &p ) {
    // Values must survive the quantized wire fields (pos deltas snap to
    // 1/256, vel to 1/4096, yaw to 360/256) so de->ser reproduces bytes.
    NetEntityStateEntry e;
    e.id = 42;
    e.kind = static_cast<uint8_t>( NetEntityKind::MOB );
    e.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_YAW | NET_SE_COUNT | NET_SE_HEALTH | NET_SE_FLAGS;
    e.x = 1.0;
    e.y = 2.0;
    e.z = 3.0;
    e.vx = 0.5f;
    e.vy = -0.5f;
    e.vz = 0.25f;
    e.yaw = 45.0f;
    e.count = 7;
    e.health = 8.0f;
    e.flags = NET_MOB_FLAG_TOUCHED;
    p.entries.push_back( e );
    // Delta-mode entry (baseline {0,0,0}, so the delta is the pos itself).
    e.id = 43;
    e.kind = static_cast<uint8_t>( NetEntityKind::ITEM );
    e.present = NET_SE_POS_DELTA | NET_SE_VEL;
    e.baseline = glm::dvec3( 0.0 );
    e.x = 0.5;
    e.y = -0.25;
    e.z = 1.0;
    e.count = 0;
    e.health = 0.0f;
    e.flags = 0;
    p.entries.push_back( e );
}
void NetCodec<NetEntityRemovePayload>::fill( NetEntityRemovePayload &p ) {
    p.id = 1234;
    p.reason = NET_REMOVE_DIED;
}
void NetCodec<NetEntityTransferPayload>::fill( NetEntityTransferPayload &p ) {
    net_fill_record( p.record );
    p.adopt = 1;
}
void NetCodec<NetEntityActionPayload>::fill( NetEntityActionPayload &p ) {
    p.id = 88;
    p.action = NET_ACTION_FEED;
    p.damage = 4.5f;
    p.ax = 1.0;
    p.ay = 2.0;
    p.az = 3.0;
    p.attacker_survival = 1;
    p.item = WHEAT;
}
void NetCodec<NetPickupRequestPayload>::fill( NetPickupRequestPayload &p ) {
    p.id = 99;
    p.room = 16;
}
void NetCodec<NetPickupResultPayload>::fill( NetPickupResultPayload &p ) {
    p.id = 99;
    p.item = STICK;
    p.granted = 12;
}
void NetCodec<NetPlayerHitPayload>::fill( NetPlayerHitPayload &p ) {
    p.target_player_id = 3;
    p.fx = 1.0;
    p.fy = 2.0;
    p.fz = 3.0;
    p.damage = 4.5f;
}
void NetCodec<NetExplosionPayload>::fill( NetExplosionPayload &p ) {
    p.x = 10.5;
    p.y = 64.0;
    p.z = -3.25;
    p.radius = 4.0f;
}
void NetCodec<NetChunkPopulatedPayload>::fill( NetChunkPopulatedPayload &p ) {
    p.cx = 5;
    p.cy = 0;
    p.cz = -9;
}
void NetCodec<NetSignTextPayload>::fill( NetSignTextPayload &p ) {
    p.x = -7;
    p.y = 70;
    p.z = 42;
    p.text.lines[ 0 ] = "Hello";
    p.text.lines[ 1 ] = "sign text!";
    p.text.lines[ 2 ] = "15 chars max ok!";
    p.text.lines[ 3 ] = "";
}

namespace {
template <typename P>
void *codec_make_wrap( ) {
    P *p = new P( );
    NetCodec<P>::fill( *p );
    return p;
}
template <typename P>
void codec_destroy_wrap( void *p ) {
    delete static_cast<P *>( p );
}
template <typename P>
void codec_ser_wrap( PacketWriter &w, const void *p ) {
    NetCodec<P>::ser( w, *static_cast<const P *>( p ) );
}
template <typename P>
bool codec_de_wrap( PacketReader &r, void *p ) {
    return NetCodec<P>::de( r, *static_cast<P *>( p ) );
}

#define NET_ROW( TYPE, P ) \
    { TYPE, #TYPE, sizeof( P ), codec_make_wrap<P>, codec_destroy_wrap<P>, codec_ser_wrap<P>, codec_de_wrap<P> }
#define NET_EMPTY_ROW( TYPE ) \
    { TYPE, #TYPE, 0, nullptr, nullptr, nullptr, nullptr }
// Retired wire id (pre-v9 chest/furnace messages): name kept for logging.
#define NET_RETIRED_ROW( ID, LABEL ) \
    { static_cast<NetMsgType>( ID ), LABEL, 0, nullptr, nullptr, nullptr, nullptr }

const NetCodecEntry kCodecTable[] = {
    NET_ROW( NetMsgType::PLAYER_LOCATION, NetPlayerPayload ),
    NET_ROW( NetMsgType::BLOCK_UPDATE, NetBlockUpdatePayload ),
    NET_ROW( NetMsgType::CLIENT_INIT, NetPlayerPayload ),
    NET_EMPTY_ROW( NetMsgType::PLAYER_CONNECTED ),
    NET_EMPTY_ROW( NetMsgType::PLAYER_DISCONNECTED ),
    NET_ROW( NetMsgType::CHUNK_DIFF_REQUEST, NetChunkDiffRequestPayload ),
    NET_ROW( NetMsgType::CHUNK_DIFF_RESULT, NetChunkDiffResultPayload ),
    NET_RETIRED_ROW( 7, "RETIRED_CHEST_OPEN" ),
    NET_RETIRED_ROW( 8, "RETIRED_CHEST_CLOSE" ),
    NET_RETIRED_ROW( 9, "RETIRED_CHEST_CONTENTS" ),
    NET_RETIRED_ROW( 10, "RETIRED_CHEST_SET_SLOT" ),
    NET_ROW( NetMsgType::WORLD_TIME, NetWorldTimePayload ),
    NET_ROW( NetMsgType::WELCOME, NetWelcomePayload ),
    NET_ROW( NetMsgType::VIEW, NetViewPayload ),
    NET_ROW( NetMsgType::CHUNK_ROLE, NetChunkRolePayload ),
    NET_ROW( NetMsgType::ENTITY_SPAWN, NetEntityRecord ),
    NET_ROW( NetMsgType::ENTITY_STATE, NetEntityStatePayload ),
    NET_ROW( NetMsgType::ENTITY_REMOVE, NetEntityRemovePayload ),
    NET_ROW( NetMsgType::ENTITY_TRANSFER, NetEntityTransferPayload ),
    NET_ROW( NetMsgType::ENTITY_ACTION, NetEntityActionPayload ),
    NET_ROW( NetMsgType::PICKUP_REQUEST, NetPickupRequestPayload ),
    NET_ROW( NetMsgType::PICKUP_RESULT, NetPickupResultPayload ),
    NET_ROW( NetMsgType::PLAYER_HIT, NetPlayerHitPayload ),
    NET_ROW( NetMsgType::EXPLOSION, NetExplosionPayload ),
    NET_ROW( NetMsgType::CHUNK_POPULATED, NetChunkPopulatedPayload ),
    NET_RETIRED_ROW( 25, "RETIRED_FURNACE_OPEN" ),
    NET_RETIRED_ROW( 26, "RETIRED_FURNACE_CLOSE" ),
    NET_RETIRED_ROW( 27, "RETIRED_FURNACE_CONTENTS" ),
    NET_RETIRED_ROW( 28, "RETIRED_FURNACE_SET_SLOT" ),
    NET_ROW( NetMsgType::CONTAINER_OPEN, NetContainerPosPayload ),
    NET_ROW( NetMsgType::CONTAINER_CLOSE, NetContainerPosPayload ),
    NET_ROW( NetMsgType::CONTAINER_CONTENTS, NetContainerContentsPayload ),
    NET_ROW( NetMsgType::CONTAINER_SET_SLOT, NetContainerSetSlotPayload ),
    NET_ROW( NetMsgType::SIGN_TEXT, NetSignTextPayload ),
};
} // namespace

const NetCodecEntry *net_codec_table( size_t *count ) {
    *count = sizeof( kCodecTable ) / sizeof( kCodecTable[ 0 ] );
    return kCodecTable;
}

const char *net_msg_type_name( NetMsgType type ) {
    const size_t idx = static_cast<size_t>( type );
    if ( idx < sizeof( kCodecTable ) / sizeof( kCodecTable[ 0 ] ) && kCodecTable[ idx ].type == type ) {
        return kCodecTable[ idx ].name;
    }
    return "UNKNOWN_MSG";
}

bool net_trace_enabled( ) {
    static const bool on = env_enabled( "REPGAME_NET_TRACE" );
    return on;
}
