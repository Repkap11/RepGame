// In-process multiplayer persistence simulation. Two fake clients (paired
// sockets adopted into the server's client slots, frames drained via
// FramedSocket::pop_send_frame — no real network) push BLOCK_UPDATEs through
// the real ServerLogic, "sessions" end via on_client_disconnected (flushAll,
// same as last-disconnect or SIGINT), players rejoin and edit the same chunks
// again, then a client with a deleted local world requests diffs and must get
// every edit back.
//
// Regression coverage for a lost-edits bug: record_block wrote into an empty
// world_cache entry after the chunk's edits were persisted and evicted, so
// the next persistChunk rewrote the on-disk diff with only the post-eviction
// edits — all earlier edits in that chunk were lost.

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include "server/server.hpp"
#include "server/diff_cache.hpp"
#include "common/container.hpp"
#include "common/furnace.hpp"
#include "common/net/packet.hpp"
#include "common/mob_types.hpp"
#include "common/vehicle_types.hpp"
#include "common/chunk.hpp"
#include "common/utils/file_utils.hpp"
#include "common/utils/map_storage.hpp"

#define st_print( fmt, ... ) fprintf( stdout, "selftest:%d:" fmt "\n", __LINE__, ##__VA_ARGS__ );

namespace {

const char *TEST_WORLD = "SelfTestWorld";

int failures = 0;

void check( bool ok, const char *fmt, ... ) {
    char msg[ 512 ];
    va_list args;
    va_start( args, fmt );
    vsnprintf( msg, sizeof( msg ), fmt, args );
    va_end( args );
    st_print( "  %-64s %s", msg, ok ? "PASS" : "FAIL" );
    if ( !ok ) {
        failures++;
    }
}

std::string world_dir( ) {
    char *dir = getRepGamePath( );
    std::string path = std::string( dir ) + "/" + TEST_WORLD;
    free( dir );
    return path;
}

// A fake connected client: one end of a socketpair adopted into the server's
// client slot so outbound frames land in send_queue_ for pop_send_frame, and
// registered in epoll so update_epoll() calls don't error.
struct SimClient {
    int fd = -1;
    int peer_fd = -1;
};

SimClient connect_client( Server &server ) {
    SimClient c;
    int sv[ 2 ];
    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, sv ) != 0 ) {
        st_print( "socketpair failed" );
        failures++;
        return c;
    }
    c.fd = sv[ 0 ];
    c.peer_fd = sv[ 1 ];
    fcntl( c.fd, F_SETFL, O_NONBLOCK );
    if ( c.fd >= MAX_CLIENT_FDS ) {
        st_print( "socketpair fd %d >= MAX_CLIENT_FDS %d", c.fd, MAX_CLIENT_FDS );
        failures++;
        return c;
    }
    ClientData &cd = server.test_client( c.fd );
    cd.connected = 1;
    cd.socket.adopt( c.fd );
    server.test_add_epoll( c.fd );
    server.test_logic( ).on_client_connected( server, c.fd );
    return c;
}

void disconnect_client( Server &server, SimClient &c ) {
    // Same order as the real teardown: logic first, then mark/close.
    server.test_logic( ).on_client_disconnected( server, c.fd );
    server.test_client( c.fd ).connected = 0;
    server.test_client( c.fd ).epoll_out_armed = 0;
    server.test_del_epoll( c.fd );
    close( c.fd );
    close( c.peer_fd );
    c.fd = -1;
    c.peer_fd = -1;
}

struct ReceivedFrame {
    NetMsgType type;
    int32_t player_id;
    std::vector<uint8_t> payload; // type-specific bytes
};

std::vector<ReceivedFrame> drain_client( Server &server, SimClient &c ) {
    std::vector<ReceivedFrame> out;
    while ( std::optional<std::vector<uint8_t>> frame = server.test_client( c.fd ).socket.pop_send_frame( ) ) {
        const std::vector<uint8_t> &f = *frame;
        if ( f.size( ) < 18 ) {
            continue;
        }
        ReceivedFrame r;
        r.type = static_cast<NetMsgType>( f[ 13 ] );
        r.player_id = f[ 14 ] | ( f[ 15 ] << 8 ) | ( f[ 16 ] << 16 ) | ( f[ 17 ] << 24 );
        r.payload.assign( f.begin( ) + 18, f.end( ) );
        out.push_back( std::move( r ) );
    }
    return out;
}

void send_block_update( Server &server, SimClient &c, const glm::ivec3 &pos, const BlockState &state ) {
    NetBlockUpdatePayload bu;
    bu.x = pos.x;
    bu.y = pos.y;
    bu.z = pos.z;
    bu.blockState = state;
    bu.flags = 0;
    PacketWriter w;
    net_serialize_block_update( w, bu );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::BLOCK_UPDATE, 0, w.buf( ) );
}

void send_diff_request( Server &server, SimClient &c, const glm::ivec3 &min, uint8_t sx, uint8_t sy, uint8_t sz ) {
    NetChunkDiffRequestPayload req;
    req.min_x = min.x;
    req.min_y = min.y;
    req.min_z = min.z;
    req.size_x = sx;
    req.size_y = sy;
    req.size_z = sz;
    PacketWriter w;
    net_serialize_chunk_diff_request( w, req );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::CHUNK_DIFF_REQUEST, 0, w.buf( ) );
}

// ---- container-sync helpers ------------------------------------------------

void send_container_open( Server &server, SimClient &c, NetContainerKind kind, const glm::ivec3 &pos ) {
    NetContainerPosPayload p{ kind, pos.x, pos.y, pos.z };
    PacketWriter w;
    net_serialize_container_pos( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::CONTAINER_OPEN, 0, w.buf( ) );
}

void send_container_close( Server &server, SimClient &c, NetContainerKind kind, const glm::ivec3 &pos ) {
    NetContainerPosPayload p{ kind, pos.x, pos.y, pos.z };
    PacketWriter w;
    net_serialize_container_pos( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::CONTAINER_CLOSE, 0, w.buf( ) );
}

void send_container_set_slot( Server &server, SimClient &c, NetContainerKind kind, const glm::ivec3 &pos, uint32_t slot, const InventorySlot &value ) {
    NetContainerSetSlotPayload p{ kind, pos.x, pos.y, pos.z, slot, value, 0 };
    PacketWriter w;
    net_serialize_container_set_slot( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::CONTAINER_SET_SLOT, 0, w.buf( ) );
}

// The last CONTAINER_CONTENTS the client received for `pos` (deserialized);
// false when none.
bool last_container_contents( const std::vector<ReceivedFrame> &frames, const glm::ivec3 &pos, NetContainerContentsPayload &out ) {
    bool found = false;
    for ( const ReceivedFrame &f : frames ) {
        if ( f.type != NetMsgType::CONTAINER_CONTENTS ) {
            continue;
        }
        PacketReader r( f.payload.data( ), f.payload.size( ) );
        NetContainerContentsPayload p;
        if ( net_deserialize_container_contents( r, p ) && glm::ivec3( p.x, p.y, p.z ) == pos ) {
            out = std::move( p );
            found = true;
        }
    }
    return found;
}

// ---- sign-text helpers ------------------------------------------------------

void send_sign_text( Server &server, SimClient &c, const glm::ivec3 &pos, const SignText &text ) {
    NetSignTextPayload p;
    p.x = pos.x;
    p.y = pos.y;
    p.z = pos.z;
    p.text = text;
    PacketWriter w;
    net_serialize_sign_text( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::SIGN_TEXT, 0, w.buf( ) );
}

// The last SIGN_TEXT the client received for `pos` (deserialized); false
// when none.
bool last_sign_text( const std::vector<ReceivedFrame> &frames, const glm::ivec3 &pos, NetSignTextPayload &out ) {
    bool found = false;
    for ( const ReceivedFrame &f : frames ) {
        if ( f.type != NetMsgType::SIGN_TEXT ) {
            continue;
        }
        PacketReader r( f.payload.data( ), f.payload.size( ) );
        NetSignTextPayload p;
        if ( net_deserialize_sign_text( r, p ) && glm::ivec3( p.x, p.y, p.z ) == pos ) {
            out = std::move( p );
            found = true;
        }
    }
    return found;
}

// ---- chat / player-name helpers --------------------------------------------

void send_player_name( Server &server, SimClient &c, const std::string &name ) {
    NetPlayerNamePayload p{ name };
    PacketWriter w;
    net_serialize_player_name( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::PLAYER_NAME, 0, w.buf( ) );
}

void send_chat( Server &server, SimClient &c, uint8_t kind, const std::string &from, const std::string &target, const std::string &text ) {
    NetChatPayload p{ kind, from, target, text };
    PacketWriter w;
    net_serialize_chat( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::CHAT, 0, w.buf( ) );
}

// A client's queued frames split by type: CHAT payloads deserialized, and
// PLAYER_NAME frames as (player_id, name). Drain once — a second drain
// would have already consumed the frames.
struct ChatFrames {
    std::vector<NetChatPayload> chats;
    std::vector<std::pair<int32_t, std::string>> names;
};

ChatFrames drain_chat_frames( Server &server, SimClient &c ) {
    ChatFrames out;
    for ( const ReceivedFrame &f : drain_client( server, c ) ) {
        PacketReader r( f.payload.data( ), f.payload.size( ) );
        if ( f.type == NetMsgType::CHAT ) {
            NetChatPayload p;
            if ( net_deserialize_chat( r, p ) ) {
                out.chats.push_back( std::move( p ) );
            }
        } else if ( f.type == NetMsgType::PLAYER_NAME ) {
            NetPlayerNamePayload p;
            if ( net_deserialize_player_name( r, p ) ) {
                out.names.emplace_back( f.player_id, p.name );
            }
        }
    }
    return out;
}

// Checks that `peer` received a forwarded BLOCK_UPDATE for (pos, state).
void expect_forwarded( Server &server, SimClient &peer, int from_fd, const glm::ivec3 &pos, const BlockState &want ) {
    int found = 0;
    for ( const ReceivedFrame &f : drain_client( server, peer ) ) {
        if ( f.type != NetMsgType::BLOCK_UPDATE ) {
            continue;
        }
        PacketReader r( f.payload.data( ), f.payload.size( ) );
        NetBlockUpdatePayload bu;
        if ( !net_deserialize_block_update( r, bu ) ) {
            check( false, "forwarded BLOCK_UPDATE failed to deserialize" );
            continue;
        }
        if ( bu.x == pos.x && bu.y == pos.y && bu.z == pos.z ) {
            check( BlockStates_equal( bu.blockState, want ) && f.player_id == from_fd,
                   "peer saw edit at (%d,%d,%d)", pos.x, pos.y, pos.z );
            found = 1;
        }
    }
    check( found, "peer received forwarded edit at (%d,%d,%d)", pos.x, pos.y, pos.z );
}

int index_in_chunk( const glm::ivec3 &chunk_pos, const glm::ivec3 &pos ) {
    return Chunk::get_index_from_coords( pos - chunk_pos * CHUNK_SIZE_I );
}

void place( Server &server, SimClient &who, const glm::ivec3 &pos, const BlockState &state ) {
    send_block_update( server, who, pos, state );
}

// chunk_pos -> (block index -> state), as a fresh client would rebuild it from
// CHUNK_DIFF_RESULT frames.
typedef std::map<glm::ivec3, std::map<int, BlockState>, Compare_I_Vec3> DiffView;

void collect_diffs( Server &server, SimClient &c, DiffView &out ) {
    for ( const ReceivedFrame &f : drain_client( server, c ) ) {
        if ( f.type != NetMsgType::CHUNK_DIFF_RESULT ) {
            continue;
        }
        PacketReader r( f.payload.data( ), f.payload.size( ) );
        NetChunkDiffResultPayload diff;
        if ( !net_deserialize_chunk_diff_result( r, diff ) ) {
            check( false, "CHUNK_DIFF_RESULT failed to deserialize" );
            continue;
        }
        auto &m = out[ glm::ivec3( diff.chunk_x, diff.chunk_y, diff.chunk_z ) ];
        for ( const NetChunkDiffEntry &e : diff.diffs ) {
            m[ static_cast<int>( e.blocks_index ) ] = e.blockState;
        }
    }
}

void expect_diff( const DiffView &view, const glm::ivec3 &chunk_pos, const glm::ivec3 &pos, const BlockState &want ) {
    const int index = index_in_chunk( chunk_pos, pos );
    const auto cit = view.find( chunk_pos );
    if ( cit == view.end( ) ) {
        check( false, "chunk (%d,%d,%d) has no diff at all (need edit at %d,%d,%d)",
               chunk_pos.x, chunk_pos.y, chunk_pos.z, pos.x, pos.y, pos.z );
        return;
    }
    const auto eit = cit->second.find( index );
    if ( eit == cit->second.end( ) ) {
        check( false, "chunk (%d,%d,%d) diff missing index %d for pos (%d,%d,%d)",
               chunk_pos.x, chunk_pos.y, chunk_pos.z, index, pos.x, pos.y, pos.z );
        return;
    }
    check( BlockStates_equal( eit->second, want ),
           "chunk (%d,%d,%d) index %d for pos (%d,%d,%d) id=%d want=%d",
           chunk_pos.x, chunk_pos.y, chunk_pos.z, index, pos.x, pos.y, pos.z,
           eit->second.id, want.id );
}

// ---- entity-sync helpers -------------------------------------------------

void send_view( Server &server, SimClient &c, const glm::ivec3 &center, uint8_t rx, uint8_t ry ) {
    NetViewPayload p{ center.x, center.y, center.z, rx, ry };
    PacketWriter w;
    net_serialize_view( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::VIEW, 0, w.buf( ) );
}

void send_player_location( Server &server, SimClient &c, float x, float y, float z ) {
    NetPlayerPayload p;
    memset( &p, 0, sizeof( p ) );
    p.held_item = LAST_BLOCK_ID;
    p.x = x;
    p.y = y;
    p.z = z;
    PacketWriter w;
    net_serialize_player( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::PLAYER_LOCATION, 0, w.buf( ) );
}

void send_entity_spawn( Server &server, SimClient &c, const NetEntityRecord &rec ) {
    PacketWriter w;
    net_write_entity_record( w, rec );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::ENTITY_SPAWN, 0, w.buf( ) );
}

void send_entity_state( Server &server, SimClient &c, NetEntityId id, double x, double y, double z, int32_t count ) {
    NetEntityStatePayload p;
    NetEntityStateEntry e;
    e.id = id;
    e.kind = static_cast<uint8_t>( NetEntityKind::ITEM );
    e.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_COUNT;
    e.x = x;
    e.y = y;
    e.z = z;
    e.count = count;
    p.entries.push_back( e );
    PacketWriter w;
    net_serialize_entity_state( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::ENTITY_STATE, 0, w.buf( ) );
}

void send_mob_state( Server &server, SimClient &c, NetEntityId id, double x, double y, double z, uint32_t flags ) {
    NetEntityStatePayload p;
    NetEntityStateEntry e{};
    e.id = id;
    e.kind = static_cast<uint8_t>( NetEntityKind::MOB );
    e.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_FLAGS;
    e.x = x;
    e.y = y;
    e.z = z;
    e.flags = flags;
    p.entries.push_back( e );
    PacketWriter w;
    net_serialize_entity_state( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::ENTITY_STATE, 0, w.buf( ) );
}

void send_pickup_request( Server &server, SimClient &c, NetEntityId id, int32_t room ) {
    NetPickupRequestPayload p{ id, room };
    PacketWriter w;
    net_serialize_pickup_request( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::PICKUP_REQUEST, 0, w.buf( ) );
}

// All frames of `type` in the client's outbox.
std::vector<ReceivedFrame> frames_of_type( const std::vector<ReceivedFrame> &frames, NetMsgType type ) {
    std::vector<ReceivedFrame> out;
    for ( const ReceivedFrame &f : frames ) {
        if ( f.type == type ) {
            out.push_back( f );
        }
    }
    return out;
}

// Latest CHUNK_ROLE seen for `chunk` in `frames`; -1 if none.
int last_role_for( const std::vector<ReceivedFrame> &frames, const glm::ivec3 &chunk ) {
    int role = -1;
    for ( const ReceivedFrame &f : frames ) {
        if ( f.type != NetMsgType::CHUNK_ROLE ) {
            continue;
        }
        PacketReader r( f.payload.data( ), f.payload.size( ) );
        NetChunkRolePayload p;
        if ( net_deserialize_chunk_role( r, p ) && glm::ivec3( p.cx, p.cy, p.cz ) == chunk ) {
            role = p.role;
        }
    }
    return role;
}

uint32_t welcome_session( const std::vector<ReceivedFrame> &frames ) {
    for ( const ReceivedFrame &f : frames ) {
        if ( f.type != NetMsgType::WELCOME ) {
            continue;
        }
        PacketReader r( f.payload.data( ), f.payload.size( ) );
        NetWelcomePayload p;
        if ( net_deserialize_welcome( r, p ) ) {
            return p.session_id;
        }
    }
    return 0;
}

NetEntityRecord item_record( NetEntityId id, uint32_t item, int32_t count, double x, double y, double z ) {
    NetEntityRecord rec;
    rec.id = id;
    rec.kind = NetEntityKind::ITEM;
    rec.x = x;
    rec.y = y;
    rec.z = z;
    rec.type = item;
    rec.count = count;
    return rec;
}

NetEntityRecord cart_record( NetEntityId id, double x, double y, double z, float yaw,
                             VehicleType type = VEHICLE_MINECART ) {
    NetEntityRecord rec;
    rec.id = id;
    rec.kind = NetEntityKind::VEHICLE;
    rec.x = x;
    rec.y = y;
    rec.z = z;
    rec.yaw = yaw;
    rec.type = type;
    rec.count = 0; // rider session id — empty cart
    return rec;
}

// A VEHICLE state send: pos + vel + yaw, plus a caller-controlled COUNT so the
// strip can be exercised (clients must never set it, but a hostile one
// might).
void send_cart_state( Server &server, SimClient &c, NetEntityId id, double x, double y, double z,
                      float vx, float vy, float vz, float yaw, int32_t count ) {
    NetEntityStatePayload p;
    NetEntityStateEntry e = { };
    e.id = id;
    e.kind = static_cast<uint8_t>( NetEntityKind::VEHICLE );
    e.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_YAW | NET_SE_COUNT;
    e.x = x;
    e.y = y;
    e.z = z;
    e.vx = vx;
    e.vy = vy;
    e.vz = vz;
    e.yaw = yaw;
    e.count = count;
    p.entries.push_back( e );
    PacketWriter w;
    net_serialize_entity_state( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::ENTITY_STATE, 0, w.buf( ) );
}

NetEntityRecord mob_record( NetEntityId id, uint32_t type, uint32_t flags, double x, double y, double z ) {
    NetEntityRecord rec;
    rec.id = id;
    rec.kind = NetEntityKind::MOB;
    rec.x = x;
    rec.y = y;
    rec.z = z;
    rec.type = type;
    rec.flags = flags;
    return rec;
}

// BODY_FALLING / BODY_PRIMED_TNT kind values (block_bodies.hpp pulls in
// client-only headers; the server only carries `type` opaquely).
constexpr uint32_t TEST_BODY_FALLING = 0;
constexpr uint32_t TEST_BODY_TNT = 1;

NetEntityRecord body_record( NetEntityId id, uint32_t body_kind, BlockID block, int32_t fuse, double x, double y, double z ) {
    NetEntityRecord rec;
    rec.id = id;
    rec.kind = NetEntityKind::BODY;
    rec.x = x;
    rec.y = y;
    rec.z = z;
    rec.type = body_kind;
    rec.block_state = BlockState{ block, 0, 0, block, 0 };
    rec.fuse_ticks = fuse;
    return rec;
}

void send_entity_remove( Server &server, SimClient &c, NetEntityId id, uint8_t reason ) {
    NetEntityRemovePayload p{ id, reason };
    PacketWriter w;
    net_serialize_entity_remove( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::ENTITY_REMOVE, 0, w.buf( ) );
}

void send_entity_action_kind( Server &server, SimClient &c, NetEntityId id, uint8_t action ) {
    NetEntityActionPayload p{};
    p.id = id;
    p.action = action;
    p.damage = 4.0f;
    PacketWriter w;
    net_serialize_entity_action( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::ENTITY_ACTION, 0, w.buf( ) );
}

void send_entity_action( Server &server, SimClient &c, NetEntityId id ) {
    send_entity_action_kind( server, c, id, NET_ACTION_HIT );
}

void send_chunk_populated( Server &server, SimClient &c, const glm::ivec3 &chunk ) {
    NetChunkPopulatedPayload p{ chunk.x, chunk.y, chunk.z };
    PacketWriter w;
    net_serialize_chunk_populated( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::CHUNK_POPULATED, 0, w.buf( ) );
}

// Cross-check the raw chunk files on disk (what survives a server restart).
void expect_disk_block( MapStorage &storage, const glm::ivec3 &chunk_pos, const glm::ivec3 &pos, const BlockState &want ) {
    const int index = index_in_chunk( chunk_pos, pos );
    ChunkEdits edits;
    if ( storage.load_chunk_edits( chunk_pos, edits ) != 1 ) {
        check( false, "no chunk file for (%d,%d,%d) holding pos (%d,%d,%d)",
               chunk_pos.x, chunk_pos.y, chunk_pos.z, pos.x, pos.y, pos.z );
        return;
    }
    for ( const ChunkEditRun &run : edits.runs ) {
        if ( index >= (int)run.start && index < (int)( run.start + run.count ) ) {
            check( BlockStates_equal( run.state, want ),
                   "file chunk (%d,%d,%d) pos (%d,%d,%d) id=%d want=%d",
                   chunk_pos.x, chunk_pos.y, chunk_pos.z, pos.x, pos.y, pos.z,
                   run.state.id, want.id );
            return;
        }
    }
    check( false, "file chunk (%d,%d,%d) has no edit for pos (%d,%d,%d) want=%d",
           chunk_pos.x, chunk_pos.y, chunk_pos.z, pos.x, pos.y, pos.z, want.id );
}

} // namespace

int server_selftest( ) {
    std::filesystem::remove_all( world_dir( ) );

    const BlockState stone = { STONE, 0, 0, STONE, 0 };
    const BlockState dirt = { DIRT, 0, 0, DIRT, 0 };
    const BlockState gold = { GOLD_BLOCK, 0, 0, GOLD_BLOCK, 0 };
    const BlockState sand = { SAND, 0, 0, SAND, 0 };

    Server server;
    server.init( 0, TEST_WORLD );

    // --- Session 1: two players edit the world ---
    SimClient a = connect_client( server );
    SimClient b = connect_client( server );
    drain_client( server, a );
    drain_client( server, b );

    // Interior edit in chunk (0,0,0).
    place( server, a, glm::ivec3( 10, 40, 10 ), stone );
    expect_forwarded( server, b, a.fd, glm::ivec3( 10, 40, 10 ), stone );
    check( server.test_logic( ).cachedChunkCount( ) > 0,
           "edit populates the chunk cache (got %zu)", server.test_logic( ).cachedChunkCount( ) );
    // Edge of chunk (0,0,0) -> mirrored into chunk (1,0,0)'s border.
    place( server, b, glm::ivec3( 61, 40, 10 ), dirt );
    // Interior of chunk (1,0,0), touching chunk 0 -> mirrored into (0,0,0).
    place( server, a, glm::ivec3( 62, 40, 10 ), gold );
    expect_forwarded( server, b, a.fd, glm::ivec3( 62, 40, 10 ), gold );

    // Both leave: last disconnect flushes every cached chunk to disk and
    // evicts the whole cache.
    disconnect_client( server, a );
    disconnect_client( server, b );

    // --- Session 2: players return and edit the same chunks again. The
    // earlier edits now live only in chunk files; these writes land in
    // chunks that were evicted from the cache. ---
    a = connect_client( server );
    b = connect_client( server );
    drain_client( server, a );
    drain_client( server, b );

    // Break the session-1 stone: records AIR into the evicted chunk (0,0,0).
    place( server, b, glm::ivec3( 10, 40, 10 ), BLOCK_STATE_AIR );
    // Chunk (1,0,0) was evicted too; this owns-in-1 edit mirrors into chunk 2.
    place( server, a, glm::ivec3( 123, 40, 10 ), sand );

    disconnect_client( server, a );
    disconnect_client( server, b );

    // --- Session 3: a client that deleted its local world rejoins and
    // requests diffs; every edit ever sent must come back. ---
    a = connect_client( server );
    drain_client( server, a );
    const size_t probes_before = server.test_logic( ).test_storage( ).index_short_circuits( );
    send_diff_request( server, a, glm::ivec3( -1, -1, -1 ), 5, 3, 3 );

    DiffView view;
    collect_diffs( server, a, view );
    // Diff reads must not populate the cache: every requested chunk was
    // already flushed, so the cache stays empty after serving them.
    check( server.test_logic( ).cachedChunkCount( ) == 0,
           "diff requests leave the chunk cache empty (got %zu)", server.test_logic( ).cachedChunkCount( ) );
    // Most of the 5x3x3 box has no save file; the file index answered
    // every chunk's existence check without touching the filesystem.
    check( server.test_logic( ).test_storage( ).index_short_circuits( ) >= probes_before + 45,
           "file index answered all %d box existence checks (got %zu, was %zu)",
           5 * 3 * 3,
           server.test_logic( ).test_storage( ).index_short_circuits( ), probes_before );

    const glm::ivec3 c0( 0, 0, 0 );
    const glm::ivec3 c1( 1, 0, 0 );
    const glm::ivec3 c2( 2, 0, 0 );

    // Owning-chunk entries.
    expect_diff( view, c0, glm::ivec3( 10, 40, 10 ), BLOCK_STATE_AIR );
    expect_diff( view, c0, glm::ivec3( 61, 40, 10 ), dirt );
    expect_diff( view, c1, glm::ivec3( 62, 40, 10 ), gold );
    expect_diff( view, c1, glm::ivec3( 123, 40, 10 ), sand );

    // Border mirrors into neighbors.
    expect_diff( view, c1, glm::ivec3( 61, 40, 10 ), dirt );  // x=61 sits in chunk1's -x border
    expect_diff( view, c0, glm::ivec3( 62, 40, 10 ), gold );  // x=62 sits in chunk0's +x border
    expect_diff( view, c2, glm::ivec3( 123, 40, 10 ), sand ); // x=123 sits in chunk2's -x border

    // And the same positions must survive in the chunk files themselves.
    MapStorage storage;
    storage.init( TEST_WORLD );
    expect_disk_block( storage, c0, glm::ivec3( 10, 40, 10 ), BLOCK_STATE_AIR );
    expect_disk_block( storage, c0, glm::ivec3( 61, 40, 10 ), dirt );
    expect_disk_block( storage, c0, glm::ivec3( 62, 40, 10 ), gold );
    expect_disk_block( storage, c1, glm::ivec3( 61, 40, 10 ), dirt );
    expect_disk_block( storage, c1, glm::ivec3( 62, 40, 10 ), gold );
    expect_disk_block( storage, c1, glm::ivec3( 123, 40, 10 ), sand );
    expect_disk_block( storage, c2, glm::ivec3( 123, 40, 10 ), sand );

    disconnect_client( server, a );

    // --- Legacy-format chunk: pre-2047 saves marked untouched slots with
    // LEGACY_EMPTY_BLOCK_ID. Server diff loads must read those as the empty
    // marker (LAST_BLOCK_ID), not AIR — AIR becomes a real diff entry per
    // slot and blows up the diff map. ---
    const glm::ivec3 leg( 5, 0, 5 );
    {
        const glm::ivec3 leg_pos = leg * CHUNK_SIZE_I + glm::ivec3( 10, 40, 10 );
        const int leg_index = index_in_chunk( leg, leg_pos );
        const int other_index = leg_index + 1;

        // Write a legacy-format chunk file directly (persist_chunk_legacy
        // encodes raw states, no sanitizing) through the server's storage
        // so the file index picks it up.
        BlockState *legacy = static_cast<BlockState *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) ) );
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
            legacy[ i ] = BlockState{ static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0, 0,
                                      static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0 };
        }
        legacy[ leg_index ] = stone;
        server.test_logic( ).test_storage( ).persist_chunk_legacy( leg, legacy );
        free( legacy );

        SimClient c = connect_client( server );
        drain_client( server, c );
        send_diff_request( server, c, leg, 1, 1, 1 );
        DiffView leg_view;
        collect_diffs( server, c, leg_view );
        expect_diff( leg_view, leg, leg_pos, stone );
        const auto lit = leg_view.find( leg );
        check( lit != leg_view.end( ) && lit->second.size( ) == 1,
               "legacy chunk (%d,%d,%d) diff has exactly 1 entry (got %zu)",
               leg.x, leg.y, leg.z, lit == leg_view.end( ) ? 0 : lit->second.size( ) );
        check( server.test_logic( ).cachedChunkCount( ) == 0,
               "legacy diff request leaves the chunk cache empty (got %zu)",
               server.test_logic( ).cachedChunkCount( ) );
        disconnect_client( server, c );

        // No client edit touched the chunk, so the file is still the legacy
        // one: it decodes to just the stone edit — the sentinel slots are
        // holes, never real diffs — and is flagged for lazy rewrite.
        ChunkEdits reloaded;
        if ( storage.load_chunk_edits( leg, reloaded ) == 1 ) {
            check( reloaded.runs.size() == 1 && reloaded.runs[ 0 ].start == (uint32_t)leg_index &&
                       reloaded.runs[ 0 ].count == 1 && reloaded.runs[ 0 ].state.id == STONE,
                   "legacy chunk keeps stone at index %d (runs=%zu)", leg_index, reloaded.runs.size() );
            check( !reloaded.legacy_snapshot && reloaded.needs_rewrite,
                   "legacy diff file flags a lazy rewrite" );
        } else {
            check( false, "no chunk file for legacy chunk (%d,%d,%d)", leg.x, leg.y, leg.z );
        }
        (void)other_index;

        // The startup migration path rewrites the legacy file to v1 with
        // identical diffs; a second pass finds nothing left to do.
        MapStorage &srv_storage = server.test_logic( ).test_storage( );
        check( srv_storage.migrate_legacy_chunk_files( ) == 1,
               "migration rewrote one legacy chunk file" );
        ChunkEdits v1;
        check( srv_storage.load_chunk_edits( leg, v1 ) == 1 && !v1.needs_rewrite && !v1.legacy_snapshot &&
                   v1.runs.size( ) == 1 && v1.runs[ 0 ].start == (uint32_t)leg_index &&
                   v1.runs[ 0 ].count == 1 && v1.runs[ 0 ].state.id == STONE,
               "migrated file decodes v1 with identical diffs" );
        check( srv_storage.migrate_legacy_chunk_files( ) == 0,
               "second migration pass is a no-op" );
    }

    // --- Corrupt legacy file: a mid-file run length that overshoots the
    // volume is refused (never returned as a partial decode), so migration
    // and later edits leave the file on disk instead of clobbering it with
    // only the decoded prefix. ---
    const glm::ivec3 bad( 9, 0, 9 );
    {
        MapStorage &srv_storage = server.test_logic( ).test_storage( );
        char bad_name[ CHUNK_NAME_MAX_LENGTH ];
        snprintf( bad_name, sizeof( bad_name ), FILE_ROOT_CHUNK, world_dir( ).c_str( ), bad.x, bad.y, bad.z );

        // Seed a valid legacy file so the save-file index knows the chunk.
        BlockState *seed = static_cast<BlockState *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) ) );
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
            seed[ i ] = BlockState{ static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0, 0,
                                    static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0 };
        }
        srv_storage.persist_chunk_legacy( bad, seed );
        free( seed );

        // Overwrite it in place with a corrupt stream: two good records then
        // a hole run that overshoots the volume.
        const uint32_t rec_size = sizeof( BlockState ) + sizeof( uint32_t );
        FILE *f = fopen( bad_name, "wb" );
        check( f != NULL, "wrote corrupt legacy chunk file" );
        if ( f != NULL ) {
            const BlockState hole{ static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0, 0,
                                   static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0 };
            fwrite( &rec_size, sizeof( rec_size ), 1, f );
            const uint32_t n5 = 5, n3 = 3, nbig = CHUNK_BLOCK_SIZE;
            fwrite( &stone, sizeof( stone ), 1, f );
            fwrite( &n5, sizeof( n5 ), 1, f );
            fwrite( &stone, sizeof( stone ), 1, f );
            fwrite( &n3, sizeof( n3 ), 1, f );
            fwrite( &hole, sizeof( hole ), 1, f );
            fwrite( &nbig, sizeof( nbig ), 1, f );
            fclose( f );
        }
        const long bad_size = std::filesystem::file_size( bad_name );

        ChunkEdits corrupt;
        check( srv_storage.load_chunk_edits( bad, corrupt ) == -1,
               "corrupt legacy run length refuses decode" );
        check( srv_storage.migrate_legacy_chunk_files( ) == 0 &&
                   std::filesystem::file_size( bad_name ) == (size_t)bad_size,
               "migration leaves an undecodable file untouched" );

        // An edit into the corrupt chunk must not clobber the file: the merge
        // refuses, so the next persist writes nothing.
        SimClient c = connect_client( server );
        drain_client( server, c );
        send_block_update( server, c, bad * CHUNK_SIZE_I + glm::ivec3( 1, 40, 1 ), stone );
        drain_client( server, c );
        server.test_logic( ).flushAll( );
        check( std::filesystem::file_size( bad_name ) == (size_t)bad_size,
               "edit + persist preserves the undecodable chunk file" );
        disconnect_client( server, c );
        remove( bad_name );
    }

    // --- Save-file index: existence checks hit the index instead of the
    // filesystem; persists keep it current. ---
    const glm::ivec3 ke( 7, 0, 7 );
    {
        const glm::ivec3 ke_pos = ke * CHUNK_SIZE_I + glm::ivec3( 10, 40, 10 );
        MapStorage &srv_storage = server.test_logic( ).test_storage( );
        SimClient c = connect_client( server );
        drain_client( server, c );

        size_t probes = srv_storage.index_short_circuits( );
        send_diff_request( server, c, ke, 1, 1, 1 );
        DiffView ke_view;
        collect_diffs( server, c, ke_view );
        check( ke_view.find( ke ) == ke_view.end( ),
               "index: unseen chunk (%d,%d,%d) emits no diff", ke.x, ke.y, ke.z );
        check( srv_storage.index_short_circuits( ) > probes,
               "index: absent chunk answered without fs probe (got %zu, was %zu)",
               srv_storage.index_short_circuits( ), probes );

        // A second request answers the same way from the index.
        probes = srv_storage.index_short_circuits( );
        send_diff_request( server, c, ke, 1, 1, 1 );
        collect_diffs( server, c, ke_view );
        check( ke_view.find( ke ) == ke_view.end( ),
               "index: repeat request for absent chunk emits no diff" );
        check( srv_storage.index_short_circuits( ) > probes,
               "index: repeat request still skips the filesystem" );

        // An edit persists through the index; a fresh client gets it back.
        place( server, c, ke_pos, stone );
        disconnect_client( server, c );
        c = connect_client( server );
        drain_client( server, c );
        send_diff_request( server, c, ke, 1, 1, 1 );
        ke_view.clear( );
        collect_diffs( server, c, ke_view );
        expect_diff( ke_view, ke, ke_pos, stone );
        disconnect_client( server, c );
    }

    // --- File index build + upkeep: one readdir picks up every file kind,
    // persists index new files, empty persists unindex them. Own world dir
    // so the main test world's contents stay untouched. ---
    {
        const char *IDX_WORLD = "SelfTestIndexWorld";
        {
            char *dir = getRepGamePath( );
            std::filesystem::remove_all( std::string( dir ) + "/" + IDX_WORLD );
            free( dir );
        }
        MapStorage idx;
        idx.init( IDX_WORLD );
        const glm::ivec3 p( 1, 2, 3 );
        const glm::ivec3 absent( -4, -5, -6 ); // negative coords pack too
        ChestContentsMap chests;
        chests[ glm::ivec3( 1, 2, 3 ) ] = ChestContents{ };
        idx.persist_chests( p, chests );
        FurnaceContentsMap furnaces;
        FurnaceContents fc = { };
        fc.slots[ FURNACE_SLOT_FUEL ].item_id = COAL;
        fc.slots[ FURNACE_SLOT_FUEL ].quantity = 1;
        furnaces[ glm::ivec3( 1, 2, 3 ) ] = fc;
        idx.persist_furnaces( p, furnaces );
        MobFileRecord mob = { };
        mob.type = MOB_PIG;
        idx.persist_mobs( p, { mob } );
        ItemFileRecord item = { };
        item.item = STONE;
        item.count = 1;
        idx.persist_items( p, { item } );
        // Written before enable: the readdir must find all of them.
        idx.persist_chunk_edits( p, { { 0u, 1u, stone } } );

        idx.enable_file_index( );
        size_t counts[ MAP_FILE_KIND_COUNT ];
        idx.file_index_counts( counts );
        check( counts[ MAP_FILE_CHUNK ] == 1 && counts[ MAP_FILE_MOBS ] == 1 &&
                   counts[ MAP_FILE_ITEMS ] == 1 && counts[ MAP_FILE_CHEST ] == 1 &&
                   counts[ MAP_FILE_FURNACE ] == 1,
               "index: built from dir with every file kind (%zu/%zu/%zu/%zu/%zu)",
               counts[ MAP_FILE_CHUNK ], counts[ MAP_FILE_MOBS ], counts[ MAP_FILE_ITEMS ],
               counts[ MAP_FILE_CHEST ], counts[ MAP_FILE_FURNACE ] );

        ChunkEdits e;
        check( idx.load_chunk_edits( p, e ) == 1 && e.runs.size( ) == 1,
               "index: indexed chunk still loads" );
        size_t probes = idx.index_short_circuits( );
        check( !idx.check_if_chunk_exists( absent ) && idx.load_chunk_edits( absent, e ) == 0,
               "index: absent chunk short-circuits" );
        check( idx.index_short_circuits( ) >= probes + 2,
               "index: absent chunk needed zero fs probes (got %zu, was %zu)",
               idx.index_short_circuits( ), probes );

        // Persist indexes a new file; an empty persist unindexes it.
        idx.persist_chunk_edits( absent, { { 0u, 1u, stone } } );
        check( idx.check_if_chunk_exists( absent ), "index: persist indexes new file" );
        idx.persist_chunk_edits( absent, { } );
        check( !idx.check_if_chunk_exists( absent ), "index: empty persist unindexes" );

        char *dir = getRepGamePath( );
        std::filesystem::remove_all( std::string( dir ) + "/" + IDX_WORLD );
        free( dir );
    }

    // === Entity sync (server arbitrates, owner client simulates) ==========
    // Chunk geometry: CHUNK_SIZE 62. A's player sits in chunk -1 with view
    // center -1 (covers x -3..1); B's player in chunk 2 with view center 2
    // (covers x 0..4). Overlap: chunks 0 and 1.
    {
        SimClient ea = connect_client( server );
        SimClient eb = connect_client( server );
        const std::vector<ReceivedFrame> wa = drain_client( server, ea );
        const std::vector<ReceivedFrame> wb = drain_client( server, eb );
        const uint32_t sess_a = welcome_session( wa );
        const uint32_t sess_b = welcome_session( wb );
        check( sess_a != 0 && sess_b != 0 && sess_a != sess_b,
               "WELCOME session ids distinct (a=%u b=%u)", sess_a, sess_b );

        send_player_location( server, ea, -70.0f, 30.0f, 10.0f ); // chunk -2
        send_player_location( server, eb, 130.0f, 30.0f, 10.0f ); // chunk 2
        send_view( server, ea, glm::ivec3( -1, 0, 0 ), 2, 1 );
        send_view( server, eb, glm::ivec3( 2, 0, 0 ), 2, 1 );

        const glm::ivec3 c_n3( -3, 0, 0 );
        const ServerEntities &se = server.test_logic( ).test_entities( );
        // Chunk 1: A (dist 3, player chunk -2) vs B (dist 1, player chunk 2).
        // A covered it first; hysteresis keeps A (3 <= 1+2).
        check( se.owner_of( c1 ) == ea.fd, "entity: nearer challenger does not steal (owner=%d a=%d)", se.owner_of( c1 ), ea.fd );
        check( se.owner_of( c2 ) == eb.fd, "entity: solo-covered chunk owned by B (owner=%d b=%d)", se.owner_of( c2 ), eb.fd );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( last_role_for( fa, c1 ) == NET_ROLE_OWNER, "entity: A role OWNER for chunk 1" );
            check( last_role_for( fb, c1 ) == NET_ROLE_VIEWER, "entity: B role VIEWER for chunk 1" );
        }

        // ENTITY_SPAWN relays to all viewers, echoing back to the sender as
        // the spawn's ack (stale CHUNK_ROLE snapshots can't wipe it).
        const NetEntityId item1 = ( static_cast<NetEntityId>( sess_a ) << 32 ) | 1;
        send_entity_spawn( server, ea, item_record( item1, DIRT, 5, 70.0, 30.0, 10.0 ) ); // chunk 1
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_SPAWN ).size( ) == 1, "entity: spawn echoed to sender" );
            check( frames_of_type( fb, NetMsgType::ENTITY_SPAWN ).size( ) == 1, "entity: spawn relayed to viewer" );
        }

        // ENTITY_STATE from a non-owner is ignored; from the owner relays.
        send_entity_state( server, eb, item1, 71.0, 30.0, 10.0, 5 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            check( frames_of_type( fa, NetMsgType::ENTITY_STATE ).empty( ), "entity: non-owner state ignored" );
        }
        send_entity_state( server, ea, item1, 71.0, 30.0, 10.0, 5 );
        {
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fb, NetMsgType::ENTITY_STATE ).size( ) == 1, "entity: owner state relayed to viewer" );
        }

        // Pickup race on item1 (in chunk 1): exactly one request grants.
        send_pickup_request( server, ea, item1, 10 );
        send_pickup_request( server, eb, item1, 10 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            int granted_a = -1, granted_b = -1;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::PICKUP_RESULT ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetPickupResultPayload p;
                if ( net_deserialize_pickup_result( r, p ) ) granted_a = p.granted;
            }
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::PICKUP_RESULT ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetPickupResultPayload p;
                if ( net_deserialize_pickup_result( r, p ) ) granted_b = p.granted;
            }
            check( granted_a == 5 && granted_b == 0, "entity: pickup race grants exactly once (a=%d b=%d)", granted_a, granted_b );
            check( frames_of_type( fa, NetMsgType::ENTITY_REMOVE ).size( ) == 1 && frames_of_type( fb, NetMsgType::ENTITY_REMOVE ).size( ) == 1,
                   "entity: PICKED_UP remove broadcast to all viewers" );
        }

        // Partial pickup reduces the count via a server-originated state.
        const NetEntityId item2 = ( static_cast<NetEntityId>( sess_a ) << 32 ) | 2;
        send_entity_spawn( server, ea, item_record( item2, STONE, 10, 70.0, 30.0, 10.0 ) );
        drain_client( server, ea );
        drain_client( server, eb );
        send_pickup_request( server, eb, item2, 4 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            int saw_count = -1;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_STATE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityStatePayload p;
                if ( net_deserialize_entity_state( r, p ) && !p.entries.empty( ) ) saw_count = p.entries[ 0 ].count;
            }
            check( saw_count == 6, "entity: partial pickup broadcasts reduced count (got %d)", saw_count );
            int granted = -1;
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::PICKUP_RESULT ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetPickupResultPayload p;
                if ( net_deserialize_pickup_result( r, p ) ) granted = p.granted;
            }
            check( granted == 4, "entity: partial pickup grants room-limited count (got %d)", granted );
        }

        // Boundary hysteresis: a state resting inside the 0.5-block margin
        // of chunk 2 keeps the record in chunk 1 — no move, no transfer.
        send_entity_state( server, ea, item2, 124.3, 30.0, 10.0, 6 ); // 0.3 into c2
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ).empty( ) &&
                       frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ).empty( ),
                   "entity: margin rest does not transfer" );
            check( se.entity_count_of( c1 ) == 1 && se.entity_count_of( c2 ) == 0,
                   "entity: margin rest keeps record in old chunk (c1=%zu c2=%zu)",
                   se.entity_count_of( c1 ), se.entity_count_of( c2 ) );
        }
        // Chunk crossing: A moves item2 from chunk 1 into chunk 2 (B-owned)
        // — 1.0 inside the new chunk, past the margin.
        send_entity_state( server, ea, item2, 125.0, 30.0, 10.0, 6 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            int adopt_a = -1, adopt_b = -1;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) ) adopt_a = p.adopt;
            }
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) ) adopt_b = p.adopt;
            }
            check( adopt_b == 1 && adopt_a == 0, "entity: crossing transfers ownership (a=%d b=%d)", adopt_a, adopt_b );
        }

        // The crossed entity still resolves through the index: B (new owner)
        // picks it up whole, then a second request finds nothing.
        send_pickup_request( server, eb, item2, 10 );
        send_pickup_request( server, eb, item2, 10 );
        {
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            int results = 0, removes = 0;
            int granted_first = -1;
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::PICKUP_RESULT ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetPickupResultPayload p;
                if ( net_deserialize_pickup_result( r, p ) ) {
                    if ( results == 0 ) granted_first = p.granted;
                    results++;
                }
            }
            removes = static_cast<int>( frames_of_type( fb, NetMsgType::ENTITY_REMOVE ).size( ) );
            check( granted_first == 6 && results == 2, "entity: post-crossing pickup resolves (granted=%d results=%d)", granted_first, results );
            check( removes == 1, "entity: post-crossing pickup broadcasts remove (got %d)", removes );
        }
        drain_client( server, ea );

        // Hysteresis: moving A's player 1 chunk farther than B for chunk 1
        // keeps ownership; 3 chunks farther flips it, both get CHUNK_ROLE.
        send_player_location( server, ea, -60.0f, 30.0f, 10.0f ); // chunk -1: dist 2 vs B 1
        drain_client( server, ea );
        drain_client( server, eb );
        check( se.owner_of( c1 ) == ea.fd, "entity: owner 1 chunk farther keeps ownership" );
        send_player_location( server, ea, -200.0f, 30.0f, 10.0f ); // chunk -4: dist 5 vs B 1
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( last_role_for( fb, c1 ) == NET_ROLE_OWNER && last_role_for( fa, c1 ) == NET_ROLE_VIEWER,
                   "entity: owner 3+ chunks farther flips (both notified)" );
            check( se.owner_of( c1 ) == eb.fd, "entity: B owns chunk 1 after flip" );
        }

        // View shrink -> uncover -> items file persists; re-cover reloads.
        const NetEntityId item3 = ( static_cast<NetEntityId>( sess_a ) << 32 ) | 3;
        send_entity_spawn( server, ea, item_record( item3, GOLD_BLOCK, 8, -180.0, 30.0, 10.0 ) ); // chunk -3, only A covers
        drain_client( server, ea );
        drain_client( server, eb );
        send_view( server, ea, glm::ivec3( -1, 0, 0 ), 0, 1 ); // covers only (-1, y, 0)
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            check( last_role_for( fa, c_n3 ) == NET_ROLE_NONE, "entity: uncovered chunk sends CHUNK_ROLE NONE" );
        }
        drain_client( server, eb );
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<ItemFileRecord> persisted;
            const int existed = check_storage.load_items( c_n3, persisted );
            check( existed == 1 && persisted.size( ) == 1 && persisted[ 0 ].item == GOLD_BLOCK && persisted[ 0 ].count == 8,
                   "entity: uncovered chunk persists items file (existed=%d n=%zu)", existed, persisted.size( ) );
        }
        send_view( server, ea, glm::ivec3( -1, 0, 0 ), 2, 1 ); // re-cover
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            bool reloaded = false;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::CHUNK_ROLE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetChunkRolePayload p;
                if ( net_deserialize_chunk_role( r, p ) && glm::ivec3( p.cx, p.cy, p.cz ) == c_n3 && p.role == NET_ROLE_OWNER &&
                     p.records.size( ) == 1 && p.records[ 0 ].type == GOLD_BLOCK && p.records[ 0 ].count == 8 ) {
                    reloaded = true;
                }
            }
            check( reloaded, "entity: re-covered chunk reloads persisted item records" );
        }

        // --- VEHICLE entities: owner-simulated like items -------------------
        // B owns chunk 1 here (the flip test above) — B drives the cart.
        // COUNT strip: a VEHICLE's count is the server-authoritative rider
        // session id — a client-sent COUNT must be dropped before the
        // entry is applied or relayed.
        const NetEntityId cart1 = ( static_cast<NetEntityId>( sess_b ) << 32 ) | 20;
        send_entity_spawn( server, eb, cart_record( cart1, 70.0, 30.0, 10.0, 45.0f ) ); // chunk 1 (B owns)
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_SPAWN ).size( ) == 1, "cart: spawn relayed to viewer" );
            check( se.entity_count_of( c1 ) > 0, "cart: record stored (n=%zu)", se.entity_count_of( c1 ) );
        }
        send_cart_state( server, eb, cart1, 71.0, 30.0, 10.0, 0.5f, 0.0f, 0.0f, 45.0f, 99 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            bool stripped = false;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_STATE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityStatePayload p;
                if ( net_deserialize_entity_state( r, p ) ) {
                    for ( const NetEntityStateEntry &ent : p.entries ) {
                        if ( ent.id == cart1 ) {
                            stripped = ( ent.present & NET_SE_COUNT ) == 0;
                        }
                    }
                }
            }
            check( stripped, "cart: client-sent COUNT stripped before relay" );
        }
        // Chunk crossing into chunk -2 (only A covers, so A owns): the cart
        // transfers like an item — B gets adopt=0, A gets adopt=1.
        send_cart_state( server, eb, cart1, -100.0, 30.0, 10.0, 0.5f, 0.0f, 0.0f, 45.0f, 0 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            int adopt_a = -1, adopt_b = -1;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) && p.record.id == cart1 ) {
                    adopt_a = p.adopt;
                }
            }
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) && p.record.id == cart1 ) {
                    adopt_b = p.adopt;
                }
            }
            check( adopt_a == 1 && adopt_b == 0, "cart: crossing transfers ownership (a=%d b=%d)", adopt_a, adopt_b );
        }
        // Remove the transferred cart — the mob block below reuses chunk -2
        // and must find it empty.
        send_entity_remove( server, ea, cart1, NET_REMOVE_DIED );
        drain_client( server, ea );
        drain_client( server, eb );

        // Persistence: uncover writes the carts_ sidecar with pos/vel/yaw;
        // re-cover reloads it as a minted VEHICLE record with count reset.
        // (yaw 45.0 survives the 360/256 wire quantization exactly.)
        const NetEntityId cart2 = ( static_cast<NetEntityId>( sess_a ) << 32 ) | 21;
        send_entity_spawn( server, ea, cart_record( cart2, -180.0, 30.0, 10.0, 45.0f ) ); // chunk -3, only A covers
        send_cart_state( server, ea, cart2, -180.5, 30.5, 10.5, 0.25f, -0.5f, 0.75f, 45.0f, 0 );
        // A boat in the same chunk — its type must persist and reload too.
        const NetEntityId boat1 = ( static_cast<NetEntityId>( sess_a ) << 32 ) | 23;
        send_entity_spawn( server, ea, cart_record( boat1, -182.0, 30.0, 12.0, 90.0f, VEHICLE_CHERRY_BOAT ) );
        send_cart_state( server, ea, boat1, -182.5, 30.5, 12.5, 0.0f, 0.0f, 0.0f, 90.0f, 0 );
        drain_client( server, ea );
        drain_client( server, eb );
        send_view( server, ea, glm::ivec3( -1, 0, 0 ), 0, 1 ); // uncover c_n3
        drain_client( server, ea );
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<VehicleFileRecord> persisted;
            const int existed = check_storage.load_vehicles( c_n3, persisted );
            check( existed == 1 && persisted.size( ) == 2 && persisted[ 0 ].x == -180.5 && persisted[ 0 ].y == 30.5 &&
                       persisted[ 0 ].vx == 0.25f && persisted[ 0 ].yaw == 45.0f && persisted[ 0 ].type == VEHICLE_MINECART &&
                       persisted[ 1 ].x == -182.5 && persisted[ 1 ].type == VEHICLE_CHERRY_BOAT,
                   "cart: uncovered chunk persists vehicles file (existed=%d n=%zu)", existed, persisted.size( ) );
        }
        send_view( server, ea, glm::ivec3( -1, 0, 0 ), 2, 1 ); // re-cover
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            bool reloaded = false, boat_reloaded = false;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::CHUNK_ROLE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetChunkRolePayload p;
                if ( !( net_deserialize_chunk_role( r, p ) && glm::ivec3( p.cx, p.cy, p.cz ) == c_n3 &&
                        p.role == NET_ROLE_OWNER ) ) {
                    continue;
                }
                for ( const NetEntityRecord &rec : p.records ) {
                    if ( rec.kind == NetEntityKind::VEHICLE && rec.x == -180.5 && rec.yaw == 45.0f && rec.count == 0 &&
                         rec.type == VEHICLE_MINECART ) {
                        reloaded = true;
                    }
                    if ( rec.kind == NetEntityKind::VEHICLE && rec.x == -182.5 && rec.type == VEHICLE_CHERRY_BOAT ) {
                        boat_reloaded = true;
                    }
                }
            }
            check( reloaded, "cart: re-covered chunk reloads persisted records" );
            check( boat_reloaded, "boat: re-covered chunk reloads the boat with its type" );
        }

        // --- Riding: the rider simulates its cart -------------------------
        // A mounts a cart in B-owned chunk 1: the server stamps count with
        // A's session, broadcasts the COUNT state to every viewer (sender
        // included), and moves the sim B -> A with adopt transfers.
        const NetEntityId cart4 = ( static_cast<NetEntityId>( sess_a ) << 32 ) | 22;
        send_entity_spawn( server, ea, cart_record( cart4, 70.0, 30.0, 10.0, 45.0f ) );
        drain_client( server, ea );
        drain_client( server, eb );
        send_entity_action_kind( server, ea, cart4, NET_ACTION_MOUNT );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            int adopt_a = -1, adopt_b = -1;
            bool count_a = false, count_b = false;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) && p.record.id == cart4 ) {
                    adopt_a = p.adopt;
                    check( p.record.count == (int32_t)sess_a, "cart ride: adopt record carries rider session" );
                }
            }
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) && p.record.id == cart4 ) {
                    adopt_b = p.adopt;
                }
            }
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_STATE ) ) {
                if ( f.player_id != 0 ) {
                    continue;
                }
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityStatePayload p;
                if ( net_deserialize_entity_state( r, p ) ) {
                    for ( const NetEntityStateEntry &ent : p.entries ) {
                        if ( ent.id == cart4 && ( ent.present & NET_SE_COUNT ) && ent.count == (int32_t)sess_a ) {
                            count_a = true;
                        }
                    }
                }
            }
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::ENTITY_STATE ) ) {
                if ( f.player_id != 0 ) {
                    continue;
                }
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityStatePayload p;
                if ( net_deserialize_entity_state( r, p ) ) {
                    for ( const NetEntityStateEntry &ent : p.entries ) {
                        if ( ent.id == cart4 && ( ent.present & NET_SE_COUNT ) && ent.count == (int32_t)sess_a ) {
                            count_b = true;
                        }
                    }
                }
            }
            check( se.rider_of( cart4 ) == ea.fd, "cart ride: rider table set on mount" );
            check( count_a && count_b, "cart ride: COUNT state broadcast to rider and viewers (a=%d b=%d)", count_a, count_b );
            check( adopt_a == 1 && adopt_b == 0, "cart ride: sim transfers owner -> rider (a=%d b=%d)", adopt_a, adopt_b );
        }
        // A competing mount is refused silently.
        send_entity_action_kind( server, eb, cart4, NET_ACTION_MOUNT );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_STATE ).empty( ) &&
                       frames_of_type( fb, NetMsgType::ENTITY_STATE ).empty( ) &&
                       frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ).empty( ) &&
                       frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ).empty( ),
                   "cart ride: competing mount refused silently" );
            check( se.rider_of( cart4 ) == ea.fd, "cart ride: competing mount keeps the rider" );
        }
        // While ridden the chunk owner's state is ignored; the rider's state
        // relays to the other viewers.
        send_cart_state( server, eb, cart4, 71.0, 30.0, 10.0, 0.5f, 0.0f, 0.0f, 45.0f, 0 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            check( frames_of_type( fa, NetMsgType::ENTITY_STATE ).empty( ), "cart ride: owner state ignored while ridden" );
        }
        send_cart_state( server, ea, cart4, 71.0, 30.0, 10.0, 0.5f, 0.0f, 0.0f, 45.0f, 0 );
        {
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fb, NetMsgType::ENTITY_STATE ).size( ) == 1, "cart ride: rider state relayed" );
        }
        // HIT routes to the rider, not the chunk owner.
        send_entity_action( server, eb, cart4 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_ACTION ).size( ) == 1 &&
                       frames_of_type( fb, NetMsgType::ENTITY_ACTION ).empty( ),
                   "cart ride: HIT routed to rider" );
        }
        // A ridden cart crossing chunks keeps the rider — no transfer either
        // way. Chunk 0 is A-owned, so the usual crossing rule WOULD send
        // adopt=0/adopt=1 here if rider sim were bypassed.
        send_cart_state( server, ea, cart4, 30.0, 30.0, 10.0, 0.5f, 0.0f, 0.0f, 45.0f, 0 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ).empty( ) &&
                       frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ).empty( ),
                   "cart ride: ridden crossing sends no transfer" );
            check( se.entity_count_of( c0 ) == 1, "cart ride: ridden crossing still moves the record" );
        }
        send_cart_state( server, ea, cart4, 70.0, 30.0, 10.0, 0.5f, 0.0f, 0.0f, 45.0f, 0 );
        drain_client( server, ea );
        drain_client( server, eb );
        // Dismount hands the sim back to the chunk owner (B) with count 0.
        send_entity_action_kind( server, ea, cart4, NET_ACTION_DISMOUNT );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            int adopt_a = -1, adopt_b = -1;
            bool count_zero = false;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) && p.record.id == cart4 ) {
                    adopt_a = p.adopt;
                }
            }
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) && p.record.id == cart4 ) {
                    adopt_b = p.adopt;
                }
            }
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::ENTITY_STATE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityStatePayload p;
                if ( net_deserialize_entity_state( r, p ) ) {
                    for ( const NetEntityStateEntry &ent : p.entries ) {
                        if ( ent.id == cart4 && ( ent.present & NET_SE_COUNT ) && ent.count == 0 ) {
                            count_zero = true;
                        }
                    }
                }
            }
            check( count_zero, "cart ride: dismount broadcasts COUNT=0" );
            check( adopt_b == 1 && adopt_a == 0, "cart ride: dismount hands sim back to owner (a=%d b=%d)", adopt_a, adopt_b );
            check( se.rider_of( cart4 ) < 0, "cart ride: rider table cleared on dismount" );
        }
        // Re-mount by B: B owns the chunk, so no transfers — COUNT only. B is
        // left riding it; the owner-disconnect block below then exercises the
        // rider-disconnect path (owner == leaver -> persist stray).
        send_entity_action_kind( server, eb, cart4, NET_ACTION_MOUNT );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ).empty( ) &&
                       frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ).empty( ),
                   "cart ride: mount by the owner sends no transfer" );
            check( se.rider_of( cart4 ) == eb.fd, "cart ride: owner mounts without transfer" );
        }

        // Owner disconnect: put an item into B-owned chunk 1, drop B, and A
        // (still viewing) takes over with the records.
        const NetEntityId item4 = ( static_cast<NetEntityId>( sess_b ) << 32 ) | 1;
        send_entity_spawn( server, eb, item_record( item4, SAND, 7, 70.0, 30.0, 10.0 ) );
        drain_client( server, ea );
        drain_client( server, eb );
        disconnect_client( server, eb );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            bool got_owner = false;
            check( se.rider_of( cart4 ) < 0, "cart ride: rider disconnect clears the rider" );
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::CHUNK_ROLE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetChunkRolePayload p;
                if ( net_deserialize_chunk_role( r, p ) && glm::ivec3( p.cx, p.cy, p.cz ) == c1 && p.role == NET_ROLE_OWNER &&
                     p.records.size( ) == 1 && p.records[ 0 ].type == SAND && p.records[ 0 ].count == 7 ) {
                    got_owner = true;
                }
            }
            check( got_owner, "entity: owner disconnect hands OWNER + records to remaining viewer" );
            check( se.owner_of( c1 ) == ea.fd, "entity: A owns chunk 1 after B disconnect" );
            // B rode cart4 and owned its chunk — nobody remains to simulate
            // it, so the disconnect persists it as a stray carts_ record.
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<VehicleFileRecord> stray;
            const int existed = check_storage.load_vehicles( c1, stray );
            bool persisted = false;
            for ( const VehicleFileRecord &rec : stray ) {
                if ( rec.x == 70.0 && rec.yaw == 45.0f ) {
                    persisted = true;
                }
            }
            check( existed == 1 && persisted, "cart ride: rider+owner disconnect persists the cart (existed=%d n=%zu)",
                   existed, stray.size( ) );
        }
        disconnect_client( server, ea );
    }

    // === Mob sync specifics: NATURAL gating, CHUNK_POPULATED, persistence
    // filters, ENTITY_ACTION routing. Same chunk geometry as above.
    {
        SimClient ma = connect_client( server );
        SimClient mb = connect_client( server );
        drain_client( server, ma );
        drain_client( server, mb );
        send_player_location( server, ma, -70.0f, 30.0f, 10.0f ); // chunk -2
        send_player_location( server, mb, 130.0f, 30.0f, 10.0f ); // chunk 2
        send_view( server, ma, glm::ivec3( -1, 0, 0 ), 2, 1 );    // covers -3..1
        send_view( server, mb, glm::ivec3( 2, 0, 0 ), 2, 1 );     // covers 0..4
        drain_client( server, ma );
        drain_client( server, mb );
        const glm::ivec3 c_n2( -2, 0, 0 );
        const ServerEntities &se2 = server.test_logic( ).test_entities( );
        check( se2.owner_of( c_n2 ) == ma.fd, "mob: chunk -2 owned by A" );
        check( se2.owner_of( c2 ) == mb.fd, "mob: chunk 2 owned by B" );

        // A natural-spawn record from the owner of an unrolled chunk is
        // accepted. Natural ids encode home chunk + roll index.
        const NetEntityId nat1 = net_natural_mob_id( c_n2, 0 );
        send_entity_spawn( server, ma, mob_record( nat1, MOB_PIG, NET_MOB_FLAG_NATURAL, -70.0, 30.0, 10.0 ) );
        check( se2.entity_count_of( c_n2 ) == 1, "mob: natural spawn from owner accepted" );
        drain_client( server, ma );
        drain_client( server, mb );
        // The same id re-sent (re-roll after an ownership flip) dedupes
        // silently: the id is already live.
        send_entity_spawn( server, ma, mob_record( nat1, MOB_PIG, NET_MOB_FLAG_NATURAL, -70.0, 30.0, 10.0 ) );
        check( se2.entity_count_of( c_n2 ) == 1, "mob: duplicate natural id dedupes" );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ma );
            check( frames_of_type( fa, NetMsgType::ENTITY_REMOVE ).empty( ), "mob: live dup spawn gets no ENTITY_REMOVE" );
        }

        // CHUNK_POPULATED from a non-owner is ignored; from the owner it
        // marks the chunk rolled and rejects later natural spawns.
        send_chunk_populated( server, mb, c_n2 );
        check( !se2.rolled_of( c_n2 ), "mob: CHUNK_POPULATED from non-owner ignored" );
        send_chunk_populated( server, ma, c_n2 );
        check( se2.rolled_of( c_n2 ), "mob: CHUNK_POPULATED from owner marks chunk rolled" );
        // A late natural spawn after the roll is dropped; since its id is
        // not live the sender gets ENTITY_REMOVE so its local copy dies.
        const NetEntityId nat2 = net_natural_mob_id( c_n2, 1 );
        send_entity_spawn( server, ma, mob_record( nat2, MOB_COW, NET_MOB_FLAG_NATURAL, -71.0, 30.0, 10.0 ) );
        check( se2.entity_count_of( c_n2 ) == 1, "mob: natural spawn after rolled rejected" );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ma );
            int removes = 0;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_REMOVE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityRemovePayload p;
                if ( net_deserialize_entity_remove( r, p ) && p.id == nat2 && p.reason == NET_REMOVE_DESPAWN ) {
                    removes++;
                }
            }
            check( removes == 1, "mob: rolled drop replies ENTITY_REMOVE (got %d)", removes );
        }

        // Natural spawn into B's chunk from A (not its owner) is dropped.
        const NetEntityId nat3 = net_natural_mob_id( c2, 0 );
        send_entity_spawn( server, ma, mob_record( nat3, MOB_SHEEP, NET_MOB_FLAG_NATURAL, 130.0, 30.0, 10.0 ) );
        check( se2.entity_count_of( c2 ) == 0, "mob: natural spawn from non-owner rejected" );
        drain_client( server, ma );

        // ENTITY_ACTION routes to the entity chunk's owner, whoever sent it.
        const NetEntityId despawn_mob = ( static_cast<NetEntityId>( 1 ) << 32 ) | 0x77;
        send_entity_spawn( server, mb, mob_record( despawn_mob, MOB_ZOMBIE, NET_MOB_FLAG_DESPAWNABLE, 130.0, 30.0, 10.0 ) );
        drain_client( server, ma );
        drain_client( server, mb );
        send_entity_action( server, ma, despawn_mob );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ma );
            const std::vector<ReceivedFrame> fb = drain_client( server, mb );
            check( frames_of_type( fa, NetMsgType::ENTITY_ACTION ).empty( ), "mob: action not sent to non-owner" );
            check( frames_of_type( fb, NetMsgType::ENTITY_ACTION ).size( ) == 1, "mob: action forwarded to owner" );
        }

        // Despawnable mobs are never persisted: uncovering chunk 2 leaves no
        // mobs file (it wasn't populated either).
        send_view( server, mb, glm::ivec3( 3, 0, 0 ), 0, 1 ); // move cover off chunk 2
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<MobFileRecord> persisted;
            const int existed = check_storage.load_mobs( c2, persisted );
            check( existed == 0, "mob: despawnable-only chunk leaves no mobs file (existed=%d)", existed );
        }

        // Chunk -2 holds only the pristine nat1: uncovering writes no file
        // at all — it will be deterministically re-rolled on the next cover.
        send_view( server, ma, glm::ivec3( -1, 0, 0 ), 0, 1 ); // covers only -1: uncovers -2
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<MobFileRecord> persisted;
            const int existed = check_storage.load_mobs( c_n2, persisted );
            check( existed == 0, "mob: pristine-only chunk leaves no mobs file (existed=%d)", existed );
        }
        // Re-cover: the chunk rolls fresh (populated=0), its record list is
        // empty and it has no tombstones — so the owner can spawn nat1
        // again.
        send_view( server, ma, glm::ivec3( -1, 0, 0 ), 2, 1 ); // re-cover
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ma );
            int seen = 0;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::CHUNK_ROLE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetChunkRolePayload p;
                if ( net_deserialize_chunk_role( r, p ) && glm::ivec3( p.cx, p.cy, p.cz ) == c_n2 ) {
                    seen = ( p.populated == 0 && p.records.empty( ) && p.removed_natural.empty( ) ) ? 1 : -1;
                }
            }
            check( seen == 1, "mob: re-cover after pristine drop reports empty roll (seen=%d)", seen );
        }
        send_entity_spawn( server, ma, mob_record( nat1, MOB_PIG, NET_MOB_FLAG_NATURAL, -70.0, 30.0, 10.0 ) );
        check( se2.entity_count_of( c_n2 ) == 1, "mob: re-covered chunk accepts the re-rolled natural" );

        // A TOUCHED flag arriving via ENTITY_STATE tombstones the roll index
        // in the home chunk.
        send_mob_state( server, ma, nat1, -70.0, 30.0, 10.0, NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED );
        check( se2.removed_natural_of( c_n2 ) == 1, "mob: TOUCHED state tombstones the natural index" );
        send_chunk_populated( server, ma, c_n2 );
        check( se2.rolled_of( c_n2 ), "mob: re-roll marks chunk rolled" );

        // Uncover: the touched record persists with its natural net_id and
        // provenance flags; the removed set holds index 0.
        send_view( server, ma, glm::ivec3( -1, 0, 0 ), 0, 1 ); // uncovers -2 again
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<MobFileRecord> persisted;
            std::vector<uint8_t> removed;
            const int existed = check_storage.load_mobs( c_n2, persisted, &removed );
            check( existed == 1 && persisted.size( ) == 1 && persisted[ 0 ].type == MOB_PIG && persisted[ 0 ].net_id == nat1 &&
                       ( persisted[ 0 ].flags & NET_MOB_FLAG_NATURAL ) != 0 && ( persisted[ 0 ].flags & NET_MOB_FLAG_TOUCHED ) != 0 &&
                       removed.size( ) == 1 && removed[ 0 ] == 0,
                   "mob: touched natural persists record + tombstone (existed=%d n=%zu rem=%zu)", existed, persisted.size( ),
                   removed.size( ) );
        }
        // Re-cover: populated=0 (rolls fresh), removed_natural carries the
        // tombstoned index, and the record keeps its natural id.
        send_view( server, ma, glm::ivec3( -1, 0, 0 ), 2, 1 ); // re-cover
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ma );
            int seen = 0;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::CHUNK_ROLE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetChunkRolePayload p;
                if ( net_deserialize_chunk_role( r, p ) && glm::ivec3( p.cx, p.cy, p.cz ) == c_n2 ) {
                    const bool rec_ok = p.records.size( ) == 1 && p.records[ 0 ].id == nat1;
                    const bool rem_ok = p.removed_natural.size( ) == 1 && p.removed_natural[ 0 ] == 0;
                    seen = ( p.populated == 0 && rec_ok && rem_ok ) ? 1 : -1;
                }
            }
            check( seen == 1, "mob: re-cover sends tombstone + natural record (seen=%d)", seen );
        }
        // The restored record is live: re-sending nat1 is a silent dup.
        send_entity_spawn( server, ma, mob_record( nat1, MOB_PIG, NET_MOB_FLAG_NATURAL, -70.0, 30.0, 10.0 ) );
        check( se2.entity_count_of( c_n2 ) == 1, "mob: restored natural id dedupes" );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ma );
            check( frames_of_type( fa, NetMsgType::ENTITY_REMOVE ).empty( ), "mob: restored dup gets no ENTITY_REMOVE" );
        }
        // Remove the live record; the tombstone stays. A re-sent spawn for
        // the tombstoned index now bounces with ENTITY_REMOVE.
        send_entity_remove( server, ma, nat1, NET_REMOVE_DESPAWN );
        check( se2.entity_count_of( c_n2 ) == 0, "mob: owner remove drops the record" );
        send_entity_spawn( server, ma, mob_record( nat1, MOB_PIG, NET_MOB_FLAG_NATURAL, -70.0, 30.0, 10.0 ) );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ma );
            int removes = 0;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::ENTITY_REMOVE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityRemovePayload p;
                if ( net_deserialize_entity_remove( r, p ) && p.id == nat1 ) {
                    removes++;
                }
            }
            check( se2.entity_count_of( c_n2 ) == 0 && removes == 1, "mob: tombstoned spawn bounces ENTITY_REMOVE (n=%d rem=%d)",
                   se2.entity_count_of( c_n2 ), removes );
        }
        // Uncover again: a file with no records but the tombstone persists.
        send_view( server, ma, glm::ivec3( -1, 0, 0 ), 0, 1 ); // uncovers -2
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<MobFileRecord> persisted;
            std::vector<uint8_t> removed;
            const int existed = check_storage.load_mobs( c_n2, persisted, &removed );
            check( existed == 1 && persisted.empty( ) && removed.size( ) == 1 && removed[ 0 ] == 0,
                   "mob: tombstone-only chunk persists removed set (existed=%d n=%zu rem=%zu)", existed, persisted.size( ), removed.size( ) );
        }

        // A rolled chunk with nothing to keep leaves no file either (no
        // populated-marker semantics anymore).
        send_view( server, ma, glm::ivec3( -1, 0, 0 ), 0, 1 ); // cover only -1
        drain_client( server, ma );
        send_chunk_populated( server, ma, glm::ivec3( -1, 0, 0 ) );
        send_view( server, ma, glm::ivec3( 5, 0, 0 ), 0, 1 ); // move cover away from -1 (uncovers it)
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<MobFileRecord> persisted;
            const int existed = check_storage.load_mobs( glm::ivec3( -1, 0, 0 ), persisted );
            check( existed == 0, "mob: zero-mob rolled chunk leaves no file (existed=%d)", existed );
        }

        disconnect_client( server, ma );
        disconnect_client( server, mb );
    }

    // === Body sync: the causer simulates, not the chunk owner. ba owns no
    // chunk the body sits in; bb owns chunk 2; bc is an extra viewer.
    {
        SimClient ba = connect_client( server );
        SimClient bb = connect_client( server );
        SimClient bc = connect_client( server );
        const std::vector<ReceivedFrame> wba = drain_client( server, ba );
        drain_client( server, bb );
        drain_client( server, bc );
        const uint32_t sess_ba = welcome_session( wba );

        send_player_location( server, ba, -70.0f, 30.0f, 10.0f ); // chunk -2
        send_player_location( server, bb, 130.0f, 30.0f, 10.0f ); // chunk 2
        send_player_location( server, bc, 250.0f, 30.0f, 10.0f ); // chunk 4
        send_view( server, ba, glm::ivec3( -1, 0, 0 ), 2, 1 ); // covers -3..1
        send_view( server, bb, glm::ivec3( 2, 0, 0 ), 2, 1 );  // covers 0..4
        send_view( server, bc, glm::ivec3( 4, 0, 0 ), 0, 1 );  // covers only 4
        drain_client( server, ba );
        drain_client( server, bb );
        drain_client( server, bc );

        const glm::ivec3 c4( 4, 0, 0 );
        const ServerEntities &se3 = server.test_logic( ).test_entities( );
        check( se3.owner_of( c2 ) == bb.fd, "body: chunk 2 owned by B" );
        check( se3.owner_of( c4 ) == bb.fd, "body: chunk 4 owned by B (hysteresis over C)" );

        // ba spawns a primed-TNT body in bb-owned chunk 2: accepted, relayed
        // to the viewers (bb). ba isn't a viewer of chunk 2, so the spawn
        // echo doesn't reach it — only viewing senders get their own ack.
        const NetEntityId body1 = ( static_cast<NetEntityId>( sess_ba ) << 32 ) | 0xB1;
        send_entity_spawn( server, ba, body_record( body1, TEST_BODY_TNT, TNT, 40, 130.0, 30.0, 10.0 ) );
        check( se3.entity_count_of( c2 ) == 1, "body: causer spawn into foreign chunk accepted" );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ba );
            const std::vector<ReceivedFrame> fb = drain_client( server, bb );
            check( frames_of_type( fa, NetMsgType::ENTITY_SPAWN ).empty( ), "body: no echo to non-viewer sim" );
            check( frames_of_type( fb, NetMsgType::ENTITY_SPAWN ).size( ) == 1, "body: spawn relayed to viewer" );
        }

        // ENTITY_STATE / ENTITY_REMOVE from the chunk owner (not the sim) are
        // ignored; the sim's state relays.
        send_entity_state( server, bb, body1, 131.0, 30.0, 10.0, 0 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ba );
            check( frames_of_type( fa, NetMsgType::ENTITY_STATE ).empty( ), "body: chunk-owner state ignored (not sim)" );
        }
        send_entity_remove( server, bb, body1, NET_REMOVE_EXPLODED );
        check( se3.entity_count_of( c2 ) == 1, "body: chunk-owner remove ignored (not sim)" );
        send_entity_state( server, ba, body1, 131.0, 30.0, 10.0, 0 );
        {
            const std::vector<ReceivedFrame> fb = drain_client( server, bb );
            check( frames_of_type( fb, NetMsgType::ENTITY_STATE ).size( ) == 1, "body: sim state relayed to viewer" );
        }

        // Crossing c2 -> c4: no ENTITY_TRANSFER for bodies; viewer fan-out
        // spawns it for bc (who didn't view c2).
        send_entity_state( server, ba, body1, 250.0, 30.0, 10.0, 0 );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ba );
            const std::vector<ReceivedFrame> fb = drain_client( server, bb );
            const std::vector<ReceivedFrame> fc = drain_client( server, bc );
            check( frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ).empty( ) &&
                       frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ).empty( ) &&
                       frames_of_type( fc, NetMsgType::ENTITY_TRANSFER ).empty( ),
                   "body: chunk crossing never transfers" );
            check( frames_of_type( fc, NetMsgType::ENTITY_SPAWN ).size( ) == 1, "body: crossing spawns for new viewer" );
            check( frames_of_type( fb, NetMsgType::ENTITY_REMOVE ).empty( ), "body: shared viewer keeps it" );
        }

        // Crossing into an uncovered chunk drops the record silently: the
        // sim keeps going (no messages), nothing is persisted.
        send_entity_state( server, ba, body1, 60.0 * 64.0, 30.0, 10.0, 0 ); // chunk 60, uncovered
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ba );
            const std::vector<ReceivedFrame> fb = drain_client( server, bb );
            const std::vector<ReceivedFrame> fc = drain_client( server, bc );
            check( frames_of_type( fa, NetMsgType::ENTITY_REMOVE ).empty( ) &&
                       frames_of_type( fa, NetMsgType::ENTITY_TRANSFER ).empty( ),
                   "body: uncovered crossing tells the sim nothing" );
            check( se3.entity_count_of( c4 ) == 0, "body: uncovered crossing drops the record" );
            (void)fb;
            (void)fc;
            send_entity_state( server, ba, body1, 3900.0, 30.0, 10.0, 0 );
            const std::vector<ReceivedFrame> fb2 = drain_client( server, bb );
            check( frames_of_type( fb2, NetMsgType::ENTITY_STATE ).empty( ), "body: dropped record ignores later states" );
        }

        // BODY records are never persisted: park one in chunk -2 (covered
        // only by ba), uncover it, re-cover — the role grant comes back
        // empty.
        const glm::ivec3 c_n2b( -2, 0, 1 );
        const NetEntityId body2 = ( static_cast<NetEntityId>( sess_ba ) << 32 ) | 0xB2;
        send_entity_spawn( server, ba, body_record( body2, TEST_BODY_FALLING, SAND, 0, -70.0, 30.0, 70.0 ) );
        check( se3.entity_count_of( c_n2b ) == 1, "body: second spawn lands in -2,0,1" );
        drain_client( server, ba );
        drain_client( server, bb );
        drain_client( server, bc );
        send_view( server, ba, glm::ivec3( -1, 0, 0 ), 0, 1 ); // uncovers -2
        drain_client( server, ba );
        {
            MapStorage check_storage;
            check_storage.init( TEST_WORLD );
            std::vector<ItemFileRecord> items;
            std::vector<MobFileRecord> mobs;
            check( check_storage.load_items( c_n2b, items ) == 0 && check_storage.load_mobs( c_n2b, mobs ) == 0,
                   "body: uncovered body writes nothing to disk" );
        }
        send_view( server, ba, glm::ivec3( -1, 0, 0 ), 2, 1 ); // re-cover -2
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ba );
            int rec_count = -1;
            for ( const ReceivedFrame &f : frames_of_type( fa, NetMsgType::CHUNK_ROLE ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetChunkRolePayload p;
                if ( net_deserialize_chunk_role( r, p ) && glm::ivec3( p.cx, p.cy, p.cz ) == c_n2b ) {
                    rec_count = static_cast<int>( p.records.size( ) );
                }
            }
            check( rec_count == 0, "body: re-covered chunk has no resurrected body (records=%d)", rec_count );
        }

        // Spawn into an uncovered chunk is ignored outright (no stray
        // persistence, no record).
        const NetEntityId body_far = ( static_cast<NetEntityId>( sess_ba ) << 32 ) | 0xBF;
        send_entity_spawn( server, ba, body_record( body_far, TEST_BODY_FALLING, SAND, 0, 70.0 * 64.0, 30.0, 10.0 ) );
        send_entity_state( server, ba, body_far, 70.0 * 64.0 + 1.0, 30.0, 10.0, 0 );
        {
            const std::vector<ReceivedFrame> fb = drain_client( server, bb );
            check( frames_of_type( fb, NetMsgType::ENTITY_STATE ).empty( ), "body: uncovered spawn leaves no record" );
        }

        // Sim disconnect: body3 sits in bb-owned c2 — the sim role hands
        // over to the chunk owner with ENTITY_TRANSFER adopt=1; the mirror
        // stream continues from the new sim.
        const NetEntityId body3 = ( static_cast<NetEntityId>( sess_ba ) << 32 ) | 0xB3;
        send_entity_spawn( server, ba, body_record( body3, TEST_BODY_TNT, TNT, 40, 130.0, 30.0, 10.0 ) );
        drain_client( server, ba );
        drain_client( server, bb );
        drain_client( server, bc );
        disconnect_client( server, ba );
        {
            const std::vector<ReceivedFrame> fb = drain_client( server, bb );
            int adopt = -1;
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::ENTITY_TRANSFER ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetEntityTransferPayload p;
                if ( net_deserialize_entity_transfer( r, p ) && p.record.id == body3 ) {
                    adopt = p.adopt;
                }
            }
            check( adopt == 1, "body: sim disconnect hands ENTITY_TRANSFER adopt=1 to chunk owner" );
            // The new sim's states are accepted: moving it into c4 spawns
            // the body for bc (a viewer of c4 that never saw c2).
            send_entity_state( server, bb, body3, 250.0, 30.0, 10.0, 0 );
            drain_client( server, bb );
            const std::vector<ReceivedFrame> fc2 = drain_client( server, bc );
            check( frames_of_type( fc2, NetMsgType::ENTITY_SPAWN ).size( ) == 1, "body: post-handover sim state moves the record" );
        }

        disconnect_client( server, bb );
        disconnect_client( server, bc );
    }

    // === Container sync: CONTAINER_* family. Two subscribers on one chest,
    // optimistic slot writes relayed with a revision, a rejected write
    // corrected with the authoritative snapshot, loot-on-break, disconnect
    // cleanup, furnace ticking + lit-swap, and sidecar persistence.
    {
        SimClient ca = connect_client( server );
        SimClient cb = connect_client( server );
        drain_client( server, ca );
        drain_client( server, cb );
        ServerContainers &sc = server.test_logic( ).test_containers( );
        const glm::ivec3 chest_pos( 20, 40, 20 );
        const glm::ivec3 furnace_pos( 24, 40, 20 );

        // OPEN: each subscriber gets a CONTENTS snapshot (empty, rev 0).
        send_container_open( server, ca, NetContainerKind::CHEST, chest_pos );
        send_container_open( server, cb, NetContainerKind::CHEST, chest_pos );
        check( sc.subscriber_count( NetContainerKind::CHEST, chest_pos ) == 2,
               "container: two subscribers on one chest" );
        {
            NetContainerContentsPayload p;
            check( last_container_contents( drain_client( server, ca ), chest_pos, p ) &&
                       p.kind == NetContainerKind::CHEST && p.slots.size( ) == CHEST_SLOTS && p.revision == 0,
                   "container: open answers CONTENTS (kind, arity, rev 0)" );
            check( last_container_contents( drain_client( server, cb ), chest_pos, p ) &&
                       p.slots.size( ) == CHEST_SLOTS,
                   "container: second open answers CONTENTS" );
        }

        // A slot write lands server-side and relays to the OTHER viewer,
        // stamped with the bumped revision.
        send_container_set_slot( server, ca, NetContainerKind::CHEST, chest_pos, 3, InventorySlot{ STONE, 7 } );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ca );
            const std::vector<ReceivedFrame> fb = drain_client( server, cb );
            check( frames_of_type( fa, NetMsgType::CONTAINER_SET_SLOT ).empty( ),
                   "container: accepted write not echoed to writer" );
            uint32_t got_rev = 0;
            bool got_slot = false;
            for ( const ReceivedFrame &f : frames_of_type( fb, NetMsgType::CONTAINER_SET_SLOT ) ) {
                PacketReader r( f.payload.data( ), f.payload.size( ) );
                NetContainerSetSlotPayload p;
                if ( net_deserialize_container_set_slot( r, p ) && glm::ivec3( p.x, p.y, p.z ) == chest_pos ) {
                    got_rev = p.revision;
                    got_slot = p.slot_index == 3 && p.slot_value.item_id == STONE && p.slot_value.quantity == 7;
                }
            }
            check( got_slot && got_rev == 1, "container: viewer got SET_SLOT rev=1 (rev=%u)", got_rev );
        }
        {
            const ServerContainerEntry *ce = sc.find( NetContainerKind::CHEST, chest_pos );
            check( ce != nullptr && ce->contents.slots.size( ) == CHEST_SLOTS &&
                       ce->contents.slots[ 3 ].item_id == STONE && ce->contents.slots[ 3 ].quantity == 7 && ce->revision == 1,
                   "container: server contents updated (rev bumped)" );
        }

        // Unauthorized write (a container the client never opened): the
        // server rejects it AND pushes the authoritative snapshot back so
        // the optimistic client self-heals.
        const glm::ivec3 rogue_pos( 21, 40, 20 );
        send_container_set_slot( server, cb, NetContainerKind::CHEST, rogue_pos, 0, InventorySlot{ GOLD_BLOCK, 99 } );
        {
            NetContainerContentsPayload p;
            check( last_container_contents( drain_client( server, cb ), rogue_pos, p ) &&
                       p.slots.size( ) == CHEST_SLOTS && p.slots[ 0 ].item_id == LAST_BLOCK_ID,
                   "container: rejected write corrected with authoritative snapshot" );
            check( sc.find( NetContainerKind::CHEST, rogue_pos ) == nullptr,
                   "container: rejected write stored nothing" );
        }

        // CLOSE drops only that client; the other stays subscribed.
        send_container_close( server, ca, NetContainerKind::CHEST, chest_pos );
        check( sc.subscriber_count( NetContainerKind::CHEST, chest_pos ) == 1,
               "container: close drops one subscriber (got %d)",
               sc.subscriber_count( NetContainerKind::CHEST, chest_pos ) );

        // Loot-on-break: overwriting the chest block snapshots the contents
        // to the breaker, then clears the entry + its subscriptions.
        send_block_update( server, cb, chest_pos, BLOCK_STATE_AIR );
        {
            NetContainerContentsPayload p;
            check( last_container_contents( drain_client( server, cb ), chest_pos, p ) &&
                       p.slots.size( ) == CHEST_SLOTS && p.slots[ 3 ].item_id == STONE && p.slots[ 3 ].quantity == 7,
                   "container: breaker received loot snapshot" );
        }
        check( sc.find( NetContainerKind::CHEST, chest_pos ) == nullptr &&
                   sc.subscriber_count( NetContainerKind::CHEST, chest_pos ) == 0,
               "container: break clears entry + subscribers" );
        drain_client( server, ca );

        // Furnace: place the block (server learns the state), open, load a
        // smelt — ticking then ignites, swaps the block to LIT_FURNACE for
        // every client, and pushes throttled progress snapshots.
        send_block_update( server, ca, furnace_pos, BlockState{ FURNACE, 0, 0, FURNACE, 0 } );
        send_container_open( server, ca, NetContainerKind::FURNACE, furnace_pos );
        {
            NetContainerContentsPayload p;
            check( last_container_contents( drain_client( server, ca ), furnace_pos, p ) &&
                       p.kind == NetContainerKind::FURNACE && p.slots.size( ) == FURNACE_INVENTORY_SLOTS && p.aux.size( ) == 2,
                   "container: furnace open answers 3-slot CONTENTS with aux" );
        }
        send_container_set_slot( server, ca, NetContainerKind::FURNACE, furnace_pos, FURNACE_SLOT_INPUT, InventorySlot{ RAW_IRON, 1 } );
        send_container_set_slot( server, ca, NetContainerKind::FURNACE, furnace_pos, FURNACE_SLOT_FUEL, InventorySlot{ COAL, 1 } );
        drain_client( server, ca );
        check( sc.any_active( ), "container: loaded furnace is active" );
        for ( int i = 0; i < 3; i++ ) {
            server.test_logic( ).processTimers( server );
        }
        {
            const ServerContainerEntry *fe = sc.find( NetContainerKind::FURNACE, furnace_pos );
            check( fe != nullptr && fe->contents.aux.size( ) == 2 && fe->contents.aux[ 1 ] > 0,
                   "container: furnace ignited (burn_ticks=%d)",
                   fe != nullptr && fe->contents.aux.size( ) > 1 ? fe->contents.aux[ 1 ] : -1 );
            const std::vector<ReceivedFrame> fa = drain_client( server, ca );
            bool lit_update = false;
            bool progress = false;
            for ( const ReceivedFrame &f : fa ) {
                if ( f.type == NetMsgType::BLOCK_UPDATE ) {
                    PacketReader r( f.payload.data( ), f.payload.size( ) );
                    NetBlockUpdatePayload bu;
                    if ( net_deserialize_block_update( r, bu ) && glm::ivec3( bu.x, bu.y, bu.z ) == furnace_pos &&
                         bu.blockState.id == LIT_FURNACE ) {
                        lit_update = true;
                    }
                } else if ( f.type == NetMsgType::CONTAINER_CONTENTS ) {
                    PacketReader r( f.payload.data( ), f.payload.size( ) );
                    NetContainerContentsPayload p;
                    if ( net_deserialize_container_contents( r, p ) && glm::ivec3( p.x, p.y, p.z ) == furnace_pos &&
                         p.aux.size( ) == 2 && p.aux[ 1 ] > 0 ) {
                        progress = true;
                    }
                }
            }
            check( lit_update, "container: lit swap broadcast LIT_FURNACE" );
            check( progress, "container: throttled progress snapshot reached viewer" );
        }

        // Disconnect cleanup: the furnace viewer's subscription drops.
        disconnect_client( server, ca );
        check( sc.subscriber_count( NetContainerKind::FURNACE, furnace_pos ) == 0,
               "container: disconnect unsubscribes" );
        disconnect_client( server, cb );

        // Persistence: the last-disconnect flush wrote the furnace sidecar —
        // a fresh client re-opening reloads the burning contents.
        SimClient cc = connect_client( server );
        send_container_open( server, cc, NetContainerKind::FURNACE, furnace_pos );
        {
            NetContainerContentsPayload p;
            check( last_container_contents( drain_client( server, cc ), furnace_pos, p ) &&
                       p.slots.size( ) == FURNACE_INVENTORY_SLOTS && p.slots[ FURNACE_SLOT_INPUT ].item_id == RAW_IRON &&
                       p.aux.size( ) == 2 && p.aux[ 1 ] > 0,
                   "container: furnace contents persist across sessions (still burning)" );
        }
        disconnect_client( server, cc );
    }

    // === Sign text: SIGN_TEXT both directions. Edits validate against the
    // recorded block, sanitize, store, and broadcast to every client;
    // chunk-diff requests re-sync the stored text; a break clears and
    // re-broadcasts; the sidecar persists across cache eviction.
    {
        SimClient sa = connect_client( server );
        SimClient sb = connect_client( server );
        drain_client( server, sa );
        drain_client( server, sb );
        ServerSigns &ss = server.test_logic( ).test_signs( );
        const glm::ivec3 sign_pos( 30, 40, 20 );
        const glm::ivec3 sign_chunk = glm::floor( glm::vec3( sign_pos ) / CHUNK_SIZE_F );

        // Text for a cell with no recorded sign is rejected silently.
        SignText early;
        early.lines[ 0 ] = "TOO EARLY";
        send_sign_text( server, sa, sign_pos, early );
        check( ss.find( sign_pos ) == nullptr, "sign: non-sign cell rejects text" );

        // The server learns the sign block via BLOCK_UPDATE.
        send_block_update( server, sa, sign_pos, BlockState{ STANDING_SIGN, 0, 0, STANDING_SIGN, 0 } );
        drain_client( server, sa );
        drain_client( server, sb );

        // An edit stores the sanitized text and broadcasts to both
        // clients — the sender gets the same authoritative echo.
        SignText text;
        text.lines[ 0 ] = "HI\xff THERE"; // \xff must not survive
        text.lines[ 1 ] = "SECOND LINE";
        send_sign_text( server, sa, sign_pos, text );
        const SignText *stored = ss.find( sign_pos );
        check( stored != nullptr && stored->lines[ 0 ] == "HI THERE" && stored->lines[ 1 ] == "SECOND LINE",
               "sign: edit stored sanitized" );
        {
            NetSignTextPayload p;
            check( last_sign_text( drain_client( server, sa ), sign_pos, p ) && p.text.lines[ 0 ] == "HI THERE",
                   "sign: writer gets authoritative echo" );
            check( last_sign_text( drain_client( server, sb ), sign_pos, p ) && p.text.lines[ 0 ] == "HI THERE" &&
                       p.text.lines[ 1 ] == "SECOND LINE",
                   "sign: broadcast reaches other client" );
        }

        // The chunk-diff path re-sends stored text — what a late joiner
        // sees when it requests the chunk.
        send_diff_request( server, sb, sign_chunk, 1, 1, 1 );
        {
            NetSignTextPayload p;
            check( last_sign_text( drain_client( server, sb ), sign_pos, p ) && p.text.lines[ 1 ] == "SECOND LINE",
                   "sign: diff request re-syncs stored text" );
        }

        // All-empty text erases the record and broadcasts the clear.
        send_sign_text( server, sb, sign_pos, SignText{ } );
        check( ss.find( sign_pos ) == nullptr, "sign: empty edit erases record" );
        {
            NetSignTextPayload p;
            check( last_sign_text( drain_client( server, sa ), sign_pos, p ) && p.text.empty( ),
                   "sign: erase broadcast as empty lines" );
        }
        drain_client( server, sb );

        // Re-edit, then breaking the block clears and re-broadcasts.
        send_sign_text( server, sa, sign_pos, text );
        drain_client( server, sa );
        send_block_update( server, sa, sign_pos, BLOCK_STATE_AIR );
        check( ss.find( sign_pos ) == nullptr, "sign: break clears server record" );
        {
            NetSignTextPayload p;
            check( last_sign_text( drain_client( server, sb ), sign_pos, p ) && p.text.empty( ),
                   "sign: break broadcasts the clear" );
        }

        // Persistence: a fresh sign + text, then flush + evict drops the
        // cache; the next chunk request reloads from the side file.
        send_block_update( server, sa, sign_pos, BlockState{ STANDING_SIGN, 0, 0, STANDING_SIGN, 0 } );
        send_sign_text( server, sa, sign_pos, text );
        server.test_logic( ).flushAll( );
        ss.evict_chunk( sign_chunk );
        check( ss.find( sign_pos ) == nullptr, "sign: cache evicted after persist" );
        send_diff_request( server, sb, sign_chunk, 1, 1, 1 );
        {
            NetSignTextPayload p;
            check( last_sign_text( drain_client( server, sb ), sign_pos, p ) && p.text.lines[ 0 ] == "HI THERE",
                   "sign: text survives persist + reload" );
        }

        disconnect_client( server, sa );
        disconnect_client( server, sb );
    }

    // === Chat + player names: PLAYER_NAME sets/dedupes/renames names with a
    // broadcast + a SYSTEM line; CHAT broadcasts stamped with the server's
    // name (spoofed `from` ignored); whispers route target + sender echo;
    // disconnect announces the leave; late joiners get the name replay. ===
    {
        SimClient ca = connect_client( server );
        SimClient cb = connect_client( server );
        drain_client( server, ca );
        drain_client( server, cb );
        ServerChat &chat = server.test_logic( ).test_chat( );

        // Name set: stored, broadcast to both (player_id = sender fd), and
        // announced with a "joined" SYSTEM line.
        send_player_name( server, ca, "Alice" );
        check( chat.name_table( ).count( ca.fd ) && chat.name_table( ).at( ca.fd ) == "Alice",
               "chat: name set stored" );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            const ChatFrames fb = drain_chat_frames( server, cb );
            check( fa.names.size( ) == 1 && fa.names[ 0 ].first == ca.fd && fa.names[ 0 ].second == "Alice",
                   "chat: name broadcast reaches sender" );
            check( fb.names.size( ) == 1 && fb.names[ 0 ].first == ca.fd && fb.names[ 0 ].second == "Alice",
                   "chat: name broadcast reaches peer" );
            check( fb.chats.size( ) == 1 && fb.chats[ 0 ].kind == CHAT_KIND_SYSTEM && fb.chats[ 0 ].text == "Alice joined the game",
                   "chat: join announces SYSTEM line" );
        }

        // Case-insensitive dedupe: "alice" on a different fd becomes alice_2.
        send_player_name( server, cb, "alice" );
        check( chat.name_table( ).at( cb.fd ) == "alice_2", "chat: dedupe appends _2" );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            check( fa.names.size( ) == 1 && fa.names[ 0 ].first == cb.fd && fa.names[ 0 ].second == "alice_2",
                   "chat: deduped name broadcast" );
        }
        drain_chat_frames( server, cb );

        // Rename announces old -> new.
        send_player_name( server, ca, "Alicia" );
        {
            const ChatFrames fb = drain_chat_frames( server, cb );
            check( fb.chats.size( ) == 1 && fb.chats[ 0 ].text == "Alice is now known as Alicia",
                   "chat: rename announces old -> new" );
        }
        drain_chat_frames( server, ca );

        // Chat broadcast: the server stamps `from` — a spoofed field is
        // ignored — and everyone including the sender gets the line.
        send_chat( server, ca, CHAT_KIND_PLAYER, "EvilSpoof", "", "hello there" );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            const ChatFrames fb = drain_chat_frames( server, cb );
            check( fa.chats.size( ) == 1 && fa.chats[ 0 ].kind == CHAT_KIND_PLAYER &&
                       fa.chats[ 0 ].from == "Alicia" && fa.chats[ 0 ].text == "hello there",
                   "chat: broadcast stamped with server name, echoes to sender" );
            check( fb.chats.size( ) == 1 && fb.chats[ 0 ].from == "Alicia" && fb.chats[ 0 ].text == "hello there",
                   "chat: broadcast reaches peer stamped" );
        }

        // Whisper: only the target gets it plus an echo to the sender.
        SimClient cc = connect_client( server );
        drain_client( server, cc );
        send_chat( server, ca, CHAT_KIND_WHISPER, "", "ALICE_2", "psst" );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            const ChatFrames fb = drain_chat_frames( server, cb );
            const ChatFrames fc = drain_chat_frames( server, cc );
            check( fa.chats.size( ) == 1 && fa.chats[ 0 ].kind == CHAT_KIND_WHISPER &&
                       fa.chats[ 0 ].from == "Alicia" && fa.chats[ 0 ].target == "alice_2" && fa.chats[ 0 ].text == "psst",
                   "chat: whisper echoes to sender" );
            check( fb.chats.size( ) == 1 && fb.chats[ 0 ].kind == CHAT_KIND_WHISPER && fb.chats[ 0 ].target == "alice_2",
                   "chat: whisper reaches target (case-insensitive)" );
            check( fc.chats.empty( ), "chat: whisper hidden from bystanders" );
        }

        // Self-whisper: the target send and the sender echo are the same
        // delivery — exactly one line, not two.
        send_chat( server, ca, CHAT_KIND_WHISPER, "", "alicia", "note to self" );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            check( fa.chats.size( ) == 1 && fa.chats[ 0 ].kind == CHAT_KIND_WHISPER &&
                       fa.chats[ 0 ].from == "Alicia" && fa.chats[ 0 ].target == "Alicia",
                   "chat: self-whisper delivers once" );
        }

        // Unknown target errors to the sender only.
        send_chat( server, ca, CHAT_KIND_WHISPER, "", "Nobody", "hi" );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            const ChatFrames fb = drain_chat_frames( server, cb );
            check( fa.chats.size( ) == 1 && fa.chats[ 0 ].kind == CHAT_KIND_SYSTEM &&
                       fa.chats[ 0 ].text == "No player named Nobody",
                   "chat: unknown whisper target errors to sender" );
            check( fb.chats.empty( ), "chat: whisper error stays private" );
        }

        // Sanitization: control bytes stripped; a line that sanitizes to
        // empty is dropped entirely.
        send_chat( server, ca, CHAT_KIND_PLAYER, "", "", "a\tb\x01""c" );
        {
            const ChatFrames fb = drain_chat_frames( server, cb );
            check( fb.chats.size( ) == 1 && fb.chats[ 0 ].text == "abc",
                   "chat: non-printables stripped" );
        }
        drain_chat_frames( server, ca );
        send_chat( server, ca, CHAT_KIND_PLAYER, "", "", "\x01\x02\x03" );
        check( drain_chat_frames( server, cb ).chats.empty( ) && drain_chat_frames( server, ca ).chats.empty( ),
               "chat: sanitizes-to-empty dropped" );

        // Unnamed fd still chats as Player<fd>.
        send_chat( server, cc, CHAT_KIND_PLAYER, "", "", "anon here" );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            const std::string want = "Player" + std::to_string( cc.fd );
            check( fa.chats.size( ) == 1 && fa.chats[ 0 ].from == want,
                   "chat: unnamed fd falls back to Player<fd>" );
        }

        // Late joiner gets the name replay on connect.
        drain_chat_frames( server, cb );
        send_player_name( server, cc, "Carol" );
        drain_chat_frames( server, ca );
        drain_chat_frames( server, cb );
        drain_chat_frames( server, cc );
        SimClient cd = connect_client( server );
        {
            const ChatFrames fd = drain_chat_frames( server, cd );
            bool saw_alicia = false, saw_alice2 = false, saw_carol = false;
            for ( const auto &pair : fd.names ) {
                saw_alicia = saw_alicia || ( pair.first == ca.fd && pair.second == "Alicia" );
                saw_alice2 = saw_alice2 || ( pair.first == cb.fd && pair.second == "alice_2" );
                saw_carol = saw_carol || ( pair.first == cc.fd && pair.second == "Carol" );
            }
            check( saw_alicia && saw_alice2 && saw_carol,
                   "chat: late joiner receives existing names" );
        }

        // Disconnect announces the leave.
        disconnect_client( server, cb );
        {
            const ChatFrames fa = drain_chat_frames( server, ca );
            check( fa.chats.size( ) == 1 && fa.chats[ 0 ].kind == CHAT_KIND_SYSTEM &&
                       fa.chats[ 0 ].text == "alice_2 left the game",
                   "chat: disconnect announces left the game" );
        }
        disconnect_client( server, ca );
        disconnect_client( server, cc );
        disconnect_client( server, cd );
    }

    // === FramedSocket: pop_send_frame returns frames in send order, and
    // the batched sendmsg flush preserves order across partial writes.
    {
        FramedSocket q;
        q.adopt_manual( );
        q.send_message( NetMsgType::WORLD_TIME, 0, { 1, 2, 3 } );
        q.send_message( NetMsgType::BLOCK_UPDATE, 0, { 4 } );
        q.send_message( NetMsgType::WORLD_TIME, 0, { 5, 6 } );
        const NetMsgType want_types[ 3 ] = { NetMsgType::WORLD_TIME, NetMsgType::BLOCK_UPDATE, NetMsgType::WORLD_TIME };
        const size_t want_sizes[ 3 ] = { 3, 1, 2 };
        bool order_ok = true;
        for ( int i = 0; i < 3; i++ ) {
            std::optional<std::vector<uint8_t>> f = q.pop_send_frame( );
            order_ok = order_ok && f && f->size( ) == 18 + want_sizes[ i ] &&
                       static_cast<NetMsgType>( ( *f )[ 13 ] ) == want_types[ i ];
        }
        check( order_ok && !q.pop_send_frame( ),
               "framed: pop_send_frame preserves send order" );
    }
    {
        // A small send buffer forces flush() to return with frames still
        // queued; draining the peer + reflushing must deliver every frame
        // intact and in order (partial-write offset resume).
        const int N = 150;
        int sv[ 2 ] = { -1, -1 };
        check( socketpair( AF_UNIX, SOCK_STREAM, 0, sv ) == 0, "framed: socketpair" );
        int sndbuf = 2048;
        setsockopt( sv[ 0 ], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof( sndbuf ) );
        fcntl( sv[ 0 ], F_SETFL, O_NONBLOCK );
        fcntl( sv[ 1 ], F_SETFL, O_NONBLOCK );
        FramedSocket fs;
        fs.adopt( sv[ 0 ] );
        for ( int i = 0; i < N; i++ ) {
            std::vector<uint8_t> p( 200, 0 );
            memcpy( p.data( ), &i, sizeof( i ) ); // sequence tag
            fs.send_message( NetMsgType::CHUNK_DIFF_RESULT, 0, p );
        }
        FramedSocket reader;
        reader.adopt_manual( );
        std::vector<int> seen;
        uint8_t buf[ 65536 ];
        for ( int guard = 0; ( int )seen.size( ) < N && guard < 1000000; guard++ ) {
            fs.flush( );
            ssize_t n;
            while ( ( n = read( sv[ 1 ], buf, sizeof( buf ) ) ) > 0 ) {
                reader.push_recv_bytes( buf, ( size_t )n );
                while ( std::optional<std::vector<uint8_t>> payload = reader.parse_next_frame( ) ) {
                    int tag = -1;
                    if ( payload->size( ) >= 10 ) {
                        memcpy( &tag, payload->data( ) + 6, sizeof( tag ) );
                    }
                    seen.push_back( tag );
                }
            }
        }
        bool ok = ( int )seen.size( ) == N;
        for ( int i = 0; ok && i < N; i++ ) {
            ok = seen[ i ] == i;
        }
        check( ok, "framed: %d frames survive partial batched sends in order (got %zu)",
               N, seen.size( ) );
        check( !fs.had_error( ) && fs.outbound_frames( ) == 0,
               "framed: send queue drains fully" );
        close( sv[ 0 ] );
        close( sv[ 1 ] );
    }

    // === Protocol version mismatch: a validly framed message (correct
    // magic/CRC) carrying a foreign version byte must tear the connection
    // down — the client slot is marked disconnected and the server's end of
    // the socket is closed. ---
    {
        SimClient v = connect_client( server );
        drain_client( server, v );

        // Build a real frame, then patch the version byte and fix the CRC.
        FramedSocket fs;
        fs.adopt_manual( );
        fs.send_message( NetMsgType::PLAYER_LOCATION, 0, { } );
        std::vector<uint8_t> frame = *fs.pop_send_frame( );
        frame[ 12 ] = NET_PROTOCOL_VERSION + 1;
        const uint32_t crc = net_crc32( frame.data( ) + 8, frame.size( ) - 8 );
        frame[ 4 ] = static_cast<uint8_t>( crc );
        frame[ 5 ] = static_cast<uint8_t>( crc >> 8 );
        frame[ 6 ] = static_cast<uint8_t>( crc >> 16 );
        frame[ 7 ] = static_cast<uint8_t>( crc >> 24 );
        check( write( v.peer_fd, frame.data( ), frame.size( ) ) == static_cast<ssize_t>( frame.size( ) ),
               "mismatch: wrote %zu-byte foreign-version frame", frame.size( ) );

        server.test_handle_client_read( v.fd );
        check( server.test_client( v.fd ).connected == 0, "mismatch: server marks client disconnected" );
        char byte;
        check( recv( v.peer_fd, &byte, 1, 0 ) == 0, "mismatch: server closed its end of the socket" );
        close( v.peer_fd );
    }

    // === DiffCache: clean chunks replay stored CHUNK_DIFF_RESULT payloads
    // out of a byte-budgeted LRU; record_block invalidates the chunk and
    // its halo neighbours; dirty chunks never enter the cache. ===
    const glm::ivec3 dc1( 20, 0, 20 );
    const glm::ivec3 dc_edge( 21, 0, 20 );
    const glm::ivec3 dc1_origin = dc1 * CHUNK_SIZE_I;
    {
        // Standalone LRU mechanics: byte accounting stays under the
        // budget, eviction is LRU-first, oversized payloads never cache,
        // and the entry cap evicts the same way.
        DiffCache dc;
        const std::vector<uint8_t> p100( 100, 7 );
        const size_t budget3 = 3 * ( p100.size( ) + DiffCache::ENTRY_OVERHEAD );
        dc.set_limits( budget3, 64 );
        dc.insert( glm::ivec3( 0, 0, 0 ), p100 );
        dc.insert( glm::ivec3( 1, 0, 0 ), p100 );
        dc.insert( glm::ivec3( 2, 0, 0 ), p100 );
        check( dc.entries( ) == 3 && dc.bytes( ) == budget3,
               "diffcache: three payloads fill the budget exactly (bytes=%zu)", dc.bytes( ) );
        check( dc.find( glm::ivec3( 0, 0, 0 ) ) != nullptr, "diffcache: hit promotes to MRU" );
        dc.insert( glm::ivec3( 3, 0, 0 ), p100 ); // over budget: evicts LRU (1)
        check( dc.entries( ) == 3 && dc.bytes( ) <= budget3 && dc.evictions( ) == 1,
               "diffcache: insert over budget evicts once (bytes=%zu evictions=%zu)",
               dc.bytes( ), dc.evictions( ) );
        check( dc.find( glm::ivec3( 1, 0, 0 ) ) == nullptr &&
                   dc.find( glm::ivec3( 0, 0, 0 ) ) != nullptr &&
                   dc.find( glm::ivec3( 3, 0, 0 ) ) != nullptr,
               "diffcache: LRU evicted the unpromoted entry" );
        const size_t miss0 = dc.misses( ), hit0 = dc.hits( );
        (void)dc.find( glm::ivec3( 99, 0, 0 ) );
        check( dc.misses( ) == miss0 + 1 && dc.hits( ) == hit0,
               "diffcache: absent lookup counts a miss" );
        dc.insert( glm::ivec3( 9, 9, 9 ), std::vector<uint8_t>( budget3 + 1, 9 ) );
        check( dc.find( glm::ivec3( 9, 9, 9 ) ) == nullptr,
               "diffcache: oversized payload never cached" );
        dc.set_limits( budget3, 2 );
        check( dc.entries( ) == 2 && dc.bytes( ) <= budget3,
               "diffcache: shrinking the entry cap evicts down" );
        // Caching disabled entirely by a 0 budget.
        dc.set_limits( 0, 64 );
        check( dc.entries( ) == 0, "diffcache: zero budget empties the cache" );
        dc.insert( glm::ivec3( 5, 5, 5 ), p100 );
        check( dc.entries( ) == 0, "diffcache: zero budget never caches" );
    }
    {
        DiffCache &cache = server.test_logic( ).test_diff_cache( );
        SimClient c = connect_client( server );
        drain_client( server, c );
        const glm::ivec3 in_pos = dc1_origin + glm::ivec3( 10, 40, 10 );
        const glm::ivec3 edge_pos = dc1_origin + glm::ivec3( 61, 40, 10 );
        place( server, c, in_pos, stone );
        place( server, c, edge_pos, dirt ); // edge: mirrors into dc_edge too
        disconnect_client( server, c );     // flush: both chunks live only on disk

        c = connect_client( server );
        drain_client( server, c );
        const size_t miss0 = cache.misses( );
        const size_t hit0 = cache.hits( );
        const size_t ent0 = cache.entries( );

        // First requests miss and fill; repeats hit.
        send_diff_request( server, c, dc1, 1, 1, 1 );
        send_diff_request( server, c, dc_edge, 1, 1, 1 );
        DiffView first_view;
        collect_diffs( server, c, first_view );
        check( cache.misses( ) == miss0 + 2 && cache.entries( ) >= ent0 + 2,
               "diffcache: clean-chunk requests miss then fill (misses +%zu)",
               cache.misses( ) - miss0 );
        send_diff_request( server, c, dc1, 1, 1, 1 );
        send_diff_request( server, c, dc_edge, 1, 1, 1 );
        DiffView second_view;
        collect_diffs( server, c, second_view );
        check( cache.hits( ) == hit0 + 2,
               "diffcache: repeat requests hit (hits +%zu)", cache.hits( ) - hit0 );
        expect_diff( second_view, dc1, in_pos, stone );
        expect_diff( second_view, dc_edge, edge_pos, dirt );

        // An edge edit dirties dc1 and halo-neighbour dc_edge: both drop
        // out of the cache, and the next request serves the merged state
        // (old file diffs + new edit) from world_cache.
        const size_t entries_before_edit = cache.entries( );
        place( server, c, edge_pos, gold );
        drain_client( server, c );
        check( cache.entries( ) == entries_before_edit - 2,
               "diffcache: record_block invalidates chunk + halo neighbour (entries %zu -> %zu)",
               entries_before_edit, cache.entries( ) );
        send_diff_request( server, c, dc1, 1, 1, 1 );
        send_diff_request( server, c, dc_edge, 1, 1, 1 );
        DiffView merged;
        collect_diffs( server, c, merged );
        expect_diff( merged, dc1, in_pos, stone );
        expect_diff( merged, dc1, edge_pos, gold );
        expect_diff( merged, dc_edge, edge_pos, gold );
        check( cache.entries( ) == entries_before_edit - 2,
               "diffcache: dirty chunks are never cached" );
        disconnect_client( server, c );
        check( cache.entries( ) == 0,
               "diffcache: session flush clears cached payloads" );
    }

    // === Dirty-cell cap: a tiny cap forces the least-recently-edited
    // chunks to persist + evict early; their files hold the edits and
    // later requests still serve them (now via the DiffCache). ===
    {
        SimClient c = connect_client( server );
        drain_client( server, c );
        server.test_logic( ).test_set_dirty_cells_max( 3 );
        const glm::ivec3 cap0( 40, 0, 40 );
        const glm::ivec3 cap_pos = glm::ivec3( 10, 40, 10 );
        for ( int i = 0; i < 4; i++ ) {
            // One interior cell per chunk — 4 dirty cells over a cap of 3.
            const glm::ivec3 chunk = cap0 + glm::ivec3( i * 2, 0, 0 );
            place( server, c, chunk * CHUNK_SIZE_I + cap_pos, stone );
            drain_client( server, c );
        }
        check( server.test_logic( ).worldCacheDirtyCells( ) <= 3 &&
                   server.test_logic( ).cachedChunkCount( ) <= 3,
               "dirtycap: early save evicts down to the cap (cells=%zu chunks=%zu)",
               server.test_logic( ).worldCacheDirtyCells( ),
               server.test_logic( ).cachedChunkCount( ) );
        // The oldest edit (cap0) went out first — its file holds the edit.
        expect_disk_block( storage, cap0, cap0 * CHUNK_SIZE_I + cap_pos, stone );
        send_diff_request( server, c, cap0, 1, 1, 1 );
        DiffView cap_view;
        collect_diffs( server, c, cap_view );
        expect_diff( cap_view, cap0, cap0 * CHUNK_SIZE_I + cap_pos, stone );
        server.test_logic( ).test_set_dirty_cells_max( 200000 );
        disconnect_client( server, c );
    }

    // === Nearest-first order: a 3x1x3 request box around the player's
    // chunk answers the centre chunk's frame before the corner's (scan
    // order would emit the corner — the box min — first). ===
    {
        const glm::ivec3 center( 50, 0, 50 );
        const glm::ivec3 corner = center + glm::ivec3( -1, 0, -1 );
        SimClient c = connect_client( server );
        drain_client( server, c );
        place( server, c, corner * CHUNK_SIZE_I + glm::ivec3( 10, 40, 10 ), sand );
        place( server, c, center * CHUNK_SIZE_I + glm::ivec3( 10, 40, 10 ), gold );
        disconnect_client( server, c ); // flush so both serve from the cache

        c = connect_client( server );
        drain_client( server, c );
        // Player stands inside the centre chunk.
        const glm::vec3 pp = glm::vec3( center * CHUNK_SIZE_I ) + glm::vec3( 10.0f, 50.0f, 10.0f );
        send_player_location( server, c, pp.x, pp.y, pp.z );
        drain_client( server, c );
        send_diff_request( server, c, corner, 3, 1, 3 );
        glm::ivec3 order[ 2 ]{ glm::ivec3( -999 ), glm::ivec3( -999 ) };
        int diff_frames = 0;
        for ( const ReceivedFrame &f : drain_client( server, c ) ) {
            if ( f.type != NetMsgType::CHUNK_DIFF_RESULT ) {
                continue;
            }
            PacketReader r( f.payload.data( ), f.payload.size( ) );
            NetChunkDiffResultPayload diff;
            if ( net_deserialize_chunk_diff_result( r, diff ) && diff_frames < 2 ) {
                order[ diff_frames ] = glm::ivec3( diff.chunk_x, diff.chunk_y, diff.chunk_z );
            }
            diff_frames++;
        }
        check( diff_frames == 2 && order[ 0 ] == center && order[ 1 ] == corner,
               "nearest: player-centred request serves centre first (frames=%d first=%d,%d,%d)",
               diff_frames, order[ 0 ].x, order[ 0 ].y, order[ 0 ].z );
        disconnect_client( server, c );
    }

    // === Env caps: parsed once at init; with the variables unset the
    // documented defaults apply (16 MB, 65536 entries, 200K dirty cells).
    {
        const DiffCache &cache = server.test_logic( ).test_diff_cache( );
        check( cache.byte_budget( ) == 16u * 1024 * 1024 &&
                   cache.max_entries( ) == 65536 &&
                   server.test_logic( ).dirtyCellsMax( ) == 200000,
               "env: diff cache + dirty-cap defaults when vars unset (budget=%zu max=%zu cells=%zu)",
               cache.byte_budget( ), cache.max_entries( ), server.test_logic( ).dirtyCellsMax( ) );
    }

    server.cleanup( );

    std::filesystem::remove_all( world_dir( ) );

    if ( failures ) {
        st_print( "FAILED: %d check(s)", failures );
        return 1;
    }
    st_print( "All checks passed" );
    return 0;
}
