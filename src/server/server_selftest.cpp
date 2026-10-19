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
    server.cleanup( );

    std::filesystem::remove_all( world_dir( ) );

    if ( failures ) {
        st_print( "FAILED: %d check(s)", failures );
        return 1;
    }
    st_print( "All checks passed" );
    return 0;
}
