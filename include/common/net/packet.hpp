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
#include "common/chest.hpp"
#include "common/constants.hpp"

// CHUNK_BLOCK_SIZE is defined in chunk.hpp as (CHUNK_SIZE_INTERNAL_X *
// CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_Z) = 34*34*34 = 39304. We avoid
// pulling chunk.hpp (which drags in renderer headers) into the net module, so
// restate the bound here and assert it matches.
static constexpr int32_t NET_CHUNK_BLOCK_SIZE = ( CHUNK_SIZE_X + 2 ) * ( CHUNK_SIZE_Y + 2 ) * ( CHUNK_SIZE_Z + 2 );

// Wire-format constants. Keep in sync with framed_socket.cpp.
static constexpr uint32_t NET_MAGIC = 0x52475054u; // 'R','G','P','T' little-endian
static constexpr uint8_t NET_PROTOCOL_VERSION = 6;
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
    CHEST_OPEN = 7,
    CHEST_CLOSE = 8,
    CHEST_CONTENTS = 9,
    CHEST_SET_SLOT = 10,
    WORLD_TIME = 11,
    WELCOME = 12,
    VIEW = 13,
    CHUNK_ROLE = 14,
    ENTITY_SPAWN = 15,
    ENTITY_STATE = 16,
    ENTITY_REMOVE = 17,
    ENTITY_TRANSFER = 18,
    ENTITY_ACTION = 19,
    PICKUP_REQUEST = 20,
    PICKUP_RESULT = 21,
    PLAYER_HIT = 22,
    EXPLOSION = 23,
    CHUNK_POPULATED = 24,
};

// ---------------------------------------------------------------------------
// Entity sync ("server arbitrates, owner client simulates").
// A NetEntityId is (session_id << 32) | counter; session 0 ids are minted by
// the server for disk-loaded entities.
// ---------------------------------------------------------------------------
typedef uint64_t NetEntityId;

enum class NetEntityKind : uint8_t {
    MOB = 0,
    ITEM = 1,
    BODY = 2,
};

// Entity sync window (half-extent in chunks, chebyshev). The client clamps
// its VIEW radius to these.
static constexpr int NET_ENTITY_RADIUS_XZ = 8;
static constexpr int NET_ENTITY_RADIUS_Y = 3;
// How often an owner streams ENTITY_STATE for its entities (~24 Hz).
static constexpr int NET_ENTITY_STATE_INTERVAL_TICKS = 6;
// Bound on batched record/state lists per message.
static constexpr uint32_t NET_ENTITY_MAX_LIST = 4096;

// CHUNK_ROLE roles.
enum NetChunkRole : uint8_t {
    NET_ROLE_NONE = 0,
    NET_ROLE_VIEWER = 1,
    NET_ROLE_OWNER = 2,
};

// ENTITY_REMOVE reasons.
enum NetEntityRemoveReason : uint8_t {
    NET_REMOVE_DESPAWN = 0,
    NET_REMOVE_DIED = 1,
    NET_REMOVE_MERGED = 2,
    NET_REMOVE_LANDED = 3,
    NET_REMOVE_EXPLODED = 4,
    NET_REMOVE_PICKED_UP = 5,
    NET_REMOVE_LEFT_VIEW = 6,
};

// ENTITY_ACTION actions.
enum NetEntityAction : uint8_t {
    NET_ACTION_HIT = 0,
};

// NetPlayerPayload flags (bit0: targetable = !peaceful && !no_clip).
static constexpr uint8_t NET_PLAYER_FLAG_TARGETABLE = 0x1;

// NetEntityRecord.flags / NetEntityStateEntry.flags bits for MOB entities.
// Low bits are live anim state (not persisted); the high bits are spawn
// provenance persisted through the records.
static constexpr uint32_t NET_MOB_FLAG_HURT = 1u << 0;        // hurt_ticks > 0
static constexpr uint32_t NET_MOB_FLAG_DYING = 1u << 1;       // death anim playing
static constexpr uint32_t NET_MOB_FLAG_FUSE = 1u << 2;        // creeper fuse lit
static constexpr uint32_t NET_MOB_FLAG_DESPAWNABLE = 1u << 8; // night-spawner mob, never persisted
static constexpr uint32_t NET_MOB_FLAG_NATURAL = 1u << 9;     // deterministic natural-spawn roll
static constexpr uint32_t NET_MOB_FLAG_TOUCHED = 1u << 10;    // player interacted: a natural mob becomes persistable

// Deterministic id for a natural-spawn roll product: top byte 0xFF keeps it
// out of the client (session<<32, session >= 1) and server (session 0) id
// spaces, while the rest packs the home chunk + the roll index so the id is
// invertible — tombstones and persistence key off the index alone.
//   [63:56] 0xFF | [55:35] chunk.x s21 | [34:14] chunk.z s21 | [13:6] chunk.y s8 | [5:0] index u6
// spawn_mobs emits at most 16 records per chunk (4 passive types x herd 4),
// so the 6-bit index never overflows in practice.
inline NetEntityId net_natural_mob_id( const glm::ivec3 &chunk, int index ) {
    return ( 0xFFull << 56 ) |                                                            //
           ( ( static_cast<uint64_t>( chunk.x ) & 0x1FFFFFull ) << 35 ) |                 //
           ( ( static_cast<uint64_t>( chunk.z ) & 0x1FFFFFull ) << 14 ) |                 //
           ( ( static_cast<uint64_t>( chunk.y ) & 0xFFull ) << 6 ) |                      //
           ( static_cast<uint64_t>( index ) & 0x3Full );
}
inline bool net_is_natural_mob_id( NetEntityId id ) {
    return ( id >> 56 ) == 0xFFull;
}
inline glm::ivec3 net_natural_mob_home( NetEntityId id ) {
    const auto sign_extend = []( uint64_t v, int bits ) {
        v &= ( 1ull << bits ) - 1;
        const uint64_t m = 1ull << ( bits - 1 );
        return static_cast<int32_t>( ( v ^ m ) - m );
    };
    return glm::ivec3( sign_extend( id >> 35, 21 ), sign_extend( id >> 6, 8 ), sign_extend( id >> 14, 21 ) );
}
inline int net_natural_mob_index( NetEntityId id ) {
    return static_cast<int>( id & 0x3Full );
}

// Chunk assignment for net entities. Spawns and records use the plain
// chunk; streamed positions advance through net_entity_next_chunk so the
// server and every client apply the same boundary hysteresis — an entity
// resting within NET_ENTITY_CHUNK_MARGIN of a boundary stays in its
// current chunk instead of bouncing between owners.
inline glm::ivec3 net_entity_chunk_of( const glm::dvec3 &pos ) {
    return glm::ivec3( glm::floor( pos / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
}

static constexpr double NET_ENTITY_CHUNK_MARGIN = 0.5;

// Chunk the entity occupies after a streamed ENTITY_STATE at pos, given
// the record currently sits in cur. A chunk axis only changes once the
// entity sits at least NET_ENTITY_CHUNK_MARGIN inside the new chunk.
inline glm::ivec3 net_entity_next_chunk( const glm::ivec3 &cur, const glm::dvec3 &pos ) {
    glm::ivec3 next = net_entity_chunk_of( pos );
    if ( next == cur ) {
        return cur;
    }
    const double sz[ 3 ] = { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z };
    const double p[ 3 ] = { pos.x, pos.y, pos.z };
    for ( int a = 0; a < 3; a++ ) {
        const double fr = p[ a ] - next[ a ] * sz[ a ];
        if ( next[ a ] > cur[ a ] && fr < NET_ENTITY_CHUNK_MARGIN ) {
            next[ a ] = cur[ a ]; // still pressed on the boundary
        } else if ( next[ a ] < cur[ a ] && fr > sz[ a ] - NET_ENTITY_CHUNK_MARGIN ) {
            next[ a ] = cur[ a ];
        }
    }
    return next;
}

// ---------------------------------------------------------------------------
// PacketWriter: append-only little-endian byte buffer.
// ---------------------------------------------------------------------------
class PacketWriter {
  public:
    PacketWriter( ) = default;
    explicit PacketWriter( NetMsgType type, int32_t player_id );

    void write_u8( uint8_t v );
    void write_u16( uint16_t v );
    void write_u32( uint32_t v );
    void write_i32( int32_t v );
    void write_u64( uint64_t v );
    void write_i64( int64_t v );
    void write_f32( float v );
    void write_f64( double v );
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
    bool read_u16( uint16_t &out );
    bool read_u32( uint32_t &out );
    bool read_i32( int32_t &out );
    bool read_u64( uint64_t &out );
    bool read_i64( int64_t &out );
    bool read_f32( float &out );
    bool read_f64( double &out );
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
    float eye_height; // feet-to-eye distance (drops while sneaking)
    uint8_t flags;    // NET_PLAYER_FLAG_*
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

// CHEST_OPEN / CHEST_CLOSE share one payload: the chest's world block pos.
struct NetChestPosPayload {
    int32_t x, y, z;
};
void net_serialize_chest_pos( PacketWriter &w, const NetChestPosPayload &p );
bool net_deserialize_chest_pos( PacketReader &r, NetChestPosPayload &p );

// CHEST_CONTENTS (server -> client): full fixed-size slot array, always
// CHEST_SLOTS entries.
struct NetChestContentsPayload {
    int32_t x, y, z;
    ChestContents contents;
};
void net_serialize_chest_contents( PacketWriter &w, const NetChestContentsPayload &p );
bool net_deserialize_chest_contents( PacketReader &r, NetChestContentsPayload &p );

// CHEST_SET_SLOT (both directions): one optimistic slot write.
struct NetChestSetSlotPayload {
    int32_t x, y, z;
    uint32_t slot_index;
    InventorySlot slot_value;
};
void net_serialize_chest_set_slot( PacketWriter &w, const NetChestSetSlotPayload &p );
bool net_deserialize_chest_set_slot( PacketReader &r, NetChestSetSlotPayload &p );

// WORLD_TIME (both directions): server -> client is an authoritative sync of
// the day/night clock; client -> server is a set request (e.g. PageUp/PgDn).
// world_time is in RepGame::tick units (DAY_LENGTH_TICKS per day).
struct NetWorldTimePayload {
    int64_t world_time;
};
void net_serialize_world_time( PacketWriter &w, const NetWorldTimePayload &p );
bool net_deserialize_world_time( PacketReader &r, NetWorldTimePayload &p );

// ---------------------------------------------------------------------------
// Entity sync messages (see NetEntityKind above).
// ---------------------------------------------------------------------------

// One record shape for all entity kinds; unused per-kind fields are 0.
//   pos    MOB: feet, ITEM: box bottom-center, BODY: min corner
//   type   MOB: MobType, ITEM: ItemID, BODY: BodyKind
//   count  ITEM: stack count, MOB: variant
//   flags  MOB: MobFileRecord.flags + anim bits, BODY: unused
struct NetEntityRecord {
    NetEntityId id = 0;
    NetEntityKind kind = NetEntityKind::MOB;
    double x = 0.0, y = 0.0, z = 0.0;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float yaw = 0.0f;
    uint32_t type = 0;
    int32_t count = 0;
    float health = 0.0f;
    int32_t age_ticks = 0;
    int32_t pickup_delay = 0;
    uint32_t flags = 0;
    BlockState block_state = { AIR, 0, 0, AIR, 0 };
    int32_t fuse_ticks = 0;
};
void net_write_entity_record( PacketWriter &w, const NetEntityRecord &rec );
bool net_read_entity_record( PacketReader &r, NetEntityRecord &rec );

// WELCOME (server -> client): session id for minting NetEntityIds.
struct NetWelcomePayload {
    uint32_t session_id;
};
void net_serialize_welcome( PacketWriter &w, const NetWelcomePayload &p );
bool net_deserialize_welcome( PacketReader &r, NetWelcomePayload &p );

// VIEW (client -> server): view center chunk + radius (client clamps to
// NET_ENTITY_RADIUS_*).
struct NetViewPayload {
    int32_t cx, cy, cz;
    uint8_t radius_xz, radius_y;
};
void net_serialize_view( PacketWriter &w, const NetViewPayload &p );
bool net_deserialize_view( PacketReader &r, NetViewPayload &p );

// CHUNK_ROLE (server -> client): role grant/revoke for one chunk, with the
// chunk's authoritative entity records for VIEWER/OWNER (n=0 for NONE).
// `populated` means the natural-spawn roll already ran during this server
// coverage; `removed_natural` lists roll indices tombstoned by touched
// naturals (never re-rolled).
struct NetChunkRolePayload {
    int32_t cx, cy, cz;
    uint8_t role;
    uint8_t populated;
    std::vector<NetEntityRecord> records;
    std::vector<uint8_t> removed_natural;
};
void net_serialize_chunk_role( PacketWriter &w, const NetChunkRolePayload &p );
bool net_deserialize_chunk_role( PacketReader &r, NetChunkRolePayload &p );

// ENTITY_SPAWN (client -> server -> viewers): one record.
// Serialization is net_write_entity_record / net_read_entity_record.

// ENTITY_STATE (owner -> server -> viewers; also server-originated):
// lightweight per-entity update.
struct NetEntityStateEntry {
    NetEntityId id = 0;
    uint8_t kind = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float yaw = 0.0f;
    int32_t count = 0;
    float health = 0.0f;
    uint32_t flags = 0;
};
struct NetEntityStatePayload {
    std::vector<NetEntityStateEntry> entries;
};
void net_serialize_entity_state( PacketWriter &w, const NetEntityStatePayload &p );
bool net_deserialize_entity_state( PacketReader &r, NetEntityStatePayload &p );

// ENTITY_REMOVE (owner -> server -> viewers).
struct NetEntityRemovePayload {
    NetEntityId id;
    uint8_t reason; // NetEntityRemoveReason
};
void net_serialize_entity_remove( PacketWriter &w, const NetEntityRemovePayload &p );
bool net_deserialize_entity_remove( PacketReader &r, NetEntityRemovePayload &p );

// ENTITY_TRANSFER (server -> client): the entity moved to a chunk with a
// different owner. adopt=1: you now simulate it; adopt=0: you no longer do.
struct NetEntityTransferPayload {
    NetEntityRecord record;
    uint8_t adopt;
};
void net_serialize_entity_transfer( PacketWriter &w, const NetEntityTransferPayload &p );
bool net_deserialize_entity_transfer( PacketReader &r, NetEntityTransferPayload &p );

// ENTITY_ACTION (client -> server -> entity chunk's owner).
struct NetEntityActionPayload {
    NetEntityId id;
    uint8_t action; // NetEntityAction
    float damage;
    double ax, ay, az; // attacker position
    uint8_t attacker_survival;
};
void net_serialize_entity_action( PacketWriter &w, const NetEntityActionPayload &p );
bool net_deserialize_entity_action( PacketReader &r, NetEntityActionPayload &p );

// PICKUP_REQUEST (client -> server): requester can hold `room` of the item.
struct NetPickupRequestPayload {
    NetEntityId id;
    int32_t room;
};
void net_serialize_pickup_request( PacketWriter &w, const NetPickupRequestPayload &p );
bool net_deserialize_pickup_request( PacketReader &r, NetPickupRequestPayload &p );

// PICKUP_RESULT (server -> requester): granted items (0 = denied/missing).
struct NetPickupResultPayload {
    NetEntityId id;
    uint32_t item;
    int32_t granted;
};
void net_serialize_pickup_result( PacketWriter &w, const NetPickupResultPayload &p );
bool net_deserialize_pickup_result( PacketReader &r, NetPickupResultPayload &p );

// PLAYER_HIT (mob owner -> server -> target client).
struct NetPlayerHitPayload {
    int32_t target_player_id;
    double fx, fy, fz; // hit source position
    float damage;
};
void net_serialize_player_hit( PacketWriter &w, const NetPlayerHitPayload &p );
bool net_deserialize_player_hit( PacketReader &r, NetPlayerHitPayload &p );

// EXPLOSION (client -> server -> all other clients).
struct NetExplosionPayload {
    double x, y, z;
    float radius;
};
void net_serialize_explosion( PacketWriter &w, const NetExplosionPayload &p );
bool net_deserialize_explosion( PacketReader &r, NetExplosionPayload &p );

// CHUNK_POPULATED (client -> server): the chunk's natural-spawn roll ran —
// mark it populated so no other owner re-rolls it.
struct NetChunkPopulatedPayload {
    int32_t cx, cy, cz;
};
void net_serialize_chunk_populated( PacketWriter &w, const NetChunkPopulatedPayload &p );
bool net_deserialize_chunk_populated( PacketReader &r, NetChunkPopulatedPayload &p );
