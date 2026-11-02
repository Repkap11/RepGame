# Lighting

Per-chunk flood-fill lighting, independent of the mesh: each chunk owns a
`light[]` volume the fragment shader samples per face-adjacent cell, so
light changes never trigger a remesh. This doc covers the light model
and propagation algorithm; the worker/queue concurrency design lives in
`docs/lighting-thread-plan.md`, and the shader-side sampling tunables in
`docs/rendering.md`.

## Storage

`Chunk::light[]` (`include/common/light.hpp`,
`include/common/chunk.hpp`) is a byte per cell over the chunk's
*internal* (halo-inclusive, `CHUNK_SIZE+2` per axis) layout — same index
space as `cells[]`, addressed by `light_index`. Each byte packs two
4-bit channels: high nibble skylight, low nibble block light, 0–15 each
(`light_get_sky`/`light_get_block`, `LIGHT_MAX_LEVEL`).

A uniform all-zero volume is folded to `Chunk::light_uniform` instead of
an allocation; `light[]` materializes on the first nonuniform write. The
volume uploads to a per-chunk 3D texture — flat `GL_RG8` with each nibble
expanded to its own byte (`light_expand_cell`), or a mipmapped RGBA8 mode
(`light_mipmaps_enabled`, `REPGAME_LIGHT_MIPMAPS`) that additionally
stores the lit-cell fraction for footprint-LOD sampling. `light_air_mask`/`light_shade_mask` are bit-caches of
"cell admits light" / "cell is a light floor" the upload reads instead
of walking `cells[]`.

Per-column skylight metadata lives in `chunk->light_columns`, three
`CHUNK_SIZE_X*Z` planes indexed by `LIGHT_COLUMN_INDEX`: `column_open`
(no light-blocking cell in this chunk's column), `sky_open_above` (no
blocker anywhere above in the loaded grid), and `fill_from` — the first
y the sky cascade fills, so the fill needs no per-cell opacity reads.

## Values that drive it

From `Block` (`include/common/block_definitions.hpp`): `emits_light`
seeds the block channel; `light_absorb` is extra attenuation per cell;
opacity (`light_opaque_id`) blocks both channels; `blocks_light_down`
(slabs, path blocks) holds light *in* the cell but stops direct vertical
descent — the "light floor" case.

## Propagation

Two work functions, both six-direction BFS over `ChunkLoader`'s queues
(`src/common/light.cpp`):

- **Add** (`light_add_seed`/queue): a cell offers `v - 1 - absorb` to
  each non-opaque neighbour; higher offers win, lower ones are dropped.
  One exception: skylight moving straight *down* at full strength keeps
  15 (sun shafts don't attenuate vertically). Emitters re-seed
  themselves so their emission survives any recomputation.
- **Remove** (`light_push_remove_seed`): when a value drops, the old
  value propagates a removal that erases everything it previously lit,
  then neighbours re-seed adds for light that flows back in. This is
  what makes "break the torch" converge instead of leaving ghosts.

Cross-chunk spread works because each chunk stores the first ring of its
neighbours in its halo: `light_border_sync` copies boundary values both
ways and seeds whatever direction changed. Edits go through
`ChunkLoader::light_recheck_block` (queued by `World::set_loaded_block`
as `light_submit_recheck`): it updates `column_open` (a sky column
opening/closing fires `light_cascade_column`), resets the cell's own
floor (emission or 0), pushes a remove seed if it dropped, then re-seeds
from all neighbours in both channels. `light_defer_marks` batches the
dirty marks so the emit cell uploads together with its spread.

## Skylight columns

Skylight is resolved column-by-column at finalize, not by BFS alone:
`light_fill_chunk` (run on the terrain worker inside
`Chunk::load_terrain`) zeroes the volume, BFS-fills emitters on the
block channel, and computes `column_open`/`fill_from` per (x,z).
`ChunkLoader::light_finalize_chunk` then runs `light_cascade_columns`:
each `sky_open_above` column directly fills sky=15 from `fill_from`
downward, `light_seed_interior_boundary` seeds BFS for cells the direct
fill can't reach (caves, overhang lips, `blocks_light_down` floors —
which hold sky=0 but *are* lit, so the cell above their cap is pushed
explicitly), and `light_border_sync` stitches chunk edges.

`fill_from`/`sky_open_above` are finalized only once neighbouring chunks
exist; a chunk finalized early with optimistic assumptions is flagged
and later re-finalized ("healed", `light_heal_chunk`, bounded by
`LIGHT_HEAL_PER_SHIFT` per grid shift plus `light_heal_idle`).

## Execution model

`LIGHT_ON_THREAD`: on native builds a lighting thread runs
finalize/propagation out of `ChunkLoader`'s job lanes (near/far
priorities, claims via `light_claim`, backlog starvation handling);
without it (WASM without pthreads, tests) the same code runs inline from
`render_chunks` — `light_drain_pending` + `light_process_queue` under a
per-frame budget scaled by backlog. All mutation is single-writer either
way; the render thread only owns GL.

Uploads are render-thread work: `light_mark_dirty` accumulates a dirty
sub-box (`light_dirty_box`, CAS'd so workers can mark concurrently) and
`light_upload_dirty` uploads at most `LIGHT_MAX_UPLOADS_PER_FRAME`
volumes per frame, debounced per chunk (`light_write_tick`,
`light_upload_defers`) so a busy region coalesces into fewer
`glTexSubImage3D` calls. `light_ensure_texture` allocates + full-uploads
on first draw — eager at finalize for drawable chunks so the cost isn't
bunched into the first camera pan.

## Day/night and tuning

`light_daylight_factor(world_time)` maps `state.world_time`
(`DAY_LENGTH_TICKS` per day, 0 = dawn, quarter = noon) to a 0.04..1.0
scale the shaders multiply skylight by; `REPGAME_MIN_AMBIENT` /
`REPGAME_NIGHT_AMBIENT` are the absolute floor and the daylight-factor
floor (imgui overlay + pause menu adjustables, persisted in
`player.dat`). Worker count and claim radius:
`REPGAME_LIGHT_WORKERS`, `REPGAME_LIGHT_CLAIM_RADIUS`. Debug counters
(`light_dbg_*`) feed the `loader` section of `ImGuiDebugVars` and
`ChunkLoader::LoaderDebugStats`.
