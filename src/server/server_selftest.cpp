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
#include "common/net/packet.hpp"
#include "common/mob_types.hpp"
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

// One placed edit and its expected diff footprint: the owning chunk always
// records it; each axis-aligned neighbor whose interior edge it touches gets a
// border-cell mirror (the server mirrors boundary writes into every affected
// chunk's record).
struct ExpectedEdit {
    glm::ivec3 pos;
    BlockState state;
};

struct SimWorld {
    std::vector<ExpectedEdit> edits;
};

void place( Server &server, SimClient &who, SimWorld &w, const glm::ivec3 &pos, const BlockState &state ) {
    send_block_update( server, who, pos, state );
    w.edits.push_back( { pos, state } );
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

void send_entity_action( Server &server, SimClient &c, NetEntityId id ) {
    NetEntityActionPayload p{};
    p.id = id;
    p.action = NET_ACTION_HIT;
    p.damage = 4.0f;
    PacketWriter w;
    net_serialize_entity_action( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::ENTITY_ACTION, 0, w.buf( ) );
}

void send_chunk_populated( Server &server, SimClient &c, const glm::ivec3 &chunk ) {
    NetChunkPopulatedPayload p{ chunk.x, chunk.y, chunk.z };
    PacketWriter w;
    net_serialize_chunk_populated( w, p );
    server.test_logic( ).on_client_message( server, c.fd, NetMsgType::CHUNK_POPULATED, 0, w.buf( ) );
}

// Cross-check the raw chunk files on disk (what survives a server restart).
void expect_disk_block( MapStorage &storage, const glm::ivec3 &chunk_pos, const glm::ivec3 &pos, const BlockState &want ) {
    BlockState *blocks = static_cast<BlockState *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) ) );
    int dirty = 0;
    const int index = index_in_chunk( chunk_pos, pos );
    if ( storage.load_blocks( chunk_pos, blocks, dirty, true ) ) {
        check( BlockStates_equal( blocks[ index ], want ),
               "file chunk (%d,%d,%d) pos (%d,%d,%d) id=%d want=%d",
               chunk_pos.x, chunk_pos.y, chunk_pos.z, pos.x, pos.y, pos.z,
               blocks[ index ].id, want.id );
    } else {
        check( false, "no chunk file for (%d,%d,%d) holding pos (%d,%d,%d)",
               chunk_pos.x, chunk_pos.y, chunk_pos.z, pos.x, pos.y, pos.z );
    }
    free( blocks );
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

    SimWorld world;

    // --- Session 1: two players edit the world ---
    SimClient a = connect_client( server );
    SimClient b = connect_client( server );
    drain_client( server, a );
    drain_client( server, b );

    // Interior edit in chunk (0,0,0).
    place( server, a, world, glm::ivec3( 10, 40, 10 ), stone );
    expect_forwarded( server, b, a.fd, glm::ivec3( 10, 40, 10 ), stone );
    check( server.test_logic( ).cachedChunkCount( ) > 0,
           "edit populates the chunk cache (got %zu)", server.test_logic( ).cachedChunkCount( ) );
    // Edge of chunk (0,0,0) -> mirrored into chunk (1,0,0)'s border.
    place( server, b, world, glm::ivec3( 61, 40, 10 ), dirt );
    // Interior of chunk (1,0,0), touching chunk 0 -> mirrored into (0,0,0).
    place( server, a, world, glm::ivec3( 62, 40, 10 ), gold );
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
    place( server, b, world, glm::ivec3( 10, 40, 10 ), BLOCK_STATE_AIR );
    // Chunk (1,0,0) was evicted too; this owns-in-1 edit mirrors into chunk 2.
    place( server, a, world, glm::ivec3( 123, 40, 10 ), sand );

    disconnect_client( server, a );
    disconnect_client( server, b );

    // --- Session 3: a client that deleted its local world rejoins and
    // requests diffs; every edit ever sent must come back. ---
    a = connect_client( server );
    drain_client( server, a );
    send_diff_request( server, a, glm::ivec3( -1, -1, -1 ), 5, 3, 3 );

    DiffView view;
    collect_diffs( server, a, view );
    // Diff reads must not populate the cache: every requested chunk was
    // already flushed, so the cache stays empty after serving them.
    check( server.test_logic( ).cachedChunkCount( ) == 0,
           "diff requests leave the chunk cache empty (got %zu)", server.test_logic( ).cachedChunkCount( ) );
    // Most of the 5x3x3 box has no save file; those positions are now
    // remembered as known-empty.
    check( server.test_logic( ).knownEmptyChunkCount( ) > 0,
           "diff requests record known-empty chunks (got %zu)", server.test_logic( ).knownEmptyChunkCount( ) );

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

        // Write a legacy-format chunk file directly (persist_dirty_blocks
        // encodes raw states, no sanitizing).
        BlockState *legacy = static_cast<BlockState *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) ) );
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
            legacy[ i ] = BlockState{ static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0, 0,
                                      static_cast<BlockID>( LEGACY_EMPTY_BLOCK_ID ), 0 };
        }
        legacy[ leg_index ] = stone;
        storage.persist_dirty_blocks( leg, legacy );
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

        // The disconnect flush rewrote the file; the slots that were the
        // legacy sentinel must now load as the empty marker, not AIR.
        BlockState *blocks = static_cast<BlockState *>( malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) ) );
        int dirty = 0;
        if ( storage.load_blocks( leg, blocks, dirty, true ) ) {
            check( blocks[ leg_index ].id == STONE,
                   "rewritten legacy chunk keeps stone at index %d (id=%d)", leg_index, blocks[ leg_index ].id );
            check( blocks[ other_index ].id == LAST_BLOCK_ID,
                   "rewritten legacy chunk index %d loads empty marker (id=%d)", other_index, blocks[ other_index ].id );
        } else {
            check( false, "no chunk file for legacy chunk (%d,%d,%d)", leg.x, leg.y, leg.z );
        }
        free( blocks );
    }

    // --- Known-empty chunks: a diff request for a chunk with no save file
    // is remembered, a second request skips the disk, and an edit
    // invalidates the mark. ---
    const glm::ivec3 ke( 7, 0, 7 );
    {
        const glm::ivec3 ke_pos = ke * CHUNK_SIZE_I + glm::ivec3( 10, 40, 10 );
        SimClient c = connect_client( server );
        drain_client( server, c );

        const size_t before = server.test_logic( ).knownEmptyChunkCount( );
        send_diff_request( server, c, ke, 1, 1, 1 );
        DiffView ke_view;
        collect_diffs( server, c, ke_view );
        check( ke_view.find( ke ) == ke_view.end( ),
               "unseen chunk (%d,%d,%d) emits no diff", ke.x, ke.y, ke.z );
        check( server.test_logic( ).knownEmptyChunkCount( ) == before + 1,
               "unseen chunk recorded known-empty (got %zu, want %zu)",
               server.test_logic( ).knownEmptyChunkCount( ), before + 1 );

        send_diff_request( server, c, ke, 1, 1, 1 );
        collect_diffs( server, c, ke_view );
        check( ke_view.find( ke ) == ke_view.end( ),
               "second request for known-empty chunk emits no diff" );
        check( server.test_logic( ).knownEmptyChunkCount( ) == before + 1,
               "second request for known-empty chunk adds nothing (got %zu)",
               server.test_logic( ).knownEmptyChunkCount( ) );

        // An edit invalidates the known-empty mark.
        place( server, c, world, ke_pos, stone );
        check( server.test_logic( ).knownEmptyChunkCount( ) == before,
               "edit invalidates known-empty mark (got %zu, want %zu)",
               server.test_logic( ).knownEmptyChunkCount( ), before );

        // Flush persists the edit; a fresh client gets it back by diff.
        disconnect_client( server, c );
        c = connect_client( server );
        drain_client( server, c );
        send_diff_request( server, c, ke, 1, 1, 1 );
        ke_view.clear( );
        collect_diffs( server, c, ke_view );
        expect_diff( ke_view, ke, ke_pos, stone );
        disconnect_client( server, c );
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

        const glm::ivec3 c0( 0, 0, 0 ), c1( 1, 0, 0 ), c2( 2, 0, 0 ), c_n3( -3, 0, 0 );
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

        // ENTITY_SPAWN relays to the other viewers only.
        const NetEntityId item1 = ( static_cast<NetEntityId>( sess_a ) << 32 ) | 1;
        send_entity_spawn( server, ea, item_record( item1, DIRT, 5, 70.0, 30.0, 10.0 ) ); // chunk 1
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ea );
            const std::vector<ReceivedFrame> fb = drain_client( server, eb );
            check( frames_of_type( fa, NetMsgType::ENTITY_SPAWN ).empty( ), "entity: spawn not echoed to sender" );
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
        const glm::ivec3 c_n2( -2, 0, 0 ), c2( 2, 0, 0 );
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

        const glm::ivec3 c2( 2, 0, 0 ), c4( 4, 0, 0 );
        const ServerEntities &se3 = server.test_logic( ).test_entities( );
        check( se3.owner_of( c2 ) == bb.fd, "body: chunk 2 owned by B" );
        check( se3.owner_of( c4 ) == bb.fd, "body: chunk 4 owned by B (hysteresis over C)" );

        // ba spawns a primed-TNT body in bb-owned chunk 2: accepted, relayed
        // to the other viewers (bb), never echoed to the sim.
        const NetEntityId body1 = ( static_cast<NetEntityId>( sess_ba ) << 32 ) | 0xB1;
        send_entity_spawn( server, ba, body_record( body1, TEST_BODY_TNT, TNT, 40, 130.0, 30.0, 10.0 ) );
        check( se3.entity_count_of( c2 ) == 1, "body: causer spawn into foreign chunk accepted" );
        {
            const std::vector<ReceivedFrame> fa = drain_client( server, ba );
            const std::vector<ReceivedFrame> fb = drain_client( server, bb );
            check( frames_of_type( fa, NetMsgType::ENTITY_SPAWN ).empty( ), "body: spawn not echoed to sim" );
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

    server.cleanup( );

    std::filesystem::remove_all( world_dir( ) );

    if ( failures ) {
        st_print( "FAILED: %d check(s)", failures );
        return 1;
    }
    st_print( "All checks passed" );
    return 0;
}
