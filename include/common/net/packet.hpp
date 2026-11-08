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
#include "common/container.hpp"

// CHUNK_BLOCK_SIZE is defined in chunk.hpp as (CHUNK_SIZE_INTERNAL_X *
// CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_Z) = 34*34*34 = 39304. We avoid
// pulling chunk.hpp (which drags in renderer headers) into the net module, so
// restate the bound here and assert it matches.
static constexpr int32_t NET_CHUNK_BLOCK_SIZE = ( CHUNK_SIZE_X + 2 ) * ( CHUNK_SIZE_Y + 2 ) * ( CHUNK_SIZE_Z + 2 );

// Wire-format constants. Keep in sync with framed_socket.cpp.
static constexpr uint32_t NET_MAGIC = 0x52475054u; // 'R','G','P','T' little-endian
static constexpr uint8_t NET_PROTOCOL_VERSION = 9;
// Largest payload we will accept. Chunk diffs dominate; a chunk can have at
// most CHUNK_BLOCK_SIZE (~35k) diffs, each ~12 bytes -> ~420KB. Round up.
static constexpr uint32_t NET_MAX_PAYLOAD_LEN = 1u << 20; // 1 MiB
// Cap on each dimension of a CHUNK_DIFF_REQUEST box. 64^3 = 262k chunks is
// far beyond any realistic load, but bounds server memory per request.
static constexpr uint8_t NET_MAX_BOX_DIM = 64;

// Message types. Values are stable on the wire; do not renumber. Ids 7-10
// (CHEST_*) and 25-28 (FURNACE_*) were retired by the CONTAINER_* family in
// protocol v9 and are never reused — they stay un-named here so stale code
// fails to compile, but the codec table keeps placeholder rows for logging.
enum class NetMsgType : uint8_t {
    PLAYER_LOCATION = 0,
    BLOCK_UPDATE = 1,
    CLIENT_INIT = 2,
    PLAYER_CONNECTED = 3,
    PLAYER_DISCONNECTED = 4,
    CHUNK_DIFF_REQUEST = 5,
    CHUNK_DIFF_RESULT = 6,
    // 7-10: retired chest messages
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
    // 25-28: retired furnace messages
    CONTAINER_OPEN = 29,
    CONTAINER_CLOSE = 30,
    CONTAINER_CONTENTS = 31,
    CONTAINER_SET_SLOT = 32,
};
// One past the highest NetMsgType value; the codec table (below) has exactly
// this many rows, one per wire id.
static constexpr int NET_MSG_TYPE_COUNT = 33;

// NetCodec<P>: compile-time map from a payload struct to its wire codec.
// Specializations are declared next to the registry at the bottom of this
// header and delegate to the named net_serialize_*/net_deserialize_* pairs.
// senders (Multiplayer::send<P>) and the codec-table tests dispatch through
// it — a new payload type must provide a specialization or send<P> won't
// compile.
template <typename P>
struct NetCodec;

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
    NET_ACTION_FEED = 1, // feed the mob the `item` field (breeding)
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
static constexpr uint32_t NET_MOB_FLAG_BABY = 1u << 11;       // age_ticks < 0: half-scale juvenile

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

// CONTAINER_OPEN / CONTAINER_CLOSE (client -> server): subscribe/unsubscribe
// the sender to the container of `kind` at world block pos. A client holds
// at most one open container; OPEN implicitly closes the previous one.
struct NetContainerPosPayload {
    NetContainerKind kind;
    int32_t x, y, z;
};
void net_serialize_container_pos( PacketWriter &w, const NetContainerPosPayload &p );
bool net_deserialize_container_pos( PacketReader &r, NetContainerPosPayload &p );

// CONTAINER_CONTENTS (server -> client): authoritative full snapshot —
// the answer to OPEN, a throttled progress update for ticking kinds, or
// the loot snapshot for a broken container. `revision` is the server's
// per-container write counter, stamped into every CONTENTS and accepted
// SET_SLOT; clients record it for staleness checks/debugging.
// slots.size()/aux.size() are kind-defined (NetContainerKindDef) and
// validated by the deserializer.
struct NetContainerContentsPayload {
    NetContainerKind kind;
    int32_t x, y, z;
    uint32_t revision;
    std::vector<InventorySlot> slots;
    std::vector<int32_t> aux;
};
void net_serialize_container_contents( PacketWriter &w, const NetContainerContentsPayload &p );
bool net_deserialize_container_contents( PacketReader &r, NetContainerContentsPayload &p );

// CONTAINER_SET_SLOT (both directions): one slot write. client->server is
// an optimistic write (revision ignored); the server broadcasts the
// accepted write to the other viewers stamped with the revision it
// produced, and answers a rejected write with a corrective CONTENTS
// snapshot instead.
struct NetContainerSetSlotPayload {
    NetContainerKind kind;
    int32_t x, y, z;
    uint32_t slot_index;
    InventorySlot slot_value;
    uint32_t revision;
};
void net_serialize_container_set_slot( PacketWriter &w, const NetContainerSetSlotPayload &p );
bool net_deserialize_container_set_slot( PacketReader &r, NetContainerSetSlotPayload &p );

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
//   pos       MOB: feet, ITEM: box bottom-center, BODY: min corner
//   type      MOB: MobType, ITEM: ItemID, BODY: BodyKind
//   count     ITEM: stack count, MOB: variant
//   flags     MOB: MobFileRecord.flags + anim bits, BODY: unused
//   age_ticks ITEM: despawn timing; MOB: breed/age state — negative is a
//             baby's remaining growth, positive the parent's re-breed
//             cooldown (same encoding as MobFileRecord::grow_ticks)
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
    uint32_t item; // FEED: the breed item the client claims to hold
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

// ---------------------------------------------------------------------------
// Codec registry.
//
// NetCodec<P> specializations map each payload struct to its
// net_serialize_*/net_deserialize_* pair (the named functions remain the
// implementation). `fill` writes a deterministic non-trivial sample used by
// the registry-driven round-trip tests — it must exercise every serialized
// field with a value that survives its deserializer's normalization.
//
// net_codec_table() has exactly NET_MSG_TYPE_COUNT rows in wire-id order:
//   - payload-bearing rows carry make/destroy/ser/de so tests can round-trip
//     every message generically;
//   - payloadless rows (PLAYER_CONNECTED, PLAYER_DISCONNECTED) and retired
//     wire ids carry only a name, for net_msg_type_name().
// RULE: adding a NetMsgType bumps NET_MSG_TYPE_COUNT and appends the matching
// row; test_net asserts the table stays contiguous (row[i].type == i).
// ---------------------------------------------------------------------------

template <> struct NetCodec<NetPlayerPayload> {
    static void ser( PacketWriter &w, const NetPlayerPayload &p ) { net_serialize_player( w, p ); }
    static bool de( PacketReader &r, NetPlayerPayload &p ) { return net_deserialize_player( r, p ); }
    static void fill( NetPlayerPayload &p );
};
template <> struct NetCodec<NetBlockUpdatePayload> {
    static void ser( PacketWriter &w, const NetBlockUpdatePayload &p ) { net_serialize_block_update( w, p ); }
    static bool de( PacketReader &r, NetBlockUpdatePayload &p ) { return net_deserialize_block_update( r, p ); }
    static void fill( NetBlockUpdatePayload &p );
};
template <> struct NetCodec<NetChunkDiffRequestPayload> {
    static void ser( PacketWriter &w, const NetChunkDiffRequestPayload &p ) { net_serialize_chunk_diff_request( w, p ); }
    static bool de( PacketReader &r, NetChunkDiffRequestPayload &p ) { return net_deserialize_chunk_diff_request( r, p ); }
    static void fill( NetChunkDiffRequestPayload &p );
};
template <> struct NetCodec<NetChunkDiffResultPayload> {
    static void ser( PacketWriter &w, const NetChunkDiffResultPayload &p ) { net_serialize_chunk_diff_result( w, p ); }
    static bool de( PacketReader &r, NetChunkDiffResultPayload &p ) { return net_deserialize_chunk_diff_result( r, p ); }
    static void fill( NetChunkDiffResultPayload &p );
};
template <> struct NetCodec<NetContainerPosPayload> {
    static void ser( PacketWriter &w, const NetContainerPosPayload &p ) { net_serialize_container_pos( w, p ); }
    static bool de( PacketReader &r, NetContainerPosPayload &p ) { return net_deserialize_container_pos( r, p ); }
    static void fill( NetContainerPosPayload &p );
};
template <> struct NetCodec<NetContainerContentsPayload> {
    static void ser( PacketWriter &w, const NetContainerContentsPayload &p ) { net_serialize_container_contents( w, p ); }
    static bool de( PacketReader &r, NetContainerContentsPayload &p ) { return net_deserialize_container_contents( r, p ); }
    static void fill( NetContainerContentsPayload &p );
};
template <> struct NetCodec<NetContainerSetSlotPayload> {
    static void ser( PacketWriter &w, const NetContainerSetSlotPayload &p ) { net_serialize_container_set_slot( w, p ); }
    static bool de( PacketReader &r, NetContainerSetSlotPayload &p ) { return net_deserialize_container_set_slot( r, p ); }
    static void fill( NetContainerSetSlotPayload &p );
};
template <> struct NetCodec<NetWorldTimePayload> {
    static void ser( PacketWriter &w, const NetWorldTimePayload &p ) { net_serialize_world_time( w, p ); }
    static bool de( PacketReader &r, NetWorldTimePayload &p ) { return net_deserialize_world_time( r, p ); }
    static void fill( NetWorldTimePayload &p );
};
template <> struct NetCodec<NetWelcomePayload> {
    static void ser( PacketWriter &w, const NetWelcomePayload &p ) { net_serialize_welcome( w, p ); }
    static bool de( PacketReader &r, NetWelcomePayload &p ) { return net_deserialize_welcome( r, p ); }
    static void fill( NetWelcomePayload &p );
};
template <> struct NetCodec<NetViewPayload> {
    static void ser( PacketWriter &w, const NetViewPayload &p ) { net_serialize_view( w, p ); }
    static bool de( PacketReader &r, NetViewPayload &p ) { return net_deserialize_view( r, p ); }
    static void fill( NetViewPayload &p );
};
template <> struct NetCodec<NetChunkRolePayload> {
    static void ser( PacketWriter &w, const NetChunkRolePayload &p ) { net_serialize_chunk_role( w, p ); }
    static bool de( PacketReader &r, NetChunkRolePayload &p ) { return net_deserialize_chunk_role( r, p ); }
    static void fill( NetChunkRolePayload &p );
};
template <> struct NetCodec<NetEntityRecord> {
    static void ser( PacketWriter &w, const NetEntityRecord &p ) { net_write_entity_record( w, p ); }
    static bool de( PacketReader &r, NetEntityRecord &p ) { return net_read_entity_record( r, p ); }
    static void fill( NetEntityRecord &p );
};
template <> struct NetCodec<NetEntityStatePayload> {
    static void ser( PacketWriter &w, const NetEntityStatePayload &p ) { net_serialize_entity_state( w, p ); }
    static bool de( PacketReader &r, NetEntityStatePayload &p ) { return net_deserialize_entity_state( r, p ); }
    static void fill( NetEntityStatePayload &p );
};
template <> struct NetCodec<NetEntityRemovePayload> {
    static void ser( PacketWriter &w, const NetEntityRemovePayload &p ) { net_serialize_entity_remove( w, p ); }
    static bool de( PacketReader &r, NetEntityRemovePayload &p ) { return net_deserialize_entity_remove( r, p ); }
    static void fill( NetEntityRemovePayload &p );
};
template <> struct NetCodec<NetEntityTransferPayload> {
    static void ser( PacketWriter &w, const NetEntityTransferPayload &p ) { net_serialize_entity_transfer( w, p ); }
    static bool de( PacketReader &r, NetEntityTransferPayload &p ) { return net_deserialize_entity_transfer( r, p ); }
    static void fill( NetEntityTransferPayload &p );
};
template <> struct NetCodec<NetEntityActionPayload> {
    static void ser( PacketWriter &w, const NetEntityActionPayload &p ) { net_serialize_entity_action( w, p ); }
    static bool de( PacketReader &r, NetEntityActionPayload &p ) { return net_deserialize_entity_action( r, p ); }
    static void fill( NetEntityActionPayload &p );
};
template <> struct NetCodec<NetPickupRequestPayload> {
    static void ser( PacketWriter &w, const NetPickupRequestPayload &p ) { net_serialize_pickup_request( w, p ); }
    static bool de( PacketReader &r, NetPickupRequestPayload &p ) { return net_deserialize_pickup_request( r, p ); }
    static void fill( NetPickupRequestPayload &p );
};
template <> struct NetCodec<NetPickupResultPayload> {
    static void ser( PacketWriter &w, const NetPickupResultPayload &p ) { net_serialize_pickup_result( w, p ); }
    static bool de( PacketReader &r, NetPickupResultPayload &p ) { return net_deserialize_pickup_result( r, p ); }
    static void fill( NetPickupResultPayload &p );
};
template <> struct NetCodec<NetPlayerHitPayload> {
    static void ser( PacketWriter &w, const NetPlayerHitPayload &p ) { net_serialize_player_hit( w, p ); }
    static bool de( PacketReader &r, NetPlayerHitPayload &p ) { return net_deserialize_player_hit( r, p ); }
    static void fill( NetPlayerHitPayload &p );
};
template <> struct NetCodec<NetExplosionPayload> {
    static void ser( PacketWriter &w, const NetExplosionPayload &p ) { net_serialize_explosion( w, p ); }
    static bool de( PacketReader &r, NetExplosionPayload &p ) { return net_deserialize_explosion( r, p ); }
    static void fill( NetExplosionPayload &p );
};
template <> struct NetCodec<NetChunkPopulatedPayload> {
    static void ser( PacketWriter &w, const NetChunkPopulatedPayload &p ) { net_serialize_chunk_populated( w, p ); }
    static bool de( PacketReader &r, NetChunkPopulatedPayload &p ) { return net_deserialize_chunk_populated( r, p ); }
    static void fill( NetChunkPopulatedPayload &p );
};

// One codec-table row. make/destroy/ser/de are null for payloadless and
// retired ids. make() heap-allocates a filled sample (a real object —
// payloads may own vectors), destroy() frees it; both exist only so tests
// can round-trip payloads without knowing their concrete type.
struct NetCodecEntry {
    NetMsgType type;
    const char *name;
    size_t payload_size;
    void *( *make )( );
    void ( *destroy )( void *p );
    void ( *ser )( PacketWriter &w, const void *p );
    bool ( *de )( PacketReader &r, void *p );
};

const NetCodecEntry *net_codec_table( size_t *count );
const char *net_msg_type_name( NetMsgType type );

// REPGAME_NET_TRACE=1 logs every framed message at the four transport
// funnels (client in/out, server in/out) with its wire name — the cheap way
// to watch a sync conversation end to end.
bool net_trace_enabled( );
