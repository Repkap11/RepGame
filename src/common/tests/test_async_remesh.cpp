// Async server-diff remesh: Chunk::build_mesh equivalence with
// calculate_populated_blocks, 26-connected job grouping, version-checked
// all-or-nothing publish, discard/re-mark, sync promotion (seam safety),
// the finalize deferral (path 1), and the REPGAME_ASYNC_REMESH killswitch.
// Jobs run inline via mesh_job_run so no real worker threads are needed.
#include "test_util.hpp"

#include <cstring>
#include <vector>

// A chunk's current published mesh contents (num + bytes per render order).
struct MeshSnap {
    int num[ LAST_RENDER_ORDER ];
    std::vector<BlockCoords> bytes[ LAST_RENDER_ORDER ];
};

int test_async_remesh( ) {
    if ( block_definitions == nullptr ) {
        block_definitions_initilize_definitions( nullptr );
    }
    setenv( "REPGAME_MOB_TEST", "1", 1 );
    unsetenv( "REPGAME_ASYNC_REMESH" );
    int failures = 0;
    auto check = [ &failures ]( bool cond, const char *msg ) {
        if ( !cond ) {
            failures++;
            fprintf( stderr, "test_async_remesh: %s\n", msg );
        }
    };
    const BlockState stone{ STONE, 0, 0, STONE, 0 };
    const int border_idx = Chunk::get_index_from_coords( CHUNK_SIZE_X - 1, 5, 5 );
    const int mirror_idx = Chunk::get_index_from_coords( -1, 5, 5 );

    // Give a chunkArray slot a consistent LIVE identity at `pos` with real cells.
    auto live_chunk = []( ChunkLoader &cl, const glm::ivec3 &pos ) -> Chunk * {
        const int sx = 2 * CHUNK_RADIUS_XZ + 1;
        const int sy = 2 * CHUNK_RADIUS_Y + 1;
        Chunk &c = cl.chunkArray[ chunk_slot_from_pos( pos ) ];
        c.chunk_mod = glm::ivec3( ( ( pos.x % sx ) + sx ) % sx, ( ( pos.y % sy ) + sy ) % sy,
                                  ( ( pos.z % sx ) + sx ) % sx );
        c.chunk_pos = pos;
        c.load_state = CHUNK_LIVE;
        if ( c.cells == nullptr ) {
            c.cells = chunk_alloc_cells( );
        }
        return &c;
    };

    auto mesh_snap = []( const Chunk &c ) {
        MeshSnap s;
        for ( int ro = 0; ro < LAST_RENDER_ORDER; ro++ ) {
            if ( c.layers != nullptr && c.layers[ ro ].populated_blocks != nullptr ) {
                s.num[ ro ] = c.layers[ ro ].num_instances;
                s.bytes[ ro ].assign( c.layers[ ro ].populated_blocks,
                                      c.layers[ ro ].populated_blocks + s.num[ ro ] );
            } else {
                s.num[ ro ] = 0;
            }
        }
        return s;
    };
    auto mesh_snap_equal = []( const MeshSnap &a, const MeshSnap &b ) {
        for ( int ro = 0; ro < LAST_RENDER_ORDER; ro++ ) {
            if ( a.num[ ro ] != b.num[ ro ] ||
                 memcmp( a.bytes[ ro ].data( ), b.bytes[ ro ].data( ),
                         a.bytes[ ro ].size( ) * sizeof( BlockCoords ) ) != 0 ) {
                return false;
            }
        }
        return true;
    };

    // Submit pending marks, take the single job off the mesh queue, run it
    // inline (worker body) — the render-thread publish is left to the caller.
    auto grab_one_job = []( ChunkLoader &cl, MeshJob &job ) {
        if ( cl.async_submit_jobs( ) != 1 ) {
            return false;
        }
        if ( !cl.terrain_loading_thread.mesh_pop_work( &job ) ) {
            return false;
        }
        mesh_job_run( job );
        return true;
    };

    // build_mesh equivalence: varied content (solid/merged, translucent,
    // water incl. a sloped flow level, flowers, a rotated block, a powered
    // piston/extra instance, a cell with extras/power) must mesh identically
    // via the static builder and the layer-writing wrapper.
    {
        Chunk *c = new Chunk( );
        c->chunk_pos = glm::ivec3( 4, 0, 4 );
        c->cells = chunk_alloc_cells( );
        const BlockState glass{ GLASS, 0, 0, GLASS, 0 };
        const BlockState water0{ WATER, 0, 0, WATER, 0 };
        const BlockState water3{ WATER, 0, 3, WATER, 0 };
        const BlockState flower{ DANDELION, 0, 0, DANDELION, 0 };
        const BlockState stairs{ OAK_STAIRS, 0, 1, OAK_STAIRS, 0 };
        const BlockState piston{ PISTON, 0, 2, PISTON, 15 };
        const BlockState torch{ REDSTONE_TORCH, 0, 0, REDSTONE_TORCH, 15 };
        for ( int x = 1; x < 15; x++ ) {
            for ( int z = 1; z < 15; z++ ) {
                c->set_block( glm::ivec3( x, 10, z ), stone );
            }
        }
        c->set_block( glm::ivec3( 4, 11, 4 ), glass );
        c->set_block( glm::ivec3( 5, 11, 5 ), water0 );
        c->set_block( glm::ivec3( 6, 11, 5 ), water3 );
        c->set_block( glm::ivec3( 6, 10, 5 ), water0 );
        c->set_block( glm::ivec3( 8, 11, 8 ), flower );
        c->set_block( glm::ivec3( 9, 11, 9 ), stairs );
        c->set_block( glm::ivec3( 11, 11, 11 ), piston );
        c->set_block( glm::ivec3( 12, 11, 12 ), torch );
        c->is_empty_chunk = false;
        c->calculate_populated_blocks( );
        int total = 0;
        for ( int ro = 0; ro < LAST_RENDER_ORDER; ro++ ) {
            total += c->layers[ ro ].num_instances;
        }
        check( total > 0, "build_mesh: fixture produced empty mesh" );

        BlockCell *cells_copy = chunk_alloc_cells( );
        memcpy( cells_copy, c->cells, sizeof( BlockCell ) * CHUNK_BLOCK_SIZE );
        BlockExtra *extras_copy = nullptr;
        if ( c->extras != nullptr ) {
            extras_copy = chunk_alloc_extras( );
            memcpy( extras_copy, c->extras, sizeof( BlockExtra ) * CHUNK_BLOCK_SIZE );
        }
        ChunkMeshResult res = {};
        Chunk::build_mesh( cells_copy, extras_copy, c->chunk_pos, res );
        for ( int ro = 0; ro < LAST_RENDER_ORDER; ro++ ) {
            int want_num = c->layers[ ro ].num_instances;
            check( res.num[ ro ] == want_num &&
                       ( want_num == 0 || memcmp( res.blocks[ ro ], c->layers[ ro ].populated_blocks,
                                                  want_num * sizeof( BlockCoords ) ) == 0 ),
                   "build_mesh: render order mismatch" );
            free( res.blocks[ ro ] );
        }
        chunk_free_cells( cells_copy );
        if ( extras_copy != nullptr ) {
            chunk_free_extras( extras_copy );
        }

        // cells==nullptr must produce an empty chunk (canonical all-air).
        ChunkMeshResult air = {};
        Chunk::build_mesh( nullptr, nullptr, glm::ivec3( 9, 9, 9 ), air );
        for ( int ro = 0; ro < LAST_RENDER_ORDER; ro++ ) {
            check( air.num[ ro ] == 0 && air.blocks[ ro ] == nullptr,
                   "build_mesh: air chunk not empty" );
            free( air.blocks[ ro ] );
        }

        // Standalone free (same teardown as free_chunk in test_chunk_edits.cpp).
        c->unprogram_terrain( ); // frees layers[].populated_blocks
        free( c->layers );
        c->layers = nullptr;
        c->free_block_storage( );
        free( c->edited ); // free_block_storage leaves it — see chunk.cpp
        delete c;
    }

    // (a) Border diff on A mirrors into LIVE B: one job {A,B}, both meshes
    // swap only at publish.
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        Chunk *B = live_chunk( cl, glm::ivec3( 1, 0, 0 ) );
        check( A != nullptr && B != nullptr, "a: missing chunks" );
        // Interior content so B's published mesh is non-empty — the mirror
        // only writes B's halo.
        B->set_block( glm::ivec3( 10, 10, 10 ), stone );
        cl.apply_diff_block( *A, border_idx, &stone );
        check( A->async_wanted && B->async_wanted, "a: diff did not mark both chunks" );
        check( A->needs_repopulation && B->needs_repopulation, "a: needs_repopulation" );
        check( B->state_at( mirror_idx ).id == STONE, "a: mirror cell not written" );
        MeshJob job;
        check( grab_one_job( cl, job ), "a: no single job" );
        check( job.members.size( ) == 2, "a: job does not contain {A,B}" );
        check( A->layers == nullptr && B->layers == nullptr, "a: mesh changed before publish" );
        check( A->async_wanted == 0 && B->async_wanted == 0, "a: async_wanted not cleared by submit" );
        cl.async_publish_job( job, false );
        check( !A->needs_repopulation && !B->needs_repopulation, "a: chunks still dirty after publish" );
        check( A->layers != nullptr && B->layers != nullptr, "a: no layers after publish" );
        check( A->layers[ RenderOrder_Opaque ].num_instances > 0, "a: A mesh empty" );
        check( B->layers[ RenderOrder_Opaque ].num_instances > 0, "a: B mesh empty (mirror had no effect)" );
        check( A->should_render && B->should_render, "a: chunks not drawable after publish" );
        check( cl.async_jobs_published == 1, "a: published stat" );
        delete worldp;
    }

    // (b) A write to B after the snapshot discards the whole job: old meshes
    // stay, both chunks are re-marked.
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        Chunk *B = live_chunk( cl, glm::ivec3( 1, 0, 0 ) );
        A->set_block( glm::ivec3( 10, 10, 10 ), stone );
        A->is_empty_chunk = false;
        A->calculate_populated_blocks( );
        A->needs_repopulation = false;
        MeshSnap before_a = mesh_snap( *A );
        cl.apply_diff_block( *A, border_idx, &stone );
        MeshJob job;
        check( grab_one_job( cl, job ), "b: no job" );
        check( job.members.size( ) == 2, "b: job members" );
        // A post-snapshot write (own edit / next diff) bumps B's version.
        B->set_block( glm::ivec3( 5, 5, 5 ), stone );
        uint32_t discards0 = cl.async_jobs_discarded;
        cl.async_publish_job( job, false );
        check( cl.async_jobs_discarded == discards0 + 1, "b: job not discarded" );
        check( A->needs_repopulation && B->needs_repopulation, "b: chunks not kept dirty" );
        check( A->async_wanted == 1 && B->async_wanted == 1, "b: chunks not re-marked" );
        check( A->async_discards == 1 && B->async_discards == 1, "b: discard counter" );
        check( mesh_snap_equal( before_a, mesh_snap( *A ) ), "b: A mesh changed despite discard" );
        check( B->layers == nullptr, "b: B gained a mesh despite discard" );
        // The replacement job can be submitted and published.
        MeshJob job2;
        check( grab_one_job( cl, job2 ), "b: no replacement job" );
        cl.async_publish_job( job2, false );
        check( !A->needs_repopulation && !B->needs_repopulation, "b: replacement publish failed" );
        check( B->layers != nullptr && B->layers[ RenderOrder_Opaque ].num_instances > 0,
               "b: B not meshed after replacement publish" );
        check( A->async_discards == 0 && B->async_discards == 0, "b: discards not reset" );
        delete worldp;
    }

    // (c) In-flight {A,B} with B only diagonal to A: a synchronous remesh on
    // A must remesh B in the same pass (seam safety).
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        Chunk *B = live_chunk( cl, glm::ivec3( 1, 1, 0 ) );
        B->set_block( glm::ivec3( 10, 10, 10 ), stone );
        B->is_empty_chunk = false;
        // A corner diff write in A mirrors across the diagonal edge into B's
        // halo — that real mirror write is what links A and B (plain
        // adjacency no longer groups chunks).
        cl.apply_diff_block( *A, Chunk::get_index_from_coords( CHUNK_SIZE_X - 1, CHUNK_SIZE_Y - 1, 8 ), &stone );
        check( A->async_links != 0 && B->async_links != 0, "c: mirror write did not link chunks" );
        MeshJob job;
        check( grab_one_job( cl, job ), "c: no job" );
        check( job.members.size( ) == 2, "c: diagonal component not in one job" );
        // Job stays in-flight; a synchronous remesh of A promotes the group.
        cl.remesh_now_impl( *A, false );
        check( !A->needs_repopulation && !B->needs_repopulation, "c: group not remeshed" );
        check( B->async_wanted == 0, "c: B async_wanted not cleared" );
        check( B->layers != nullptr && B->layers[ RenderOrder_Opaque ].num_instances > 0,
               "c: B not synchronously remeshed" );
        check( cl.async_jobs_promoted >= 1, "c: promoted stat" );
        // The stale in-flight result is a no-op at publish.
        cl.async_publish_job( job, false );
        check( !A->needs_repopulation && !B->needs_repopulation, "c: publish re-dirtied chunks" );
        delete worldp;
    }

    // (d) Path 1: a finalize that applied cell-changing diffs leaves the
    // chunk LIVE but undrawn until its async job publishes.
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        cl.apply_diff_block( *A, Chunk::get_index_from_coords( 5, 5, 5 ), &stone );
        check( A->needs_repopulation && A->should_render, "d: diff did not dirty the chunk" );
        cl.finalize_defer_mesh( *A );
        check( A->should_render == 0, "d: chunk still drawable after defer" );
        check( A->async_wanted == 1 && A->needs_repopulation, "d: not marked for async remesh" );
        MeshJob job;
        check( grab_one_job( cl, job ), "d: no job" );
        check( job.members.size( ) == 1, "d: job should contain only A" );
        check( A->should_render == 0, "d: chunk drawn before publish" );
        cl.async_publish_job( job, false );
        check( A->should_render == 1, "d: chunk not drawable after publish" );
        check( !A->needs_repopulation && A->layers[ RenderOrder_Opaque ].num_instances > 0,
               "d: mesh not published" );
        delete worldp;
    }

    // (e) Disabled restores the old synchronous path (the env var itself is
    // read once and cached — the override is the testable killswitch).
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 0;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        Chunk *B = live_chunk( cl, glm::ivec3( 1, 0, 0 ) );
        cl.apply_diff_block( *A, border_idx, &stone );
        check( A->async_wanted == 0 && B->async_wanted == 0, "e: async mark under killswitch" );
        check( A->needs_repopulation && B->needs_repopulation, "e: needs_repopulation" );
        check( B->state_at( mirror_idx ).id == STONE, "e: mirror cell not written" );
        check( cl.async_submit_jobs( ) == 0, "e: job submitted under killswitch" );
        delete worldp;
    }

    // (f) Superseded job can't split a seam: J1={A,B} in flight, an interior
    // write to A bumps its version, the next submission must fold in-flight B
    // into J2={A,B} — so discarding J1 re-marks nobody and J2 swaps both.
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        Chunk *B = live_chunk( cl, glm::ivec3( 1, 0, 0 ) );
        B->set_block( glm::ivec3( 10, 10, 10 ), stone );
        cl.apply_diff_block( *A, border_idx, &stone );
        MeshJob j1;
        check( grab_one_job( cl, j1 ), "f: no J1" );
        check( j1.members.size( ) == 2, "f: J1 not {A,B}" );
        const uint32_t j1_seq = j1.seq;
        // Interior diff on A while J1 is in flight — marks only A.
        cl.apply_diff_block( *A, Chunk::get_index_from_coords( 5, 5, 6 ), &stone );
        check( A->async_wanted == 1 && B->async_wanted == 0, "f: interior diff marked B" );
        MeshJob j2;
        check( grab_one_job( cl, j2 ), "f: no J2" );
        // J2 must contain BOTH members: the flood folds in-flight B into the
        // newer job (new seq), so discarding J1 re-marks nothing.
        check( j2.members.size( ) == 2, "f: J2 does not contain {A,B}" );
        check( A->async_job_seq == j2.seq && B->async_job_seq == j2.seq, "f: B kept old seq" );
        // Deliver J1: invalid (A's version moved) — but neither mesh may
        // change, and nothing may be re-marked.
        cl.async_publish_job( j1, false );
        cl.async_in_flight_seqs.erase( j1_seq );
        check( A->layers == nullptr && B->layers == nullptr, "f: J1 publish changed a mesh" );
        check( A->needs_repopulation && B->needs_repopulation, "f: J1 publish cleared dirty" );
        check( A->async_wanted == 0 && B->async_wanted == 0, "f: J1 discard re-marked members" );
        // Deliver J2: both swap in the same publish call.
        cl.async_publish_job( j2, false );
        check( !A->needs_repopulation && !B->needs_repopulation, "f: J2 publish failed" );
        check( A->layers != nullptr && A->layers[ RenderOrder_Opaque ].num_instances > 0,
               "f: A not meshed by J2" );
        check( B->layers != nullptr && B->layers[ RenderOrder_Opaque ].num_instances > 0,
               "f: B not meshed by J2" );
        check( cl.async_jobs_discarded >= 1, "f: J1 not counted discarded" );
        delete worldp;
    }

    // Submit until the wanted list is empty: the per-frame submit budget can
    // legitimately defer seeds to a later "frame" (each iteration here is
    // one frame's worth). Returns the total number of jobs submitted.
    auto submit_all = []( ChunkLoader &cl ) {
        int n = 0;
        for ( int s = cl.async_submit_jobs( ); s != 0; s = cl.async_submit_jobs( ) ) {
            n += s;
        }
        return n;
    };
    // Pop every queued job (each run inline) into `out`.
    auto grab_all_jobs = []( ChunkLoader &cl, std::vector<MeshJob> &out ) {
        MeshJob job;
        while ( cl.terrain_loading_thread.mesh_pop_work( &job ) ) {
            mesh_job_run( job );
            out.push_back( std::move( job ) );
        }
    };

    // (g) Interior-only diffs create no links: a 3x1x3 block of adjacent
    // LIVE chunks is 9 independent single-member jobs — adjacency alone must
    // NOT group them (that was the load-in megajob bug).
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        for ( int x = -1; x <= 1; x++ ) {
            for ( int z = -1; z <= 1; z++ ) {
                Chunk *c = live_chunk( cl, glm::ivec3( x, 0, z ) );
                cl.apply_diff_block( *c, Chunk::get_index_from_coords( 5, 5, 5 ), &stone );
            }
        }
        const int n_jobs = submit_all( cl );
        check( n_jobs == 9, "g: interior-only diffs grouped into fewer than 9 jobs" );
        std::vector<MeshJob> jobs;
        grab_all_jobs( cl, jobs );
        check( jobs.size( ) == 9, "g: wrong job count" );
        int singles = 0;
        for ( const MeshJob &j : jobs ) {
            singles += j.members.size( ) == 1;
        }
        check( singles == 9, "g: jobs not all single-member" );
        delete worldp;
    }

    // (g2) Same via path 1: finalize-deferred chunks with interior-only
    // pending diffs — 9 jobs, and publishing the centre job alone makes only
    // the centre drawable.
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *centre = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        for ( int x = -1; x <= 1; x++ ) {
            for ( int z = -1; z <= 1; z++ ) {
                Chunk *c = ( x == 0 && z == 0 ) ? centre : live_chunk( cl, glm::ivec3( x, 0, z ) );
                cl.apply_diff_block( *c, Chunk::get_index_from_coords( 5, 5, 5 ), &stone );
                cl.finalize_defer_mesh( *c );
            }
        }
        const int n_jobs = submit_all( cl );
        check( n_jobs == 9, "g2: path-1 interior diffs grouped" );
        std::vector<MeshJob> jobs;
        grab_all_jobs( cl, jobs );
        int published = 0;
        for ( MeshJob &j : jobs ) {
            if ( j.members.size( ) == 1 && j.members[ 0 ].chunk == centre ) {
                cl.async_publish_job( j, false );
                published++;
            }
        }
        check( published == 1, "g2: centre job not found/published" );
        check( centre->should_render == 1, "g2: centre not drawable after publish" );
        int drawable = 0;
        for ( int x = -1; x <= 1; x++ ) {
            for ( int z = -1; z <= 1; z++ ) {
                Chunk *c = cl.get_chunk( glm::ivec3( x, 0, z ) );
                drawable += c != nullptr && c->should_render;
            }
        }
        check( drawable == 1, "g2: non-centre chunk drawable before its own publish" );
        delete worldp;
    }

    // (h) Border diffs A|B and B|C link through B → one job {A,B,C}; D
    // adjacent to C with an interior-only diff is its own job.
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        Chunk *B = live_chunk( cl, glm::ivec3( 1, 0, 0 ) );
        Chunk *C = live_chunk( cl, glm::ivec3( 2, 0, 0 ) );
        Chunk *D = live_chunk( cl, glm::ivec3( 3, 0, 0 ) );
        cl.apply_diff_block( *A, border_idx, &stone ); // writes B's halo → A-B
        cl.apply_diff_block( *B, border_idx, &stone ); // writes C's halo → B-C
        cl.apply_diff_block( *D, Chunk::get_index_from_coords( 5, 5, 5 ), &stone ); // interior
        check( submit_all( cl ) == 2, "h: wrong component count" );
        std::vector<MeshJob> jobs;
        grab_all_jobs( cl, jobs );
        check( jobs.size( ) == 2, "h: job count" );
        int tri = 0, single = 0;
        for ( MeshJob &j : jobs ) {
            tri += j.members.size( ) == 3;
            single += j.members.size( ) == 1 && j.members[ 0 ].chunk == D;
            cl.async_publish_job( j, false );
        }
        check( tri == 1 && single == 1, "h: jobs not {A,B,C} + {D}" );
        // Links satisfied by the publish: a later interior-only diff on A
        // yields a single-member job — A is no longer glued to B.
        cl.apply_diff_block( *A, Chunk::get_index_from_coords( 7, 7, 7 ), &stone );
        check( submit_all( cl ) == 1, "h: no job after interior diff" );
        MeshJob j3;
        check( cl.terrain_loading_thread.mesh_pop_work( &j3 ), "h: missing job" );
        check( j3.members.size( ) == 1 && j3.members[ 0 ].chunk == A, "h: A still linked to B after publish" );
        mesh_job_run( j3 );
        cl.async_publish_job( j3, false );
        delete worldp;
    }

    // (k) J1={A,B} linked + in flight; an interior write on unlinked
    // neighbour C of A does NOT fold A into C's job (the fold only follows
    // link edges — test f still covers the linked case).
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        Chunk *A = cl.get_chunk( glm::ivec3( 0, 0, 0 ) );
        Chunk *B = live_chunk( cl, glm::ivec3( 1, 0, 0 ) );
        Chunk *C = live_chunk( cl, glm::ivec3( 0, 1, 0 ) );
        cl.apply_diff_block( *A, border_idx, &stone ); // A-B linked
        MeshJob j1;
        check( grab_one_job( cl, j1 ), "k: no J1" );
        check( j1.members.size( ) == 2, "k: J1 not {A,B}" );
        // Interior write on C: adjacent to A but no link edge was created.
        cl.apply_diff_block( *C, Chunk::get_index_from_coords( 5, 5, 5 ), &stone );
        MeshJob j2;
        check( grab_one_job( cl, j2 ), "k: no J2" );
        check( j2.members.size( ) == 1 && j2.members[ 0 ].chunk == C, "k: C's job folded A or B" );
        cl.async_publish_job( j1, false );
        cl.async_in_flight_seqs.erase( j1.seq );
        cl.async_publish_job( j2, false );
        delete worldp;
    }

    // (order) Submission seeds nearest-first from chunk_center.
    {
        World *worldp = new World( );
        test_setup_world( *worldp );
        ChunkLoader &cl = worldp->chunkLoader;
        cl.async_remesh_override = 1;
        cl.chunk_center = glm::ivec3( 0, 0, 0 );
        Chunk *near_c = live_chunk( cl, glm::ivec3( 1, 0, 0 ) );
        Chunk *far_c = live_chunk( cl, glm::ivec3( 3, 0, 0 ) );
        // Mark far first so the wanted list order is far-then-near.
        cl.apply_diff_block( *far_c, Chunk::get_index_from_coords( 5, 5, 5 ), &stone );
        cl.apply_diff_block( *near_c, Chunk::get_index_from_coords( 5, 5, 5 ), &stone );
        check( submit_all( cl ) == 2, "order: wrong job count" );
        MeshJob j;
        check( cl.terrain_loading_thread.mesh_pop_work( &j ), "order: no first job" );
        check( j.members.size( ) == 1 && j.members[ 0 ].chunk == near_c, "order: first job not nearest" );
        mesh_job_run( j );
        cl.async_publish_job( j, false );
        MeshJob j2;
        if ( cl.terrain_loading_thread.mesh_pop_work( &j2 ) ) {
            mesh_job_run( j2 );
            cl.async_publish_job( j2, false );
        }
        delete worldp;
    }

    if ( failures == 0 ) {
        printf( "test_async_remesh PASSED\n" );
    } else {
        printf( "test_async_remesh FAILED %d checks\n", failures );
    }
    return failures;
}
