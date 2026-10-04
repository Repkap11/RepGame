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

## Later: sharding (only if load-in throughput still lags)

Prefer region shards over job claims: K light threads each own a fixed set of
(x,z) column ranges; a mirror write into a foreign chunk becomes a tiny
"set cell" message to the owning shard; BFS seeds crossing a border get posted
to the owner's queue. Race-free by construction. The single-thread job-queue
interface is designed so this is "run K instances", not a rewrite.

## Also pending

- Strip/gate the TEMP diagnostics: LIGHT probe print in RepGame::draw,
  light_dbg_* counters, per-phase chrono (esp. the two chrono calls inside
  Chunk::draw), FPS us/frame line.
- world_draw steady-state is ~5.3ms vs master ~3.5ms: ~1.3ms is per-chunk
  glBindTexture(GL_TEXTURE_3D)+u_LightBase per pass. Real fix is a 3D light
  texture atlas (bind once per pass) -- significant refactor.
- light_bind/u_LightTex: glActiveTexture hoisted per render-order already.
