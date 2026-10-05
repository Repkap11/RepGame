# Lighting thread plan

Goal: get light propagation/finalization off the render thread. Frame-rate
regression vs master (250fps -> ~50fps, now ~165 steady / ~70 while streaming)
is dominated by `light_finalize_chunk` (~2.4ms/chunk of pure CPU: cascade,
boundary scan, border sync) running inside the 8ms chunk-finalize budget.

## Why one thread, not N terrain workers

The load-in work writes *outside* its own chunk: halo mirrors go into up to 8
neighbors (`light_set`), the cascade reads `column_open`/`sky_open_above` of
the whole 17-chunk column, border sync writes neighbor halos. Multiple terrain
workers writing neighbors' light[] concurrently gives stale/lost updates with
no later event to fix them. A single lighting thread makes all cross-chunk
light mutation single-writer.

Conflict domain for future parallelism: two chunk finalizes conflict iff
|dx| <= 2 AND |dz| <= 2 (halo = +-1 chunk; cascade = the whole x,z column).
Chunks >=3 apart in x or z are fully independent.

## Ownership model

- Terrain workers (unchanged): `light_fill_chunk` at gen time -- interior
  emitter BFS + column_open/fill_from. Only writes the chunk's own memory
  before publish. No change needed.
- Lighting thread (new): sole mutator of light[] interior+halo, light_columns
  flags, add/remove BFS queues, light_pending/reseed handling. Runs
  light_finalize_chunk (cascade + boundary scan + border sync), queue
  processing, rechecks.
- Render thread (keeps): GL only -- light_ensure_texture,
  light_upload_dirty, per-chunk texture binds. Reads light[] while light
  thread writes it -> worst case a few torn texels for one frame, self-heals
  via dirty marks.

## Implementation steps

1. Job queue (mutex + cv or lock-free MPSC) to the light thread:
   - finalize(chunk*) -- emitted after mesh finalize instead of running
     light_finalize_chunk inline
   - recheck(world_pos) -- from set_block edits (replaces direct
     light_recheck_block calls on render thread)
   - pending-drain(chunk*) -- from light_pending_enqueue (diffs)
   - unload(chunk*) -- tombstone before render frees light[] (or ack protocol)
2. Dirty-box handoff: light thread sets dirty+box, render reads+uploads+
   clears. Needs a small lock or version counter so a mark landing mid-upload
   isn't lost (today's light_dirty_list is single-threaded -- make enqueue
   safe for light-thread producers).
3. light_process_queue moves to light thread; render-thread
   LIGHT_PROPAGATE_BUDGET goes away.
4. BFS seed pushes from finalize stay on the light thread (already are --
   light_add_seed is only called from light paths).
5. Chunk::draw: do not draw until light_finalized, or draw with whatever
   light texture exists (transient dark/bright ok) -- decide by feel.
6. Unload handshake: light thread must not hold a chunk pointer across the
   render thread freeing light[]. Simplest: never free light[] on unload
   (keep until slot reuse), or route frees through the light thread.
7. WASM/single-thread fallback: same functions called inline from
   render_chunks behind #if -- keep the existing budgeted path.
8. Gate: finalize a chunk only once neighbors are past is_loading (fixes
   today's transient border staleness too).

## Races that remain (all narrow)

- Render reads light[] during glTexSubImage3D while light thread writes ->
  benign torn texels, self-healing.
- Producer writes pending_count then enqueues; light thread clears
  listed-flag before reading pending fields (already implemented that order).
- blocks[] read during finalize while a terrain worker writes a *loading*
  neighbor -> skipped by the is_loading gate.
- atomics/fences: is_loading should be release-store on publish, acquire on
  read (probably fine on x86 today but should be made explicit).

## Status: worker pool (claims) implemented

The pool uses a **column-claim grid** rather than the 3x3 coloring or region
shards discussed earlier:

- K workers (`LIGHT_WORKER_COUNT` = 4 std::threads) pull jobs under
  `light_work_mutex`. A job's conflict domain is its 5x5 column footprint;
  the worker stamps all 25 (x,z) column keys into `light_claims`
  (unordered_set) all-or-nothing. Blocked jobs stay queued and are skipped in
  the scan; releasing a claim `notify_all`s so waiters rescan.
- Job column: FINALIZE claims the chunk_pos captured at submit (execution
  validates `chunk->chunk_pos == job.pos` — a recycled slot's fresh dequeue
  re-submits, so skipping loses nothing). RECHECK claims the block's column.
  Pending-diff chunks in `light_pending_list` are claimed the same way —
  the list entry is erased and `light_pending_listed` cleared under the lock
  at claim time so producers re-enqueue fresh entries.
- The cv predicate only wakes for *runnable* work (some claimable job/pending
  entry, or BFS-runnable) — a queue of all-blocked jobs must not spin.
- **BFS exclusion**: shared `light_add_queue`/`light_remove_queue` deques
  can't be claimed per-seed, so `light_process_queue` runs only while no
  claims are in flight (`light_claims.empty()`) and marks `light_bfs_active`
  so no claim starts mid-drain. Seeds pushed by claimed jobs wait for a
  claims-free window — same interleaving the single thread had.
- Push sites on the shared deques take `light_seed_lock` (atomic_flag
  spinlock — tiny critical section) since concurrent claimed jobs push
  simultaneously. Pops are unlocked: they only happen in the BFS drain.
- `light_bfs_pending` (atomic) counts queued seeds so the cv predicate can
  test queue-nonempty without touching deque internals from another thread.
- BFS drains in 2ms slices but keeps the slot while seeds remain and no
  jobs/pending wait; yields the moment either appears or on stop.
- Backlog-priority window (light_bfs_priority): when pending seeds exceed
  LIGHT_BFS_PRI_HI (~3M) new FINALIZE claims pause until a drain pass drops
  the backlog under LIGHT_BFS_PRI_LO or ~100ms elapses. RECHECK and pending
  diffs bypass the pause so block edits stay responsive. Without this, a
  sustained job flow starved the BFS entirely — border seams, overhang
  shading and torch spreads sat unlit for ~a minute during load-in.
- Border-sync pass 2 seeds only when a foreign-interior cell adjacent to
  the shell cell could actually gain light (was: one seed per differing
  mirror cell, up to 7 dups per corner). Cut seed pushes ~1000x during
  load-in (~1.8M -> ~2k seeds per 2s interval); backlog stays ~0.
- light_thread_stop drops queued work instead of draining it — cleanup()
  frees all light volumes right after, so draining was seconds of wasted
  finalizes on quit-during-load-in. Containers are cleared for re-init.
- Start: `light_thread_running` is set to the spawned count BEFORE workers
  spawn (a thread never sees the flag while producers still inline). Stop:
  flag + notify_all + join all, then inline drain of leftovers — unchanged.
- `light_process_pending_chunk(Chunk&)` extracted from `light_drain_pending`
  (the latter remains the serial fallback path).
- Near-player priority is at SUBMIT time, not claim time: jobs within
  LIGHT_NEAR_COL_DIST (6 columns) of chunk_center and all rechecks
  push_front into light_jobs; the first-claimable scan then finds them
  first. A per-claim nearest-scan was tried and reverted — the O(queue x
  25-hash) scan under light_work_mutex convoyed the render thread's lock
  acquisitions at load-in depths (~20 FPS). Front-insertion is O(1) and
  self-corrects as the player moves.
- Seeds get the same treatment: pushes whose target column is within
  LIGHT_NEAR_COL_DIST land in light_add_queue_pri / light_remove_queue_pri,
  which light_process_queue drains strictly before the shared queues.
  Without this, near seeds FIFO-mix behind the whole streaming backlog and
  the chunk under the player converges last even though its job ran first.
- A STARVATION window complements the backlog-size window: if
  light_bfs_pending > LIGHT_BFS_STARVE_MIN and no drain has run for
  LIGHT_BFS_STARVE_US (~100ms), light_bfs_priority opens even below the HI
  watermark — during streaming claims.empty() basically never happens
  otherwise, so seeds accumulated for the entire load-in and only drained
  when it stopped (pop=0 for tens of seconds; backlog ~226k). Starve
  windows drain to empty with a ~25ms cap (light_bfs_starve distinguishes
  the exit target from the HI-triggered LO watermark).

Bug found during bring-up: `light_seed_interior_boundary` was gated on
`cells_filled > 0`, but the optimistic prefill means a correctly-predicted
chunk flips no flags — the scan never ran and canopy-shaded columns stayed
black until an unrelated edit re-seeded them. Both call sites now run it
unconditionally (the per-column lit_top early-out already covers the
fully-dark case). Same class of bug: concurrent `push_back` on the shared
`light_add_queue`/`light_remove_queue` deques from two claimed jobs
corrupted deque internals (SEGV writing to a null slot). Fixed by
`light_seed_lock`.

GPU upload handoff (atomic dirty box):

- `Chunk::light_dirty_box` packs the dirty texel min/max into one atomic
  uint64 (6 bits/coord; LIGHT_BOX_EMPTY = min>max). Marks CAS-merge their
  texel; the uploader clears `light_upload_listed` AND exchanges the box in
  the same critical section that pops the chunk from `light_dirty_list`.
- Why: the old flag+min/max scheme wiped `light_dirty` AFTER reading the
  box, so a racing mark had its flag cleared while the chunk was already
  unlisted — the re-pushed entry was then skipped as clean and the mark was
  lost permanently (converged-but-black patches under trees that survived
  an empty queue). A mark's CAS either lands inside the pop's snapshot or
  orders after it and observes listed==0, re-adding the chunk.
- `light_ensure_texture` exchanges the box before its full upload for the
  same reason (marks mid-upload stay pending). The per-frame upload cap
  bounds the pop itself, so unprocessed entries keep their boxes — no
  requeue path that could strand consumed marks.

Measured (legacy terrain, ~2s probe intervals, 4 workers):

- fin ~800-1500 finalizes/interval during load-in bursts (parallel).
- BFS pops ~400-800k seeds/interval once claims pause — drain converges in
  the gaps between bursts (BFS is inherently serial under this model).
- FPS ~75-155 during the heaviest load-in burst, ~165 steady — unchanged
  vs single thread (render path was already free; the pool buys light-side
  throughput + recheck latency, not render time).
- 90s runtime clean; all Rep tests pass.

## Later: sharding (only if throughput still lags)

Prefer region shards over more claim tuning: K light threads each own a
fixed set of (x,z) column ranges; a mirror write into a foreign chunk
becomes a tiny "set cell" message to the owning shard; BFS seeds crossing a
border get posted to the owner's queue — which also parallelizes the BFS
itself, the current serial bottleneck.

## Also pending

- Strip/gate the TEMP diagnostics: LIGHT probe print in RepGame::draw,
  light_dbg_* counters, per-phase chrono (esp. the two chrono calls inside
  Chunk::draw), FPS us/frame line.
- world_draw steady-state is ~5.3ms vs master ~3.5ms: ~1.3ms is per-chunk
  glBindTexture(GL_TEXTURE_3D)+u_LightBase per pass. Real fix is a 3D light
  texture atlas (bind once per pass) -- significant refactor.
- light_bind/u_LightTex: glActiveTexture hoisted per render-order already.

## Status: implemented (single thread)

Done on branch `lighting`:

- `LIGHT_ON_THREAD` in light.hpp: 1 on native, 0 on WASM-no-pthreads.
- `ChunkLoader::light_thread_start/stop/loop` + `light_submit_finalize` /
  `light_submit_recheck` in light.cpp. One mutex+cv (`light_work_mutex`)
  guards jobs, pending list, dirty-upload list. Statics are file-scope —
  ChunkLoader is effectively a singleton.
- Render loop: `light_submit_finalize` replaces inline finalize;
  `light_drain_pending`/`light_process_queue` only run when
  `light_async_active()` is false (tests/WASM inline fallback preserved).
- `world.cpp` edits enqueue `LIGHT_JOB_RECHECK` via `light_submit_recheck`.
- Worker optimistic prefill: `light_fill_chunk` sets `sky_open_above=1` and
  writes sky=15 from `fill_from` up so chunks never draw black between mesh
  upload and light finalize; the cascade corrects wrong assumptions via the
  flag-flip removal path.
- Loop order per wake: drain pending -> jobs -> process_queue(2ms slice).
  Predicate also wakes on non-empty BFS queues (they're light-thread-owned).
- `cleanup()` joins the light thread BEFORE `chunk.destroy()` frees volumes.

Bugs found and fixed during bring-up:

- light_upload_dirty re-queued unprocessed chunks into light_dirty_list
  WITHOUT the mutex -> concurrent vector push_back corrupted the heap
  (malloc(): unaligned tcache chunk detected on the light thread).
- Mirror writes via cached Chunk* (light_set_nb, border sync pass 2) used a
  chunk_pos-derived local -> torn chunk_pos on slot reuse indexed light[]
  OOB. All sites now validate with light_local_ok().
- Same torn-local guard added to light_get/light_block_id_at (render-side
  probe calls these).

Measured (legacy terrain, ~2s probe intervals):

- fin (dequeue+submit on render thread): ~50us/frame, was ~14ms.
- Load-in FPS ~155-165 (was ~50-75 pre-thread, ~250 on master).
- world_draw remains ~5.3ms/frame: ~1.3ms is per-chunk 3D-texture binds.
- Light BFS drain ~340k seeds/s; mass load-in pushes a multi-M backlog that
  converges in the background (visual border seams resolve late during
  bursts).
