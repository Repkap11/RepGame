// Chunk edit-file format tests: v1 codec, legacy migration, the
// load_terrain edits overlay, and the multiplayer file-IO gate.
#include "test_util.hpp"

#include <algorithm>
#include <sys/stat.h>

int test_chunk_edits( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    setenv( "REPGAME_MOB_TEST", "1", 1 ); // skip the mob roll — irrelevant here
    int failures = 0;
    auto check_bool = [ & ]( const char *name, bool cond ) {
        pr_test( "  %-55s %s", name, cond ? "PASS" : "FAIL" );
        if ( !cond ) {
            failures++;
        }
    };
    const BlockState TNT_EDIT{ TNT, 0, 0, TNT, 0 };
    const BlockState GOLD_EDIT{ GOLD_BLOCK, 0, 0, GOLD_BLOCK, 0 };

    // A generated chunk: fresh heap Chunk (value-init zeroes the POD fields)
    // run through the real load_terrain. free_chunk is the CPU-side teardown
    // — nothing here made GL objects.
    auto gen_chunk = []( MapStorage &ms, const glm::ivec3 &pos ) {
        Chunk *chunk = new Chunk( );
        chunk->chunk_pos = pos;
        chunk->chunk_mod = pos;
        chunk->load_terrain( ms );
        return chunk;
    };
    auto free_chunk = []( Chunk *chunk ) {
        chunk->unprogram_terrain( ); // frees layers[].populated_blocks
        free( chunk->layers );
        chunk->layers = nullptr;
        chunk->free_block_storage( );
        free( chunk->edited ); // free_block_storage leaves it — see chunk.cpp
        chunk_free_light( chunk->light.volume );
        chunk_free_light_columns( chunk->light.columns );
        chunk_free_air_mask( chunk->light.air_mask );
        chunk_free_air_mask( chunk->light.shade_mask );
        delete chunk;
    };
    auto snapshot_states = []( const Chunk &chunk ) {
        std::vector<BlockState> out( CHUNK_BLOCK_SIZE, BLOCK_STATE_AIR );
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            out[ i ] = chunk.cells != nullptr ? chunk.state_at( i ) : BLOCK_STATE_AIR;
        }
        return out;
    };
    auto count_edit_bits = []( const Chunk &chunk ) {
        int n = 0;
        if ( chunk.edited != nullptr ) {
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                n += ( chunk.edited[ i >> 3 ] >> ( i & 7 ) ) & 1;
            }
        }
        return n;
    };
    auto count_edit_cells = []( const ChunkEdits &edits ) {
        int n = 0;
        for ( const ChunkEditRun &run : edits.runs ) {
            n += run.count;
        }
        return n;
    };

    ScopedTempHome temp_home( "/tmp/repgame_test_chunk_edits_home" );

    // --- 1. codec ------------------------------------------------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsCodec" );
        const glm::ivec3 pos( 3, 0, 3 );
        std::vector<ChunkEditRun> runs{
            { 10u, 2u, TNT_EDIT },
            { 100u, 5u, GOLD_EDIT },
        };
        storage.persist_chunk_edits( pos, runs );
        ChunkEdits back;
        check_bool( "v1 round trip decodes", storage.load_chunk_edits( pos, back ) == 1 );
        check_bool( "v1 round trip runs", back.runs.size( ) == 2 && back.runs[ 0 ].start == 10 && back.runs[ 0 ].count == 2 &&
                                            back.runs[ 1 ].start == 100 && back.runs[ 1 ].count == 5 );
        check_bool( "v1 round trip states", back.runs.size( ) == 2 &&
                                                BlockStates_equal( back.runs[ 0 ].state, TNT_EDIT ) &&
                                                BlockStates_equal( back.runs[ 1 ].state, GOLD_EDIT ) );
        check_bool( "v1 file needs no rewrite", !back.needs_rewrite && !back.legacy_snapshot );

        // Empty runs remove the file.
        storage.persist_chunk_edits( pos, { } );
        check_bool( "empty runs remove file", !storage.check_if_chunk_exists( pos ) );

        // Hand-crafted files with foreign record_size: shared-prefix decode.
        char file_name[ CHUNK_NAME_MAX_LENGTH ];
        snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, storage.get_map_name( ), pos.x, pos.y, pos.z );
        const uint32_t magic = 0x4B434752u, version = 1u;
        {
            // Larger record: BlockState + 4 trailing bytes.
            const uint32_t record_size = sizeof( BlockState ) + 4;
            FILE *f = fopen( file_name, "wb" );
            uint32_t run_count = 1, start = 7, count = 3;
            char record[ sizeof( BlockState ) + 4 ];
            memset( record, 0xAA, sizeof( record ) );
            memcpy( record, &TNT_EDIT, sizeof( BlockState ) );
            fwrite( &magic, 4, 1, f );
            fwrite( &version, 4, 1, f );
            fwrite( &record_size, 4, 1, f );
            fwrite( &run_count, 4, 1, f );
            fwrite( &start, 4, 1, f );
            fwrite( &count, 4, 1, f );
            fwrite( record, record_size, 1, f );
            fclose( f );
            ChunkEdits bigger;
            check_bool( "larger record_size decodes", storage.load_chunk_edits( pos, bigger ) == 1 &&
                                                          bigger.runs.size( ) == 1 && bigger.runs[ 0 ].start == 7 &&
                                                          BlockStates_equal( bigger.runs[ 0 ].state, TNT_EDIT ) );
            check_bool( "larger record_size flags rewrite", bigger.needs_rewrite );
        }
        {
            // Smaller record: first 8 bytes of BlockState.
            const uint32_t record_size = 8;
            FILE *f = fopen( file_name, "wb" );
            uint32_t run_count = 1, start = 11, count = 1;
            fwrite( &magic, 4, 1, f );
            fwrite( &version, 4, 1, f );
            fwrite( &record_size, 4, 1, f );
            fwrite( &run_count, 4, 1, f );
            fwrite( &start, 4, 1, f );
            fwrite( &count, 4, 1, f );
            fwrite( &TNT_EDIT, record_size, 1, f );
            fclose( f );
            ChunkEdits smaller;
            check_bool( "smaller record_size decodes", storage.load_chunk_edits( pos, smaller ) == 1 &&
                                                           smaller.runs.size( ) == 1 && smaller.runs[ 0 ].start == 11 );
            check_bool( "smaller record_size flags rewrite", smaller.needs_rewrite );
        }
        {
            // Corrupt/short file: magic then truncation.
            FILE *f = fopen( file_name, "wb" );
            fwrite( &magic, 4, 1, f );
            fclose( f );
            ChunkEdits bad;
            check_bool( "short file rejected", storage.load_chunk_edits( pos, bad ) != 1 );
            FILE *f2 = fopen( file_name, "wb" );
            fputc( 'x', f2 );
            fclose( f2 );
            ChunkEdits bad2;
            check_bool( "1-byte file rejected", storage.load_chunk_edits( pos, bad2 ) != 1 );
            remove( file_name );
        }
        check_bool( "missing file returns 0", storage.load_chunk_edits( pos, back ) == 0 );
    }

    // --- 2. legacy server diff file (RLE with holes) --------------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsLegacyDiff" );
        const glm::ivec3 pos( 4, 0, 4 );
        std::vector<BlockState> cells( CHUNK_BLOCK_SIZE, BLOCK_STATE_LAST_BLOCK_ID );
        cells[ 10 ] = TNT_EDIT;
        cells[ 11 ] = TNT_EDIT;
        cells[ 50 ] = GOLD_EDIT;
        storage.persist_chunk_legacy( pos, cells.data( ) );
        ChunkEdits edits;
        check_bool( "legacy diff decodes", storage.load_chunk_edits( pos, edits ) == 1 );
        check_bool( "legacy diff runs = non-hole cells",
                    edits.runs.size( ) == 2 && edits.runs[ 0 ].start == 10 && edits.runs[ 0 ].count == 2 &&
                        edits.runs[ 1 ].start == 50 && edits.runs[ 1 ].count == 1 );
        check_bool( "legacy diff not a snapshot", !edits.legacy_snapshot );
        check_bool( "legacy diff flagged for rewrite", edits.needs_rewrite );
    }

    // --- 3. legacy client full snapshot migrates to sparse edits --------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsLegacySnap" );
        const glm::ivec3 pos( 5, 0, 5 );
        // Generate the chunk pristine (no file yet), then write a legacy
        // snapshot of those natural states with N cells changed.
        Chunk *pristine = gen_chunk( storage, pos );
        std::vector<BlockState> snap = snapshot_states( *pristine );
        int picked[ 3 ] = { -1, -1, -1 };
        int n = 0;
        for ( int i = 0; i < CHUNK_BLOCK_SIZE && n < 3; i++ ) {
            // Interior cells only, and only ones whose natural state differs
            // from the marker — otherwise the edit is invisible.
            int cx, cy, cz;
            Chunk::get_coords_from_index( i, cx, cy, cz );
            if ( cx < 0 || cx >= CHUNK_SIZE_X || cy < 0 || cy >= CHUNK_SIZE_Y || cz < 0 || cz >= CHUNK_SIZE_Z ) {
                continue;
            }
            if ( !BlockStates_equal( snap[ i ], TNT_EDIT ) ) {
                snap[ i ] = TNT_EDIT;
                picked[ n++ ] = i;
            }
        }
        free_chunk( pristine );
        storage.persist_chunk_legacy( pos, snap.data( ) );

        Chunk *migrated = gen_chunk( storage, pos );
        // Mapgen has a small run-to-run drift (GPU path): a stored cell that
        // regenerated differently counts as an extra "edit". The picked
        // cells must all be marked, and the total stays near N.
        const int edits_now = count_edit_bits( *migrated );
        check_bool( "legacy snapshot yields ~N edits", edits_now >= n && edits_now <= n + 64 );
        check_bool( "legacy snapshot marks chunk dirty", migrated->dirty == 1 );
        bool states_ok = n > 0;
        for ( int i = 0; i < n; i++ ) {
            states_ok = states_ok && migrated->is_edited( picked[ i ] ) &&
                        BlockStates_equal( migrated->state_at( picked[ i ] ), TNT_EDIT );
        }
        check_bool( "legacy snapshot edited cells match", states_ok );
        // Persist migrates the file to v1 holding exactly the edited bits.
        migrated->persist( storage );
        ChunkEdits v1;
        check_bool( "migration writes v1", storage.load_chunk_edits( pos, v1 ) == 1 && !v1.needs_rewrite && !v1.legacy_snapshot );
        check_bool( "migration v1 has N cells", count_edit_cells( v1 ) == edits_now );
        free_chunk( migrated );

        // A snapshot identical to natural removes the file on persist.
        // Remove the migrated v1 file first so the snapshot is pure
        // natural terrain.
        storage.persist_chunk_edits( pos, { } );
        Chunk *again = gen_chunk( storage, pos );
        std::vector<BlockState> identical = snapshot_states( *again );
        free_chunk( again );
        storage.persist_chunk_legacy( pos, identical.data( ) );
        Chunk *same = gen_chunk( storage, pos );
        check_bool( "identical snapshot yields no edits", !same->has_edits( ) );
        same->persist( storage );
        check_bool( "identical snapshot removes file", !storage.check_if_chunk_exists( pos ) );
        free_chunk( same );
    }

    // --- 4. overlay: interior, halo and sky edits ---------------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsOverlay" );
        const glm::ivec3 pos( 6, 0, 6 );
        Chunk *pristine = gen_chunk( storage, pos );
        const std::vector<BlockState> natural = snapshot_states( *pristine );
        free_chunk( pristine );
        // A second fresh gen measures the run-to-run drift: mapgen isn't
        // bit-stable on the GPU path (single-cell flips), so the overlay
        // check below tolerates a small diff count rather than masking.
        Chunk *pristine2 = gen_chunk( storage, pos );
        const std::vector<BlockState> natural2 = snapshot_states( *pristine2 );
        free_chunk( pristine2 );
        int drift = 0;
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            drift += BlockStates_equal( natural[ i ], natural2[ i ] ) ? 0 : 1;
        }
        pr_test( "    mapgen drift cells=%d", drift );

        const int interior = Chunk::get_index_from_coords( 10, 10, 10 );
        const int halo = Chunk::get_index_from_coords( -1, 10, 10 );
        std::vector<ChunkEditRun> runs;
        runs.push_back( { static_cast<uint32_t>( interior ), 1u, TNT_EDIT } );
        runs.push_back( { static_cast<uint32_t>( halo ), 1u, GOLD_EDIT } );
        std::sort( runs.begin( ), runs.end( ), []( const ChunkEditRun &a, const ChunkEditRun &b ) { return a.start < b.start; } );
        storage.persist_chunk_edits( pos, runs );

        Chunk *overlaid = gen_chunk( storage, pos );
        check_bool( "overlay applies interior edit", overlaid->is_edited( interior ) &&
                                                        BlockStates_equal( overlaid->state_at( interior ), TNT_EDIT ) );
        check_bool( "overlay applies halo edit", overlaid->is_edited( halo ) &&
                                                     BlockStates_equal( overlaid->state_at( halo ), GOLD_EDIT ) );
        int diff_count = 0;
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            if ( i == interior || i == halo ) {
                continue;
            }
            const BlockState now = overlaid->cells != nullptr ? overlaid->state_at( i ) : BLOCK_STATE_AIR;
            if ( !BlockStates_equal( now, natural[ i ] ) ) {
                if ( diff_count++ < 8 ) {
                    int cx, cy, cz;
                    Chunk::get_coords_from_index( i, cx, cy, cz );
                    pr_test( "    natural diff i=%d (%d,%d,%d) now=%d rot=%d pow=%d was=%d rot=%d pow=%d",
                             i, cx, cy, cz, now.id, now.rotation, now.current_redstone_power,
                             natural[ i ].id, natural[ i ].rotation, natural[ i ].current_redstone_power );
                }
            }
        }
        pr_test( "    natural diffs=%d (gen drift %d)", diff_count, drift );
        check_bool( "overlay leaves natural cells pristine", diff_count <= 64 );
        check_bool( "v1 load stays clean", overlaid->dirty == 0 );

        // A runtime edit on top persists everything (old edits + new).
        overlaid->set_block( glm::ivec3( 20, 10, 10 ), TNT_EDIT );
        overlaid->dirty = 1;
        overlaid->persist( storage );
        free_chunk( overlaid );
        Chunk *reloaded = gen_chunk( storage, pos );
        const int new_edit = Chunk::get_index_from_coords( 20, 10, 10 );
        check_bool( "persist/reload keeps edits", reloaded->is_edited( interior ) && reloaded->is_edited( halo ) &&
                                                      reloaded->is_edited( new_edit ) &&
                                                      BlockStates_equal( reloaded->state_at( new_edit ), TNT_EDIT ) );
        free_chunk( reloaded );

        // An edit in an above-terrain chunk materializes it.
        const glm::ivec3 sky_pos( 6, 10, 6 );
        const int sky_edit = Chunk::get_index_from_coords( 5, 5, 5 );
        std::vector<ChunkEditRun> sky_runs{ { static_cast<uint32_t>( sky_edit ), 1u, TNT_EDIT } };
        storage.persist_chunk_edits( sky_pos, sky_runs );
        Chunk *sky = gen_chunk( storage, sky_pos );
        check_bool( "sky edit materializes chunk", !sky->is_empty_chunk && sky->cells != nullptr );
        check_bool( "sky edit applied", sky->is_edited( sky_edit ) &&
                                            BlockStates_equal( sky->state_at( sky_edit ), TNT_EDIT ) );
        free_chunk( sky );
    }

    // --- 5. generation writes are not edits -----------------------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsGen" );
        const glm::ivec3 pos( 7, 0, 7 );
        Chunk *fresh = gen_chunk( storage, pos );
        check_bool( "fresh load has no edits", !fresh->has_edits( ) );
        // Dirty but unedited: nothing becomes a chunk file.
        fresh->dirty = 1;
        fresh->persist( storage );
        check_bool( "dirty-unedited writes no file", !storage.check_if_chunk_exists( pos ) );
        free_chunk( fresh );
    }

    // --- 6. multiplayer gate --------------------------------------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsMP" );
        const glm::ivec3 pos( 8, 0, 8 );
        std::vector<ChunkEditRun> runs{ { 10u, 1u, TNT_EDIT } };
        storage.persist_chunk_edits( pos, runs );
        ChestContentsMap chests;
        ChestContents chest{};
        chests[ glm::ivec3( 1, 2, 3 ) ] = chest;
        storage.persist_chests( pos, chests );

        storage.set_chunk_io_enabled( false );
        ChunkEdits gated;
        check_bool( "gate ignores existing files", storage.load_chunk_edits( pos, gated ) == 0 );
        // Writes while gated must not create or remove chunk/container files.
        storage.persist_chunk_edits( pos, { } );
        storage.persist_chunk_edits( pos, runs );
        storage.persist_chests( pos, { } );
        FurnaceContents fuel;
        fuel.slots[ FURNACE_SLOT_FUEL ].item_id = COAL;
        fuel.slots[ FURNACE_SLOT_FUEL ].quantity = 1;
        storage.persist_furnaces( pos, FurnaceContentsMap{ { glm::ivec3( 1, 2, 3 ), fuel } } );
        storage.set_chunk_io_enabled( true );

        ChunkEdits after;
        storage.load_chunk_edits( pos, after );
        check_bool( "gate blocks chunk writes", after.runs.size( ) == 1 && after.runs[ 0 ].count == 1 &&
                                                      BlockStates_equal( after.runs[ 0 ].state, TNT_EDIT ) );
        char furnace_name[ CHUNK_NAME_MAX_LENGTH ];
        snprintf( furnace_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_FURNACE, storage.get_map_name( ), pos.x, pos.y, pos.z );
        ChestContentsMap chests_back;
        storage.load_chests( pos, chests_back );
        check_bool( "gate blocks chest writes", chests_back.size( ) == 1 );
        struct stat st;
        check_bool( "gate writes no furnace file", stat( furnace_name, &st ) != 0 );
    }

    // --- 7. slot reuse drops stale cells and edit bits -----------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsReuse" );
        const glm::ivec3 ground( 9, 0, 9 ), sky( 9, 12, 9 ), ground2( 11, 0, 11 );
        const int edit_idx = Chunk::get_index_from_coords( 10, 10, 10 );
        std::vector<ChunkEditRun> runs{ { static_cast<uint32_t>( edit_idx ), 1u, TNT_EDIT } };
        storage.persist_chunk_edits( ground, runs );

        // One Chunk object repositioned like ChunkLoader recycles a slot.
        Chunk *slot = new Chunk( );
        slot->chunk_pos = ground;
        slot->chunk_mod = ground;
        slot->load_terrain( storage );
        check_bool( "reuse: ground load has edits", slot->has_edits( ) );

        slot->chunk_pos = sky;
        slot->chunk_mod = sky;
        slot->load_terrain( storage );
        check_bool( "reuse: sky slot cells freed", slot->cells == nullptr && slot->is_empty_chunk );
        check_bool( "reuse: sky slot edits cleared", !slot->has_edits( ) );

        Chunk *pristine2 = gen_chunk( storage, ground2 );
        const std::vector<BlockState> nat2 = snapshot_states( *pristine2 );
        free_chunk( pristine2 );
        slot->chunk_pos = ground2;
        slot->chunk_mod = ground2;
        slot->load_terrain( storage );
        check_bool( "reuse: second ground edits cleared", !slot->has_edits( ) );
        int drift = 0;
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            const BlockState now = slot->cells != nullptr ? slot->state_at( i ) : BLOCK_STATE_AIR;
            drift += BlockStates_equal( now, nat2[ i ] ) ? 0 : 1;
        }
        check_bool( "reuse: second ground matches pristine", drift <= 64 );
        free_chunk( slot );
    }

    // --- 8. all-air fold keeps the edit bits ---------------------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsAirFold" );
        const glm::ivec3 pos( 10, 0, 10 );
        // A v1 file whose single run flattens every cell to AIR — the chunk
        // folds to null cells but must keep its edit bits so persist writes
        // the runs back instead of unlinking the file.
        std::vector<ChunkEditRun> runs{ { 0u, CHUNK_BLOCK_SIZE, BLOCK_STATE_AIR } };
        storage.persist_chunk_edits( pos, runs );
        Chunk *flat = gen_chunk( storage, pos );
        check_bool( "air-fold: cells folded", flat->cells == nullptr && flat->is_empty_chunk );
        check_bool( "air-fold: edits kept", flat->has_edits( ) &&
                                                count_edit_bits( *flat ) == CHUNK_BLOCK_SIZE );
        flat->dirty = 1;
        flat->persist( storage );
        ChunkEdits back;
        check_bool( "air-fold: file kept", storage.load_chunk_edits( pos, back ) == 1 &&
                                                 count_edit_cells( back ) == CHUNK_BLOCK_SIZE );
        free_chunk( flat );
    }

    // --- 9. v1 edit equal to natural keeps its mark --------------------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsNaturalEq" );
        const glm::ivec3 pos( 12, 0, 12 );
        Chunk *pristine = gen_chunk( storage, pos );
        const std::vector<BlockState> natural = snapshot_states( *pristine );
        free_chunk( pristine );
        // Pick an interior solid cell so mapgen drift can't flip it to the
        // run state; run state == stored natural state exactly.
        int pick = -1;
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            int cx, cy, cz;
            Chunk::get_coords_from_index( i, cx, cy, cz );
            if ( cx >= 0 && cx < CHUNK_SIZE_X && cy >= 0 && cy < CHUNK_SIZE_Y && cz >= 0 && cz < CHUNK_SIZE_Z &&
                 natural[ i ].id != AIR && natural[ i ].id != LAST_BLOCK_ID ) {
                pick = i;
                break;
            }
        }
        check_bool( "v1-eq-natural: found a cell", pick >= 0 );
        if ( pick >= 0 ) {
            std::vector<ChunkEditRun> runs{ { static_cast<uint32_t>( pick ), 1u, natural[ pick ] } };
            storage.persist_chunk_edits( pos, runs );
            Chunk *c = gen_chunk( storage, pos );
            check_bool( "v1-eq-natural: marked edited", c->is_edited( pick ) );
            c->dirty = 1; // force the rewrite path
            c->persist( storage );
            ChunkEdits back;
            check_bool( "v1-eq-natural: persist keeps run",
                        storage.load_chunk_edits( pos, back ) == 1 && !back.runs.empty( ) &&
                            back.runs[ 0 ].start == static_cast<uint32_t>( pick ) );
            free_chunk( c );
        }
    }

    // --- 10. corrupt legacy file with overflowing run lengths -----------------
    {
        MapStorage storage;
        storage.init( "TestChunkEditsCorrupt" );
        const glm::ivec3 pos( 13, 0, 13 );
        char file_name[ CHUNK_NAME_MAX_LENGTH ];
        snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, storage.get_map_name( ), pos.x, pos.y, pos.z );
        // Legacy layout: u32 record_size, then {state rec, u32 count} records.
        // A record_size matching sizeof(BlockState)+4 = sizeof(STORAGE_TYPE);
        // the state record is the first record_size-4 bytes of BlockState.
        const uint32_t record_size = sizeof( BlockState ) + 4;
        FILE *f = fopen( file_name, "wb" );
        fwrite( &record_size, 4, 1, f );
        auto put = [ & ]( const BlockState &s, uint32_t count ) {
            fwrite( &s, record_size - 4, 1, f );
            fwrite( &count, 4, 1, f );
        };
        put( TNT_EDIT, CHUNK_BLOCK_SIZE ); // fills the whole volume
        put( GOLD_EDIT, 7 );               // one record too many
        fclose( f );
        ChunkEdits edits;
        check_bool( "corrupt legacy decodes without crash", storage.load_chunk_edits( pos, edits ) == 1 );
        uint32_t last_end = 0;
        bool in_bounds = true;
        for ( const ChunkEditRun &run : edits.runs ) {
            in_bounds = in_bounds && run.start + run.count <= CHUNK_BLOCK_SIZE;
            last_end = run.start + run.count;
        }
        check_bool( "corrupt legacy runs stay in bounds", in_bounds && last_end <= CHUNK_BLOCK_SIZE );
        Chunk *c = gen_chunk( storage, pos );
        check_bool( "corrupt legacy overlay survives", c->is_edited( 0 ) &&
                                                           BlockStates_equal( c->state_at( 0 ), TNT_EDIT ) );
        free_chunk( c );
    }

    return failures;
}
