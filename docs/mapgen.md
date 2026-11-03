# Terrain generation (mapgen)

How ungenerated chunks get their blocks: noise fields → per-column context →
per-cell block pick → structures. Everything is a pure function of world
coordinates and `MAP_SEED`, so terrain is identical on every client, every
backend, and every run — the dedicated server never generates terrain, it
only stores block diffs (`ServerLogic::persist_dirty_blocks`). If two
clients run different generators they see different worlds for the same
seed.

## The pipeline

`Chunk::load_terrain` (`src/common/chunk.cpp`) runs on the
`TerrainLoadingThread` worker pool:

1. `map_storage.load_chunk` — a saved chunk file always wins; generation
   only fills chunks that have never been persisted. Generated chunks are
   marked `dirty = 0`, so they are not written back unless edited.
2. Block-world / flat-world bypass (`repgame_block_world`,
   `repgame_flat_world`) — O(1) layouts, see "Other world shapes" below.
3. **Empty-chunk early-out**: above `WATER_LEVEL`, a chunk whose floor is
   past `MapGen::maxTerrainHeight() + MAPGEN_MAX_TREE_REACH` is all air with
   zero noise evaluation; a second tier samples
   `MapGen::calculateTerrainHeight` per column and compares against
   `+ OVERHANG_MAX_RISE + MAPGEN_MAX_TREE_REACH`. These bounds must stay
   true — see "Bounds contracts" below.
4. **Mapgen backend** fills `chunk->cells` — the full
   `CHUNK_SIZE_INTERNAL_*` array, i.e. the 62³ interior plus a ±1 border
   halo so edge meshing and lighting see neighbor blocks. All cells are
   written in canonical state (waterlogged bit 0); `Chunk::set_block`
   applies waterlogging rules to later writes.
5. `StructureGen::place` — CPU-only pass that stamps trees, features,
   decorations and cave crystals over the generated terrain.
6. Chest/mob sidecar loads, natural mob spawns, `light_fill_chunk`.

What happens to a chunk around this call — window shifts, finalize,
light, meshing, parking — is the loader lifecycle in `docs/chunks.md`;
the light pass in step 6 is expanded in `docs/lighting.md`.

`TERRAIN_GEN_PROFILING` (constants.hpp) prints per-stage timings every 100
chunks.

## Picking a generator

`REPGAME_MAP_GEN_LEGACY` (`include/common/map_gen_consts.hpp`) selects the
generator at compile time for **every** backend:

- `1` (the `#ifndef` default) — the legacy generator: the pre-overhaul code
  as it existed at commit `101a348`, fields in
  `include/common/map_gen_fields_legacy.hpp`, structures in
  `src/common/structure_gen_legacy.cpp`.
- `0` — the reworked generator: fields in
  `include/common/map_gen_fields.hpp` (noodly caves, overhangs, biomes,
  floating islands, lava layer, geodes), structures in
  `src/common/structure_gen.cpp`.

There is no make flag for it — flip the `#define` in `map_gen_consts.hpp`
or add `-DREPGAME_MAP_GEN_LEGACY=0` to the platform `CFLAGS_*` in
`makefiles/*.mk` (a command-line `CFLAGS_LINUX=...` would replace, not
append). Only one generator is compiled per binary — `map_gen_fields.hpp`
`#include`s the legacy file under `#if REPGAME_MAP_GEN_LEGACY` — so A/B
comparisons need two builds. `MAP_SEED` (currently `0`) is the global seed;
each noise field adds a fixed offset to it (seed maps are commented in
`map_gen_fields.hpp` — pick an unused offset for new fields).

## One source, four backends

The field math is written once in the C++/WGSL dual dialect
(`include/common/wgsl_dialect.hpp`) and compiled into every backend, so
worlds are identical by construction (modulo float rounding):

| Piece | File | Role |
|---|---|---|
| Dialect macros | `include/common/wgsl_dialect.hpp` | `MG_FN*`, `MG_LET/VAR/DECL`, `MG_F32/I32`, `MG_TERN`, `MG_ABS`… expand to C++ or WGSL depending on `REPGAME_WGSL_GEN` |
| Noise | `include/common/perlin_noise_shared.hpp` | 256-entry permutation hash + smooth-interpolated value noise; `perlin_noise2d/3d` return `[0, 1]` |
| World constants | `include/common/map_gen_consts.hpp` | `REPGAME_MAP_GEN_LEGACY`, `MAP_SEED`, layout constants, `MapGenColumn` — pure preprocessor so the WGSL pass sees it |
| Fields + block picker | `include/common/map_gen_fields{,_legacy}.hpp` | The generator itself; includer defines `MG_QUAL` and `MAP_GEN_PERLIN2D/3D` |
| Per-cell glue | `include/common/map_logic.hpp` | `#include`d inside each backend's per-y loop; produces `finalBlockId` from `col` (new) or `terrainHeight` (legacy) |

The backends share the same shape: one thread/invocation per (x, z) column
computes the 2D context once (`mgl_base_height` or `mg_column_info`), then a
per-y loop runs `map_logic.hpp`:

- **CPU** — `MapGen::load_block_c` (`src/common/map_gen.cpp`). Always
  available; also the per-chunk fallback for the GPU paths.
- **CUDA** — `src/linux/cuda/map_gen.cu` under `REPGAME_BUILD_WITH_CUDA`.
- **HIP** — `src/linux/hip/map_gen.hip` under `REPGAME_BUILD_WITH_HIP`;
  `thread_local` device buffer, returns `false` on failure so the chunk
  falls back to C. `REPGAME_NO_HIP` kills all HIP work,
  `REPGAME_NO_HIP_MAPGEN` only the mapgen half.
- **WebGPU (WASM only)** — `src/shaders/map_gen.wgsl` is a *template*: the
  build runs it through `SHADER_PP -DREPGAME_WGSL_GEN` (`wasm.mk`'s `%.wgsl`
  rule, `gcc -E -P -x assembler-with-cpp`)
  so the same dialect headers emit a compute shader, plus `block_ids.inc`
  generated by `tools/gen_wgsl_block_ids.awk`. Host shim:
  `src/wasm/webgpu_mapgen.cpp` (async adapter/device/pipeline chain pumped
  each frame from `main_loop_wasm`; `false` → per-chunk C fallback). Opt-in
  via `REPGAME_WEBGPU=1`; `REPGAME_NO_WEBGPU_MAPGEN` disables it.
  `MAPGEN_VERIFY=1` runs CPU alongside WebGPU and diffs chunk cells.

Dispatch order in `load_terrain` is CUDA → HIP → WebGPU → C; the debug
overlay shows which backend won (`imgui_overlay.cpp`).

## World layout

Constants live in `map_gen_consts.hpp` (`WATER_LEVEL`, `BEDROCK_LEVEL`,
`LAVA_LEVEL`, `SNOW_LINE`, `MOUNTAIN_ROCK_LINE`, `MAX_MOUNTAIN_HEIGHT`,
`MESA_MAX_RISE`, `CANYON_MAX_DEPTH`, `OVERHANG_MAX_RISE`, `SURFACE_BAND`)
and tree-reach bounds in `map_gen.hpp` (`MAPGEN_TREE_CELL`,
`MAPGEN_MAX_CANOPY_RADIUS`, `MAPGEN_MAX_TREE_REACH`). Change any of these
and the terrain changes on every backend at once — that is the point of the
single-source dialect.

## Legacy generator (`REPGAME_MAP_GEN_LEGACY=1`)

`mgl_base_height` sums four transforms of 2D noise — broad `level`,
cubed `mountains`, `hills`, small `ground` — and `mgl_pick_block` decides
each cell: a dirt surface band with sand/gravel shores and mountain caps
that thin to stone/snow above `LEGACY_MOUNTAIN_CAP_HEIGHT`; squared-perlin
ore rolls (iron/coal/gold) deep underground; a single 3D field caves
out blocks below a depth-gated density (`LEGACY_CAVE_THRESHOLD`), hard
bedrock at `y <= -100`, water under `WATER_LEVEL`.

Legacy structures (`structure_gen_legacy.cpp`, `StructureGen::place_legacy`)
are the pre-overhaul forests/trees/decorations: per-column tree rolls
(seed-offset noise → tree types 1–6), and trees are no longer clipped at
chunk borders — every chunk re-derives all trees whose parts could land in
it from pure world-coord rolls and writes the cells it owns
(`sgl_tree_suppressed` local-max rule). ~1 in 100 trees gets a glowstone
top log ("Etho trees"). `FOREST_DEBUG` paints every column as a tree for
debugging.

## New generator (`REPGAME_MAP_GEN_LEGACY=0`)

Two stages: each column first computes a `MapGenColumn` via
`mg_column_info` (cached context the per-y loop reuses), then
`mg_pick_block` decides each cell.

**Heightmap** (`mg_base_height`): continental `level` + `rolling` + `hills`
+ `ground`, plus masked domain-warped ridged mountains
(`mg_mountain_mask` × `mg_mountain_ridge` × `MAX_MOUNTAIN_HEIGHT` — ranges,
not uniform bumps), plus quantized mesa terraces on desert columns
(`mg_desert`, `mg_mesa_riser`), minus slot canyons (a narrow band around a
low-frequency field midline, patch-gated, flooding below sea level instead
of trenching to bedrock).

**Solidity** (`mg_solid_at`): below the heightmap, soft noise notches the
surface inward; above it, ridged 3D noise builds overhang sheets scaled by
`col.rough_gain` (plains stay smooth, mountains get cliffs/arches), capped
at `OVERHANG_MAX_RISE`. Where `col.island > 0`, a warped footprint field
grows domed floating islands in a jittered band above the terrain.

**Caves** (`mg_cave_carve`): `mg_channel_dist` measures *physical* distance
to a noise-channel centerline by dividing by the field's forward-difference
gradient — that normalization is what keeps tubes a constant width instead
of pinching to specks. Two channel pairs (wide mains, thin branches), a
bounded 3D zone mask, cheese rooms near the lava layer, rare mega-caverns,
and a surface gate so most caves stay underground.

**Block pick** (`mg_pick_block`): jittered bedrock top → air/water →
exposed-top vs. underlayer surface pick (`mg_surface_block`: ocean floor
(muddy inside swamp gates), beach sand over sandstone, desert red-sand over
red-sandstone with stone mesa risers, pale-garden moss, scree under cliffs,
snowline, `MOUNTAIN_ROCK_LINE`, mycelium/podzol/snowy variants, island
meadows) → deep pick (`mg_deep_block`: dirt/gravel pockets + depth-banded
coal/copper/iron/lapis/redstone/diamond/gold, plus emerald veins under
mountains). Cliff-face magma veins (`col.steepness` gated) light
overhangs at night; cave-carved space is `LAVA` at/below `LAVA_LEVEL`;
deep rare geodes get a hollow heart in a basalt→calcite→amethyst shell
(with budding flecks and amethyst-cluster floor buds).

**Structures** (`StructureGen::place`, `src/common/structure_gen.cpp`):
`place_trees` → `place_features` → `place_decorations` → `place_crystals`,
all pure functions of world coords:

- *Trees*: the world is gridded into `MAPGEN_TREE_CELL` cells each rolling
  at most one tree; biome/weird pick density and species (oak/birch blobs,
  spruce pines, dark-oak forest, acacias, jungle incl. rare 2×2 giants,
  cherry groves, pale-oak pale gardens, coastal mangroves rooted in
  underwater mud (seabeds down to ~4 under the waterline; tall stilt trunks
  keep the canopy above it), giant mushrooms on mycelium), a clearing field modulates
  groves. Species can hang things under the canopy rim — pale hanging moss,
  jungle vines, mangrove propagules — and oak/birch trunks rarely get bee
  nests. Each chunk re-derives every tree whose canopy can reach it
  (`sg_tree_for_cell`, `sg_find_surface_y` — surface found via `mg_solid_at`
  + `mg_pick_block` on the column's exact solid-reach bound, not the chunk's
  array, and "no surface" is `SG_NO_SURFACE` since real seabeds legitimately
  sit at negative y); canopy scatter hashes are keyed on world coords so
  border-crossing trees match on both sides.
- *Features*: `SG_FEATURE_CELL`-gridded landmarks — crystal spire clusters,
  packed-ice spike clusters (cold land), volcanic cinder-cone vents (lava
  crater + scorched ground), beacon monoliths, icebergs (cold open ocean) —
  claimed per-column by tallest contribution (`sg_column_feature`).
- *Decorations*: per-column surface scatter — flowers/tufts/ferns,
  sunflower/lilac/peony/rose flower fields, flowering azalea in cherry
  groves, pale moss carpet in pale gardens, jungle-edge bamboo, reeds,
  desert cacti (some flowered), pumpkin/melon patches, mushrooms and ferns
  on podzol/mycelium, mangrove propagules on mud, pinecones, snow-layer
  drifts + rare powder snow in the cold, deadbushes, saplings; shallow
  seabeds get coral/kelp/seagrass (fully submerged cells only, with extra
  headroom required for the tall quads; swamp water stays murky — sparse
  seagrass/tufts, no coral), denser inside reef patches, and
  cold-biome water surfaces freeze over in patchy ice sheets. Scatter is written into
  the ±1 halo too, so edge cells mirror exactly.
- *Cave crystals*: stalactites/stalagmites anchored to cave roofs and
  floors (and island undersides), occasionally glowstone-tipped, clustered
  in 16-block neighborhoods.

## Bounds contracts

Three `MapGen` helpers exist for code that needs terrain without a chunk:

- `calculateTerrainHeight(x, z)` — the 2D heightmap only.
- `maxTerrainHeight()` — exact max of every heightmap component, computed
  once by sweeping each pure `*_transform` over `[0, 1]` (no noise
  sampling). The empty-chunk skip relies on
  `maxTerrainHeight + OVERHANG_MAX_RISE + MAPGEN_MAX_TREE_REACH` bounding
  all solid blocks **and** treetops; raising any generator term without
  the matching constant silently clips terrain or breaks the early-out.
- `gen_block_id(x, y, z)` — CPU evaluation of one generated cell (terrain
  only, no structures). Used by spawn placement (`game_init.cpp` /
  `game_tick.cpp` `spawn_pending` rescan) and by structure-gen surface and
  collision scans. The full pipeline (`MapGen::load_block_c` +
  `StructureGen::place`) is also re-run on demand by inverse-TNT blasts,
  which restore every cell in the sphere to its natural state
  (`TntExplodeEvent`).

`test_mapgen` in `src/common/tests/test_world.cpp` (`make tests`) enforces
the invariants: nothing solid above the bound, gapless bedrock, plus
new-gen-only checks (lava layer, overhangs, snowy surfaces).

## Other world shapes

Two non-terrain generators bypass the above entirely in `load_terrain`:

- **Block world** — `REPGAME_BLOCK_WORLD=1`, `MapGen::block_world_id`:
  sandstone floor, every defined block on a 32-wide grid, plus door/fence
  showcases in z<0. See docs/blocks.md.
- **Flat world** — save dir with `world_type` = `flat`,
  `MapGen::flat_world_id`: sandstone at y≤0, air above (voxelized-model
  worlds). See docs/models.md.

## Editing checklist

- Write fields in the dialect (`MG_*` macros): no implicit casts, no
  ternary, no shadowing, immutable params — or the WGSL build breaks.
  `MG_TERN` evaluates both branches, so both sides must be unconditionally
  safe.
- Any field edit retunes all four backends identically; the WGSL rebuild is
  driven by `wasm.mk` deps, no manual shader step.
- Height increases must keep `maxTerrainHeight`'s swept bound (and
  `OVERHANG_MAX_RISE` for 3D additions) true — both the empty-chunk skip
  and island/tree clipping depend on it.
- New noise fields need an unused `MAP_SEED + n` offset — the seed maps in
  `map_gen_fields.hpp` list what's taken.
- Structure code must stay a pure function of world coords: neighboring
  chunks re-derive the same rolls to write their half of border-crossing
  pieces.
- Verify with `make tests` (`test_mapgen`); on WASM, `MAPGEN_VERIFY=1`
  diffs CPU vs. WebGPU output per chunk.
