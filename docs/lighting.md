# Lighting

Per-chunk flood-fill lighting, independent of the mesh: each chunk owns a
`light[]` volume the fragment shader samples per face-adjacent cell, so
light changes never trigger a remesh. This doc covers the light model
and propagation algorithm; the worker/queue concurrency design lives in
`docs/lighting-thread-plan.md`, and the shader-side sampling tunables in
`docs/rendering.md`.

## Storage

`Chunk::light[]` (`include/common/light.hpp`,
`include/common/chunk.hpp`) is an `unsigned short` per cell over the
chunk's *internal* (halo-inclusive, `CHUNK_SIZE+2` per axis) layout —
same index space as `cells[]`, addressed by `light_index`. Each short
packs two byte channels: high byte skylight, low byte block light
(`light_get_sky`/`light_get_block`). Channels hold *raw* light units,
0–255 (`LIGHT_RAW_MAX`), with `LIGHT_RAW_PER_LEVEL` (17) raw units per
display level so `LIGHT_MAX_LEVEL` (15) still describes the shader-facing
range — 15 levels × 17 = 255. The sub-level granularity is what the
chamfer propagation (below) needs: weighted step costs of 17/24/29 raw
units wouldn't fit in a 4-bit nibble.

A uniform all-zero volume is folded to `Chunk::light_uniform` instead of
an allocation; `light[]` materializes on the first nonuniform write. The
volume uploads to a per-chunk 3D texture — flat `GL_RG8` where each raw
byte channel passes straight through (`light_expand_cell`), or a
mipmapped RGBA8 mode (`light_mipmaps_enabled`, `REPGAME_LIGHT_MIPMAPS`)
that additionally stores the lit-cell fraction for footprint-LOD
sampling. `light_air_mask`/`light_shade_mask` are bit-caches of
"cell admits light" / "cell is a light floor" the upload reads instead
of walking `cells[]`.

On top of the raw field, `light_fill_scratch_smooth` can smooth *only
the uploaded copy* — `Chunk::light[]` stays exact — with a 3x3x3
`[1,2,1]`-profile blur in raw units. Neighbour gating mirrors flood
connectivity (`light_ctx_link`): non-air cells never contribute, edge
diagonals need an open shared-face path (no corner leaks), and a light
floor seals its vertical adjacencies. Maxed channels stay maxed so
emitters and sun shafts aren't dimmed. Dirty sub-box uploads grow by
the 1-cell footprint. This is the pause-menu "Shadows" setting
(`input.shadow_quality` — `SHADOW_SMOOTH`/`SHADOW_FAST`, persisted in
player.dat; toggling re-uploads every live texture via
`ChunkLoader::light_shadow_refresh_all`). `REPGAME_LIGHT_SMOOTH=0|1`
forces the mode and ignores the menu.

Under "Smooth" the blur only runs on chunks within
`REPGAME_LIGHT_SMOOTH_RADIUS` chunks of the camera
(`light_smooth_in_radius`); farther chunks upload the raw field —
the smoothing delta is sub-perceptual at that distance and streamed-in
chunks stay cheap. Mixed-mode borders stay byte-exact: a halo texel
delegates to its *owner's* presentation mode, so a smoothed chunk
bordering a raw one computes the raw byte for shared texels
(`raw_owners`), and a raw chunk bordering a smoothed one still delegates
its halo texels to the smoothed interior. Each texture records the mode
it was last fully filled under (`light_tex_mode`); on drift — toggle or
radius crossing — `light_ensure_texture`/`light_upload_dirty` upgrade the
next upload to a full-volume refill, so no texture ever mixes modes
mid-volume. A flip isn't a field change, so the mirror-mark walk never
fires for it: `light_remark_owned_halos` separately re-marks the ring
slabs the flipped chunk owns inside its neighbours' volumes.

Borders need two extra mechanisms. The blur footprint is one cell, but
texels ON the volume edge (the halo ring) mirror a neighbour's interior
cell and would need a second ring past the border; instead they delegate
to the owning chunk's predicate bundle (`light_ctx_smooth_cell`'s `nb`
table, filled by `Chunk::light_nb_ctxs`), so both chunks' textures store
the identical smoothed byte — no seam by construction. And since a
texel's uploaded value now depends on cells around it, an interior light
change must re-upload those texels in every chunk that stores them:
`light_mark_dirty_now` marks the mirror copies of border cells held by
neighbouring chunks on top of the owning cell's own mark — but only for
positions whose owner smoothes (raw owners' texels don't depend on the
footprint).

Per-column skylight metadata lives in `chunk->light_columns`, three
`CHUNK_SIZE_X*Z` planes indexed by `LIGHT_COLUMN_INDEX`: `column_open`
(no light-blocking cell in this chunk's column), `sky_open_above` (no
blocker anywhere above in the loaded grid), and `fill_from` — the first
y the sky cascade fills, so the fill needs no per-cell opacity reads.

## Values that drive it

From `Block` (`include/common/block_definitions.hpp`): `emits_light`
seeds the block channel (in display levels, scaled ×17 to raw);
`light_absorb` is extra attenuation per cell (also display levels,
charged per step entry); opacity (`light_opaque_id`) blocks both
channels; `blocks_light_down` (slabs, path blocks) holds light *in* the
cell but stops direct vertical descent — the "light floor" case, which
also seals the downward hop of any diagonal route through it.

## Propagation

Two work functions, both 26-direction chamfer BFS over `ChunkLoader`'s
queues (`src/common/light.cpp`). `light_step_dirs` lists the step set: 6
face steps at cost 17 (one level), 12 edge diagonals at 24 (≈√2·17), and
8 corner diagonals at 29 (≈√3·17). The weighted metric turns shadow
contours from Manhattan diamonds into near-circles and — because the raw
values rarely land on whole display levels — dissolves the 16-step
quantization banding without any presentation smoothing. A diagonal step
is additionally gated (`light_diag_open`): at least one shared-face cell
must be non-opaque and neither hop may be a downward step sealed by a
`blocks_light_down` cell, so light can't cut corners or squeeze between
two opaque cells touching diagonally.

- **Add** (`light_add_seed`/queue): a cell offers
  `v - step_cost - absorb*17` (`light_offered`) to each reachable
  non-opaque neighbour; higher offers win, lower ones are dropped.
  One exception: skylight moving straight *down* at full strength keeps
  255 (sun shafts don't attenuate vertically) through non-absorbing
  cells. Emitters re-seed themselves so their emission survives any
  recomputation.
- **Remove** (`light_push_remove_seed`): when a value drops, the old
  value propagates a removal along the same 26 directions that erases
  everything it previously lit, then neighbours re-seed adds for light
  that flows back in. This is what makes "break the torch" converge
  instead of leaving ghosts. Removal checks no floor/gate conditions —
  a cell's current block isn't what it was while it held the old value,
  so the cascade clears whatever the old value could have carried and
  relies on re-seeding to converge.

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
