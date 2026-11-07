# Chunks & streaming

`ChunkLoader` (`include/common/chunk_loader.hpp`,
`src/common/chunk_loader.cpp`) owns the fixed-size window of `Chunk`s
around the camera and drives the whole lifecycle: load → populate →
light → mesh → draw → edit → park. Terrain generation itself is
`docs/mapgen.md`; the render pipeline consuming the meshes is
`docs/rendering.md`. This doc is about the lifecycle and the queues.

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
   departing dirty slots, and run `Chunk::load_terrain`: save-file load
   (`docs/persistence.md`) or mapgen, with all-air chunks folded to null
   `cells` (`is_empty_chunk`) instead of a 34³ array.
3. **Finalize** (`finalize_loaded_chunk`, render thread, budgeted by
   `CHUNK_FINALIZE_BUDGET_US`): publish `CHUNK_LIVE`, lazy GL init,
   `process_chunk_position` (the 6-bit face-visibility mask that decides
   which chunk faces can be seen from the current center →
   `calculate_sides`), apply queued network diffs
   (`apply_pending_diffs`), `light_submit_finalize`
   (`docs/lighting.md`), CPU mesh (`calculate_populated_blocks`) +
   upload (`program_terrain`), eager `light_ensure_texture`, then push
   to `just_finalized` for `MobManager`/`ItemManager` adoption
   (`World::render`).
4. **Draw.** `rebuild_drawable_list` collects live `should_render`
   chunks; `calculate_cull` does frustum culling plus hardware occlusion
   probes (`REPGAME_OCCLUSION` kill switch, cached in
   `cached_cull_normal`/`cached_cull_reflect`); `draw` issues the
   instanced draws. Occlusion verdicts (`ChunkOcclState`) are invalidated
   by any camera move; a pure rotation only invalidates verdicts earned
   while the chunk wasn't fully inside the frustum — a full-box probe is
   rotation-invariant from a fixed eye position.
5. **Edits.** A block write (`World::set_loaded_block`) dirties the
   chunk (`dirty`, `needs_repopulation`) and enqueues it on
   `repop_must` — the must-remesh queue that also pulls dirty
   face-neighbors in via `remesh_group`, so a boundary edit never leaves
   a see-through gap while its neighbour's mesh waits for the budget.
   Must-queue drains under `REPOP_MUST_BUDGET_US`; flag-only backlog
   under `CHUNK_REMESH_BUDGET_US`.

WASM without pthreads runs terrain inline inside `dequeue()` under
`WASM_LOAD_BUDGET_US` (`limit_render`).

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

## Diagnostics

`ChunkLoader::LoaderDebugStats` (terrain queue depth, light
finalize/propagate/backlog, uploads, remesh counts,
`num_remeshed_this_frame`) feeds the `loader` section of the ImGui
overlay (`imgui_overlay_get_imgui_debug_vars`). `process_random_ticks`
runs the per-frame random-tick budget over live chunks.
