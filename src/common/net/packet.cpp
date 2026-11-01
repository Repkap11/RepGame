#include "common/net/packet.hpp"

#include "common/constants.hpp"
#include "common/item.hpp"

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

void net_serialize_chest_pos( PacketWriter &w, const NetChestPosPayload &p ) {
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
}

bool net_deserialize_chest_pos( PacketReader &r, NetChestPosPayload &p ) {
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    return true;
}

void net_serialize_chest_contents( PacketWriter &w, const NetChestContentsPayload &p ) {
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
    for ( int i = 0; i < CHEST_SLOTS; i++ ) {
        net_write_inventory_slot( w, p.contents.slots[ i ] );
    }
}

bool net_deserialize_chest_contents( PacketReader &r, NetChestContentsPayload &p ) {
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    for ( int i = 0; i < CHEST_SLOTS; i++ ) {
        if ( !net_read_inventory_slot( r, p.contents.slots[ i ] ) ) return false;
    }
    return true;
}

void net_serialize_chest_set_slot( PacketWriter &w, const NetChestSetSlotPayload &p ) {
    w.write_i32( p.x );
    w.write_i32( p.y );
    w.write_i32( p.z );
    w.write_u32( p.slot_index );
    net_write_inventory_slot( w, p.slot_value );
}

bool net_deserialize_chest_set_slot( PacketReader &r, NetChestSetSlotPayload &p ) {
    if ( !r.read_i32( p.x ) ) return false;
    if ( !r.read_i32( p.y ) ) return false;
    if ( !r.read_i32( p.z ) ) return false;
    if ( !r.read_u32( p.slot_index ) ) return false;
    if ( p.slot_index >= static_cast<uint32_t>( CHEST_SLOTS ) ) {
        net_pr_debug( "CHEST_SET_SLOT bad slot_index:%u (max:%d)", p.slot_index, CHEST_SLOTS );
        return false;
    }
    if ( !net_read_inventory_slot( r, p.slot_value ) ) return false;
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
    return true;
}

void net_serialize_entity_state( PacketWriter &w, const NetEntityStatePayload &p ) {
    w.write_u16( static_cast<uint16_t>( p.entries.size( ) ) );
    for ( const NetEntityStateEntry &e : p.entries ) {
        w.write_u64( e.id );
        w.write_u8( e.kind );
        w.write_f64( e.x );
        w.write_f64( e.y );
        w.write_f64( e.z );
        w.write_f32( e.vx );
        w.write_f32( e.vy );
        w.write_f32( e.vz );
        w.write_f32( e.yaw );
        w.write_i32( e.count );
        w.write_f32( e.health );
        w.write_u32( e.flags );
    }
}

bool net_deserialize_entity_state( PacketReader &r, NetEntityStatePayload &p ) {
    uint16_t n;
    if ( !r.read_u16( n ) ) return false;
    if ( n > NET_ENTITY_MAX_LIST ) {
        net_pr_debug( "ENTITY_STATE n:%u > max:%u", n, NET_ENTITY_MAX_LIST );
        return false;
    }
    p.entries.resize( n );
    for ( uint16_t i = 0; i < n; i++ ) {
        NetEntityStateEntry &e = p.entries[ i ];
        if ( !r.read_u64( e.id ) ) return false;
        if ( !r.read_u8( e.kind ) ) return false;
        if ( !r.read_f64( e.x ) ) return false;
        if ( !r.read_f64( e.y ) ) return false;
        if ( !r.read_f64( e.z ) ) return false;
        if ( !r.read_f32( e.vx ) ) return false;
        if ( !r.read_f32( e.vy ) ) return false;
        if ( !r.read_f32( e.vz ) ) return false;
        if ( !r.read_f32( e.yaw ) ) return false;
        if ( !r.read_i32( e.count ) ) return false;
        if ( !r.read_f32( e.health ) ) return false;
        if ( !r.read_u32( e.flags ) ) return false;
    }
    return true;
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
}

bool net_deserialize_entity_action( PacketReader &r, NetEntityActionPayload &p ) {
    if ( !r.read_u64( p.id ) ) return false;
    if ( !r.read_u8( p.action ) ) return false;
    if ( !r.read_f32( p.damage ) ) return false;
    if ( !r.read_f64( p.ax ) ) return false;
    if ( !r.read_f64( p.ay ) ) return false;
    if ( !r.read_f64( p.az ) ) return false;
    if ( !r.read_u8( p.attacker_survival ) ) return false;
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
