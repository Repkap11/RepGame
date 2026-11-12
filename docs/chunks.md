# Chunks & streaming

`ChunkLoader` (`include/common/chunk_loader.hpp`,
`src/common/chunk_loader.cpp`) owns the fixed-size window of `Chunk`s
around the camera and drives the whole lifecycle: load → populate →
light → mesh → draw → edit → park. Terrain generation itself is
`docs/mapgen.md`; the render pipeline consuming the meshes is
`docs/rendering.md`. This doc is about the lifecycle and the queues.

`Chunk` groups its per-subsystem state into three plain sub-structs —
`chunk.light` (`ChunkLight`: light volume, masks, dirty/stale/staging
state), `chunk.sidecars` (`ChunkSidecars`: mob/item sidecar staging,
`player_edited`), and `chunk.containers` (`ChunkContainers`: chest and
furnace maps) — keeping block storage, mesh/GL state and lifecycle
fields on `Chunk` itself.

## The torus

`chunkArray` is `MAX_LOADED_CHUNKS` slots — a compile-time box of
`2*CHUNK_RADIUS_XZ+1` × `2*CHUNK_RADIUS_Y+1` × `2*CHUNK_RADIUS_XZ+1`
chunks centered on the camera chunk. A world position maps to exactly
one slot by modular arithmetic (`mod`, `chunk_slot_from_pos`,
`chunk.chunk_mod` records the slot's phase); `get_chunk` returns the
slot only when `chunk_pos` matches — a stale slot is a miss, not a
wrong-chunk hit.

The *active render window* is smaller: `render_radius` on x/z (runtime
adjustable, `set_render_distance`, clamped to `RENDER_RADIUS_MAX`) and
the full compile-time y range. Slots outside the window park instead of
loading — see "Parking".

## States

`Chunk::load_state` has three values:

- `CHUNK_LOADING` — a terrain job is queued or running. Excluded from
  drawing, random ticks, and the light paths.
- `CHUNK_LIVE` (0) — blocks are final and the chunk participates in
  everything.
- `CHUNK_PARKED` — out of window; holds no terrain data (see below).

## The pipeline

Per frame, `ChunkLoader::render_chunks` (from `World::render`):

1. **Queue.** Window shifts (`chunk_center != camera chunk`, or radius
   changed) walk every slot through `reload_if_out_of_bounds`: slots
   whose nearest position moved enqueue on `terrain_loading_thread`;
   out-of-window slots park. New positions batch into one
   `CHUNK_DIFF_REQUEST` box (`docs/multiplayer.md`).
2. **Terrain work.** `TerrainLoadingThread`
   (`include/common/utils/terrain_loading_thread.hpp`) workers pop jobs,
   retarget stale requests at the current `set_focus` center, persist
   departing dirty slots, and run `Chunk::load_terrain`: mapgen +
   structure gen always runs first, then the chunk file's saved edit
   runs are overlaid on the generated cells (`docs/persistence.md`),
   with all-air chunks folded to null `cells` (`is_empty_chunk`)
   instead of a 34³ array. `load_terrain` also runs the CPU mesh here on
   the worker (`calculate_populated_blocks` → `build_mesh`).
3. **Finalize** (`finalize_loaded_chunk`, render thread, capped by
   `CHUNK_FINALIZE_BUDGET_US` within the shared frame-work pool below):
   publish `CHUNK_LIVE`, lazy GL init,
   `process_chunk_position` (the 6-bit face-visibility mask that decides
   which chunk faces can be seen from the current center →
   `calculate_sides`), apply queued network diffs
   (`apply_pending_diffs`), `light_submit_finalize`
   (`docs/lighting.md`), upload the worker-built mesh
   (`program_terrain`), eager `light_ensure_texture`, then push
   to `just_finalized` for `MobManager`/`ItemManager` adoption
   (`World::render`). When pending diffs changed cells after the worker
   meshed (`needs_repopulation` set), the stale mesh is discarded
   instead and the chunk defers to an async remesh job — see "Async
   server-diff remesh" below.
4. **Draw.** `rebuild_drawable_list` collects live `should_render`
   chunks; `calculate_cull` does frustum culling plus hardware occlusion
   probes (`REPGAME_OCCLUSION` kill switch, cached in
   `cached_cull_normal`/`cached_cull_reflect`); `draw` issues the
   instanced draws. Occlusion verdicts (`ChunkOcclState`) are invalidated
   by any camera move; a pure rotation only invalidates verdicts earned
   while the chunk wasn't fully inside the frustum — a full-box probe is
   rotation-invariant from a fixed eye position.
5. **Edits.** A block write (`World::set_loaded_block`) dirties the
   chunk (`dirty`, `needs_repopulation`), marks the cell in
   `chunk.edited` (the bitset `MapStorage::persist_chunk` writes as edit
   runs — generation and structure writes never set it), and enqueues it on
   `repop_must` — the must-remesh queue that also pulls dirty
   face-neighbors in via `remesh_group`, so a boundary edit never leaves
   a see-through gap while its neighbour's mesh waits for the budget.
   Must-queue drains under `REPOP_MUST_BUDGET_US`; flag-only backlog
   under `CHUNK_REMESH_BUDGET_US` — both clipped by the shared pool.

## Shared frame-work pool

The streaming phases at the top of `render_chunks` — finalize dequeue,
async remesh submit/publish, `repop_must` drain, the flagged remesh scan,
the inline light-BFS fallback, and `light_upload_dirty` — used to each
hold an independent wall-clock cap (3/2/2/8/2/1.2 ms), so a chunk-border
crossing could stack ~18 ms of update work into one frame. They now draw
from one pool measured from the top of `render_chunks`: each phase gets
`min(its own cap, pool remaining)`, and pool-empty phases still run their
one-unit minimums (at least one finalize/component/publish/remesh/upload
slice — `LIGHT_UPLOAD_FLOOR_US` — so nothing starves outright).

The pool FLOATS with queue depth (`FRAME_WORK_MIN_US`,
`FRAME_WORK_MAX_US`, `FRAME_WORK_BACKLOG_US` in `chunk_loader.cpp`):
`min + backlog*per_item`, clamped to the ceiling. Backlog counts terrain
and mesh work+result queue depths, `repop_must`/`async_wanted` pending,
the previous frame's leftover flagged-remesh count
(`remesh_flag_backlog`, tallied during the scan itself), and the light
upload backlog (`light_upload_backlog`). Quiet play holds the pool near
the floor (~half a 240 Hz frame) so a border crossing stays invisible;
sprint flight or a CPU too slow to drain the queues widens it toward the
ceiling — throughput over smoothness only while the queues say the
machine is actually behind. Since the pool is a ceiling not a target, an
idle machine never spends what it doesn't need.

WASM without pthreads runs terrain inline inside `dequeue()` under
`WASM_LOAD_BUDGET_US` (`limit_render`), clipped by the same pool.

## Parking

When a slot leaves the window it *releases its data* — parking is not a
cache (`release_terrain` frees cells/light/mesh; a full torus of parked
chunks would overflow the wasm32 heap and make render distance an
unbounded cache elsewhere). A dirty live slot instead goes back through
the work queue with `persist=1` so `process_value` writes it to disk
before the dequeue-time bounds check parks it. The free itself is
deferred `PARK_RELEASE_DEFER_FRAMES` (`defer_release`, `parked_release`,
generation-countered) because a light worker may still be mid-job on
the buffers; a `malloc_trim` on glibc makes the RSS drop visible.
Re-entry is a full reload — the save or regenerating terrain.

`is_empty_chunk` chunks keep `cells == nullptr`; `should_render` is set
by `program_terrain` only when a mesh layer has instances, which is also
why an edit into an empty chunk sets it manually
(`World::set_loaded_block`).

## Networking

Diffs that arrive before a chunk is live queue in
`Multiplayer::pending_diffs` and apply at finalize (before light, so
lighting sees final blocks); `apply_diff_block` mirrors boundary cells
into loaded neighbours so both sides of a seam remesh together. A local
write to a non-live slot queues a synthetic diff the same way. Details:
`docs/multiplayer.md`.

## Async server-diff remesh

Server bulk diffs (`CHUNK_DIFF_RESULT` on live chunks, and
`apply_pending_diffs` at finalize) remesh on the terrain workers rather
than the render thread — walking back and forth over a chunk border
otherwise pays a full CPU remesh per edit re-application
(`REPGAME_ASYNC_REMESH` killswitch; force-off without worker threads,
e.g. WASM without pthreads). Own edits and `BLOCK_UPDATE` still remesh
synchronously via `repop_must`/`remesh_group`.

`Chunk::build_mesh` is the pure half of `calculate_populated_blocks`:
it reads a cells/extras/chunk_pos snapshot into thread-local scratch and
returns right-sized arrays in `ChunkMeshResult`, so several workers can
mesh concurrently.

Flow (`ChunkLoader::async_*`):

- **Mark.** `apply_diff_block` sets `async_wanted` on the diffed chunk
  and every live neighbour it mirrors into (replaces `repop_must`
  enqueueing on this path). A border diff's mirrors therefore share one
  mark set.
- **Links.** A mirror write that actually changes a live neighbour
  sets a symmetric link bit (`async_links`, one bit per 26-direction)
  on both chunks. Links — not adjacency — define which chunks must
  swap meshes together: two chunks only share a mesh edge if a diff
  write crossed their border, so interior-only edits in neighbouring
  chunks stay independent jobs.
- **Group + submit.** Each frame `render_chunks` floods the marked
  chunks into components over link edges only (a chunk with no links is
  a single-member job), snapshots cells/extras + `cell_version` per
  member (allocated on the render thread) and enqueues one `MeshJob`
  per component on the terrain workers, which pop mesh jobs ahead of
  terrain jobs. Seeds are processed nearest-first by squared chunk
  distance from `chunk_center`, and submission stops once it exceeds
  `CHUNK_REMESH_BUDGET_US` (always at least one component; unprocessed
  seeds are re-listed for the next frame — a component is never
  split). When a job publishes or a promotion group syncs, link bits
  between members — and toward partners that no longer need a remesh —
  are cleared in both directions.
- **Publish.** Finished jobs are drained non-blocking. A job is valid
  only if every member is still `CHUNK_LIVE`, at the same `chunk_pos`
  and at the snapshotted `cell_version`; it then publishes
  all-or-nothing — unprogram + move meshes into `layers[]` for every
  member first, then `program_terrain` + `light_ensure_texture` for all
  of them, so two chunks sharing an edited border always swap meshes in
  the same frame (no seam gaps). On a version mismatch the whole job is
  discarded and every member is re-marked; after 3 consecutive discards
  a member's dirty component promotes to a synchronous remesh right in
  the publish pass (never through the budgeted `repop_must` drain, which
  could split the component across frames).
- **Sync promotion.** The backlog scan skips async-marked/in-flight
  chunks, but if a synchronous remesh lands on one anyway (an own edit
  on a chunk with a pending server remesh), `remesh_now` first floods
  its whole link-connected async component and remeshes it in the same
  pass — the in-flight job's result then no-ops at publish since
  `needs_repopulation` is already clear.
- **Path 1 (finalize).** A finalize whose pending diffs changed cells
  unprograms the worker's now-stale mesh, holds `should_render` at 0 and
  async-marks the chunk — it stays `CHUNK_LIVE` but undrawn until its
  job publishes (which also creates the light texture that finalize
  skipped).

`cell_version` (`std::atomic<uint32_t>`, bumped relaxed in every
cell-writing path) is the staleness key; it is never reset, so slot
reuse can't alias an old job — `chunk_pos` in the job guards the
identity side anyway.

Job counters (`async_jobs_submitted`/`published`/`discarded`/
`promoted`, `async_in_flight_seqs`) feed `LoaderDebugStats` — ImGui
overlay `Remesh async:` line and the `loader` block of
`results.jsonl`. Snapshot and publish render-thread time counts into
`light_dbg_us_remesh`.

## Diagnostics

`ChunkLoader::LoaderDebugStats` (terrain queue depth, light
finalize/propagate/backlog, uploads, remesh counts,
`num_remeshed_this_frame`) feeds the `loader` section of the ImGui
overlay (`imgui_overlay_get_imgui_debug_vars`). `process_random_ticks`
runs the per-frame random-tick budget over live chunks.
