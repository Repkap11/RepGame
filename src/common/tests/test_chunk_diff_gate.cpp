// Diff-gated chunk reveal (docs/multiplayer.md): in multiplayer a generated
// chunk must stay invisible to every reader until its covering
// CHUNK_DIFF_REQUEST's DONE lands — ChunkLoader defers the whole finalize as
// CHUNK_SETTLE_WAIT, then settle_scan re-runs it once the diff_boxes_done
// watermark passes the stamped seq. All GL work in the finalize path is
// guarded out here the same way test_async_remesh does: the chunk reports
// gl_initialized, has no mesh instances, and should_render stays 0, so
// program_terrain/light_ensure_texture touch no GL objects.
#include "test_util.hpp"

#include <climits>

int test_chunk_diff_gate( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    static const glm::ivec3 cp0( 0, 0, 0 );
    const int slot0 = chunk_slot_from_pos( cp0 );

    // ---- Settle predicate matrix ----
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_done = 3;
        cl.slot_diff_req[ slot0 ] = cp0;
        cl.slot_diff_seq[ slot0 ] = 4;
        check_bool( "predicate: outstanding seq unsettles", !cl.chunk_settled( slot0, gs.multiplayer ) );
        gs.multiplayer.diff_boxes_done = 4;
        check_bool( "predicate: DONE reaching seq settles", cl.chunk_settled( slot0, gs.multiplayer ) );
        gs.multiplayer.diff_boxes_done = 0;
        cl.slot_diff_req[ slot0 ] = glm::ivec3( 9, 9, 9 );
        check_bool( "predicate: uncovered position unsettles", !cl.chunk_settled( slot0, gs.multiplayer ) );
        gs.multiplayer.active = false;
        check_bool( "predicate: offline always settles", cl.chunk_settled( slot0, gs.multiplayer ) );
        gs.multiplayer.connect_pending = true;
        check_bool( "predicate: connecting unsettles", !cl.chunk_settled( slot0, gs.multiplayer ) );
        gs.multiplayer.connect_pending = false;
        gs.multiplayer.active = true;
        cl.diff_gate_override = 0;
        check_bool( "predicate: gate override off settles", cl.chunk_settled( slot0, gs.multiplayer ) );
        cl.diff_gate_override = -1;
        gs.multiplayer.active = false;
    }

    // ---- Defer: outstanding covering request holds the whole finalize ----
    // The origin slot arrives as CHUNK_LOADING with terrain cells; the stamp
    // says request 4 covers it but only requests <= 3 are DONE.
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ slot0 ];
        chunk.load_state = CHUNK_LOADING;
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_sent = 4;
        gs.multiplayer.diff_boxes_done = 3;
        cl.slot_diff_req[ slot0 ] = cp0;
        cl.slot_diff_seq[ slot0 ] = 4;
        cl.finalize_loaded_chunk( chunk, gs.multiplayer, cp0 );
        check_bool( "defer: slot is CHUNK_SETTLE_WAIT, not live",
                    chunk.load_state == CHUNK_SETTLE_WAIT && !chunk.is_live( ) );
        check_bool( "defer: block reads see LAST_BLOCK_ID",
                    gs.world.get_loaded_block( glm::ivec3( 1, 1, 1 ) ).id == LAST_BLOCK_ID );
        check_bool( "defer: listed for the settle scan",
                    cl.settle_wait_list.size( ) == 1 && cl.settle_wait_list[ 0 ].slot == slot0 );
        check_bool( "defer: nothing adopted yet", cl.just_finalized.empty( ) );
        check_bool( "defer: not drawable", !chunk.should_render );
        // The scan holds it while the watermark is still short.
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "scan: unsettled stays deferred",
                    chunk.load_state == CHUNK_SETTLE_WAIT && cl.settle_wait_list.size( ) == 1 );

        // ---- DONE advances: reveal finalizes and drains the pending diff ----
        NetChunkDiffResultPayload diff;
        diff.chunk_x = 0;
        diff.chunk_y = 0;
        diff.chunk_z = 0;
        NetChunkDiffEntry entry;
        entry.blocks_index = static_cast<uint32_t>( Chunk::get_index_from_coords( 1, 2, 3 ) );
        entry.blockState = { STONE, 0, 0, STONE, 0 };
        diff.diffs.push_back( entry );
        diff.num_diffs = 1;
        gs.multiplayer.queue_pending_diff( diff );
        // No GL context here: gl_initialized skips ensure_gl_init, and a
        // nonzero fake light.texture early-outs light_ensure_texture (the
        // diff write marks should_render=1, which would otherwise reach
        // glGenTextures). program_terrain is safe — num_instances stays 0.
        chunk.gl_initialized = 1;
        chunk.light.texture = 1;
        gs.multiplayer.diff_boxes_done = 4;
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "reveal: chunk goes live", chunk.load_state == CHUNK_LIVE && chunk.is_live( ) );
        check_bool( "reveal: wait list drained", cl.settle_wait_list.empty( ) );
        check_bool( "reveal: adoption queued", cl.just_finalized.size( ) == 1 && cl.just_finalized[ 0 ] == &chunk );
        failures += check_id( gs.world, "reveal: pending diff applied", glm::ivec3( 1, 2, 3 ), STONE );
        gs.multiplayer.active = false;
    }

    // ---- Coverage backstop: a landing with no stamp re-asks + defers ----
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ slot0 ];
        chunk.load_state = CHUNK_LOADING;
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_sent = 4;
        gs.multiplayer.diff_boxes_done = 3;
        cl.slot_diff_req[ slot0 ] = glm::ivec3( 9, 9, 9 ); // some other position
        cl.slot_diff_seq[ slot0 ] = 2;
        cl.finalize_loaded_chunk( chunk, gs.multiplayer, cp0 );
        check_bool( "backstop: request fired + restamped",
                    cl.slot_diff_req[ slot0 ] == cp0 && cl.slot_diff_seq[ slot0 ] == 5 &&
                        gs.multiplayer.diff_boxes_sent == 5 );
        check_bool( "backstop: deferred on the new seq", chunk.load_state == CHUNK_SETTLE_WAIT );
        gs.multiplayer.active = false;
    }

    // ---- Stall fallback: reask once past the window, force past the cap ----
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ slot0 ];
        chunk.load_state = CHUNK_LOADING;
        gs.multiplayer.active = true;
        gs.multiplayer.diff_boxes_sent = 4;
        gs.multiplayer.diff_boxes_done = 3;
        cl.slot_diff_req[ slot0 ] = cp0;
        cl.slot_diff_seq[ slot0 ] = 4;
        cl.finalize_loaded_chunk( chunk, gs.multiplayer, cp0 );
        // Age the deferral past the reask window: the scan re-issues the
        // covering request once and restamps the seq.
        cl.settle_wait_list[ 0 ].defer_us -= 4 * 1000000;
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "reask: covering request re-sent",
                    gs.multiplayer.diff_boxes_sent == 5 && cl.slot_diff_seq[ slot0 ] == 5 &&
                        cl.settle_wait_list[ 0 ].reasked );
        check_bool( "reask: still deferred", chunk.load_state == CHUNK_SETTLE_WAIT );
        // The cap force-reveals even with the watermark still short.
        cl.settle_wait_list[ 0 ].defer_us -= 12 * 1000000;
        chunk.gl_initialized = 1;
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "force: cap reveals despite outstanding seq",
                    chunk.load_state == CHUNK_LIVE && cl.settle_wait_list.empty( ) );
        gs.multiplayer.active = false;
    }

    // ---- Connect window: terrain landing before the socket is up waits ----
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ slot0 ];
        chunk.load_state = CHUNK_LOADING;
        gs.multiplayer.connect_pending = true;
        cl.slot_diff_req[ slot0 ] = glm::ivec3( 9, 9, 9 );
        cl.finalize_loaded_chunk( chunk, gs.multiplayer, cp0 );
        check_bool( "connecting: finalize defers", chunk.load_state == CHUNK_SETTLE_WAIT && cl.settle_wait_list.size( ) == 1 );
        // Long connect: the wait clock doesn't run, so no force-reveal.
        cl.settle_wait_list[ 0 ].defer_us -= 20 * 1000000;
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "connecting: scan holds + restarts clock",
                    chunk.load_state == CHUNK_SETTLE_WAIT && cl.settle_wait_list.size( ) == 1 &&
                        cl.settle_wait_list[ 0 ].defer_us > 0 );
        // Connected: the uncovered slot gets its own request, then settles.
        gs.multiplayer.connect_pending = false;
        gs.multiplayer.active = true;
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "connected: uncovered slot requested",
                    cl.slot_diff_req[ slot0 ] == cp0 && gs.multiplayer.diff_boxes_sent == 1 &&
                        cl.slot_diff_seq[ slot0 ] == 1 && chunk.load_state == CHUNK_SETTLE_WAIT );
        chunk.gl_initialized = 1;
        gs.multiplayer.diff_boxes_done = 1;
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "connected: DONE reveals", chunk.load_state == CHUNK_LIVE && cl.settle_wait_list.empty( ) );
        gs.multiplayer.active = false;
    }

    // ---- Failed connect: deferred slots settle immediately ----
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ slot0 ];
        chunk.load_state = CHUNK_LOADING;
        gs.multiplayer.connect_pending = true;
        cl.finalize_loaded_chunk( chunk, gs.multiplayer, cp0 );
        gs.multiplayer.connect_pending = false;
        chunk.gl_initialized = 1;
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "connect failed: reveals", chunk.load_state == CHUNK_LIVE && cl.settle_wait_list.empty( ) );
    }

    // ---- Stale entries: a slot that moved on drops out of the list ----
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ slot0 ];
        chunk.load_state = CHUNK_SETTLE_WAIT;
        cl.settle_wait_list.push_back( { slot0, 0, false } );
        chunk.load_state = CHUNK_PARKED; // window moved off; park owns the slot now
        cl.settle_scan( gs.multiplayer, cp0, LLONG_MAX );
        check_bool( "stale: parked slot drops its entry", cl.settle_wait_list.empty( ) );
    }

    // ---- Single player / gate off: finalize never defers ----
    {
        RepGameState &gs = *new RepGameState( );
        test_setup_world( gs.world );
        ChunkLoader &cl = gs.world.chunkLoader;
        Chunk &chunk = cl.chunkArray[ slot0 ];
        chunk.load_state = CHUNK_LOADING;
        chunk.gl_initialized = 1;
        cl.slot_diff_req[ slot0 ] = glm::ivec3( 9, 9, 9 );
        cl.finalize_loaded_chunk( chunk, gs.multiplayer, cp0 ); // mp inactive
        check_bool( "offline: finalizes immediately", chunk.load_state == CHUNK_LIVE );
        Chunk &chunk2 = cl.chunkArray[ chunk_slot_from_pos( glm::ivec3( 1, 0, 0 ) ) ];
        chunk2.chunk_pos = glm::ivec3( 1, 0, 0 );
        chunk2.load_state = CHUNK_LOADING;
        chunk2.gl_initialized = 1;
        gs.multiplayer.active = true;
        cl.diff_gate_override = 0;
        cl.finalize_loaded_chunk( chunk2, gs.multiplayer, cp0 );
        check_bool( "gate off: active mp finalizes immediately",
                    chunk2.load_state == CHUNK_LIVE && cl.settle_wait_list.empty( ) );
        cl.diff_gate_override = -1;
        gs.multiplayer.active = false;
    }

    return failures;
}
