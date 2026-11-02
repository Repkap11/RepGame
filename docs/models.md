# Models → worlds (flat worlds + voxelizer)

Turn a 3D model (glTF/GLB, OBJ, …) into a single-player RepGame world: the
model is voxelized into colored blocks standing on a flat sandstone floor.
Client-only — the server is never involved.

## Flat worlds

A save dir containing a `world_type` file whose first token is `flat` is a
flat world (`MapStorage::detect_flat_world`, called from `RepGame::init`
before any chunk generation):

- Ungenerated chunks come from `MapGen::flat_world_id`: `SANDSTONE` at
  y<=0, `AIR` above (fast path in `Chunk::load_terrain`, shared with the
  block-world layout). No terrain, mobs-from-terrain, etc.
- Saved chunk files load normally, so a pre-voxelized model is just ordinary
  chunk files in the save dir.
- Multiplayer is never connected, even if a host is given.
- A fresh spawn (no `player.dat`) is creative + flying; an existing save
  wins.
- `REPGAME_BLOCK_WORLD` takes precedence over the marker.

Any world can be made flat by hand: `echo flat > ~/.repgame/NAME/world_type`.

## tools/model_to_world.py

```sh
python3 -m venv out/model_venv   # one-time; out/ is gitignored
out/model_venv/bin/pip install 'trimesh==4.12.2' numpy pillow
out/model_venv/bin/python tools/model_to_world.py MODEL [MODEL ...] \
    --world NAME [--size 64] [--up y|z] [--x 4] [--gap 8] \
    [--repgame-dir ~/.repgame] [--no-glass] [--force] [--verbose] \
    [--color NAME=#RRGGBB]...
./out/linux/release/RepGame NAME
```

Multiple `MODEL`s each get their own voxel grid scaled to `--size` on the
longest side, and are laid out in a row along +x — the first at `--x`,
each next one `--gap` blocks past the previous model's end. Each is
z-centered on 0 and sits on y=1, all in one world.

| Option | Meaning |
|---|---|
| `--size` | Longest model side in blocks (default 64). |
| `--up z` | Model is Z-up (most CAD/Blender-native exports); default Y-up (glTF). Applies to every model. |
| `--x` | World x of the first model's min corner (default 4, keeps spawn at 0,0 clear). Each model is centered on z=0 and sits on y=1. |
| `--gap` | Empty blocks between consecutive models along +x (default 8). |
| `--no-glass` | Opaque palette only. |
| `--force` | Overwrite a world that already has chunk files. Only files it writes are replaced; nothing is deleted, so stale chunks from a bigger previous run can remain — `rm -rf` the world for a clean regen. |
| `--verbose` | Per-material triangle counts and effective colors — use this to find material names for `--color`. |
| `--list` | Print every material's tri count, mean sRGB/alpha and a few scene-node names (the names usable by `--color`/`--prefer`), then exit — no world written, `--world` not required. With multiple models the tables are merged (matching `--color`/`--prefer`, which apply globally). `--color`/`--prefer` args are still validated so typos warn. |
| `--color NAME=#RRGGBB` | Repeatable. Paint the material named NAME a flat sRGB color, ignoring its texture/factor. Unmatched names warn. |
| `--prefer A,B,C` | Repeatable, comma-separated ordered priority list: listed entries win every voxel they touch (vote weight `PREFER_BOOST ** (N - rank)`, after the visible-vote filter); earlier names beat later ones. Entries may be material names or glTF scene-node/geometry names (from the file, e.g. Blender's outliner), which lets you target single parts like the gas cap. Trade-off: real trim edges adjacent to them also get painted as that material. |
| `--jobs N` | Worker processes for baking/sampling/chunk writing (default: all cores). `--jobs 1` runs everything in-process — use it for debugging. |

### How it works

1. **Palette.** Average color of each candidate block's tile in
   `bitmaps/blocks.png` (ids/textures from `data/blocks/*.json` and
   `include/common/texture_ids_gen.hpp`): concrete, wool, non-glazed
   terracotta as opaque; glass + stained glass as transparent.
2. **Load.** `trimesh` loads the scene; the scene graph is baked to world
   space (vertices transformed in place — visuals stay shared so material
   work can be cached) and normalized so the longest extent is `--size`
   blocks.
3. **ID maps.** Palette Lab colors are quantized into a 64³ sRGB → block-id
   LUT (`block_lut`). Each textured material is then baked once per run
   into an "id image": `baseColorTexture` × `baseColorFactor` combined in
   linear space, converted back to sRGB, then LUT'd to block ids (glass
   ids get a +2048 tag when texel alpha < `ALPHA_THRESHOLD` = 0.75). The
   bake is cached per material — the glTF loader shares materials/images
   across primitives, so ~500 geometries cost ~60 bakes. Untextured
   sources (override, face/vertex colors, material color) get per-face
   ids through the same LUT.
4. **Sample + vote.** Texture baking runs in a `ThreadPoolExecutor`
   (big numpy ops release the GIL). Triangle sampling runs in a `fork`
   multiprocessing pool over `(part, lo, hi)` work units, largest first;
   inputs live in module globals so workers inherit them copy-on-write.
   Each worker returns locally reduced `(unique keys, weights)`; the
   parent merges them (re-merging the accumulation whenever it passes
   ~50M entries to bound memory). Each sample votes for its voxel,
   weighted by triangle area / samples-per-triangle so dense meshes
   don't outvote large panels. Every vote also carries the sample
   triangle's 6-bit facing mask (unit-normal axis bits, `smask` in
   `_sample_unit`).
   After the merge, `outside_air` flood-fills the non-occupied cells
   from the grid boundary (repeated axis sweeps over contiguous air
   runs, no scipy; static run layouts are precomputed once and the
   outer loop is capped by `FLOOD_MAX_SWEEPS` — unreached cells count
   as inside and just fall back to majority votes). The padded ring
   counts as outside, so voxels on grid-edge faces see their
   out-of-grid neighbour as air too. A vote is *visible* only if its
   triangle faces a
   neighbour that is outside air (`mask & O[vox]`); voxels with at
   least one visible vote drop the rest — hidden inner surfaces can't
   paint the outside. Voxels with no visible votes keep everything.
   Highest remaining weight wins; glass also wins with ≥
   `GLASS_MIN_SHARE` of the voxel's votes so thin glass survives
   next to opaque trim.
5. **Write.** Chunk files in MapStorage's RLE format (`uint32` record size,
   then packed `BlockState` + `uint32` count records over the 64³ padded
   chunk, index `((y+1)*64 + (x+1))*64 + (z+1)`), plus `world_type`.
   `player.dat` is not written. Chunks are written in parallel by the
   same fork pool.

### Limitations / gotchas

- Colors snap to the nearest block in Lab space. Saturated colors may land
  on a neighbor hue: e.g. Ferrari `#D40000` → ORANGE_CONCRETE because the
  RED_CONCRETE tile is dark (~`#8E2121`). Pick override colors near the
  target block's tile color.
- The model is a hollow shell (surface voxels only).
- `KHR_texture_transform` is ignored (base UVs used as authored).
- Hidden inner geometry is filtered by the facing-mask/outside-air vote
  (`outside_air`, `GLASS_MIN_SHARE`), which relies on triangle winding:
  degenerate or flipped normals mark no direction, and voxels where no
  vote sees outside air fall back to plain majority weight.
- Memory/time scale with triangle count and `--size`: the 2.2M-triangle
  Ferrari at `--size 1024` takes ~45 s and ~5.5 GB RSS on 16 cores
  (the outside-air flood fill is a serial parent-phase pass).
- Model colors come only from the file. If a viewer shows a different color
  than you get, check `--verbose` — the material may genuinely be untextured
  grey (the Ferrari's `Carpaint` is).

## make car / car-run (makefiles/model.mk)

Example pipeline; by default it lines up every `.glb` in
`~/Downloads/cars/` (a commented single-model Ferrari config shows the
per-model overrides):

- `make car` — creates `out/model_venv` and voxelizes the .glb file(s)
  at `CAR_GLB` into `~/.repgame/$(CAR_WORLD)`, in a row along +x.
  Incremental: `world_type` is the stamp; it regenerates only when any
  glb, `tools/model_to_world.py`, or the args
  (`out/models/<world>.args`, rewritten only when `CAR_SIZE`/`CAR_GAP`/
  `CAR_COLORS`/`CAR_PREFER` or the file list itself changes) are newer,
  or the world dir is gone. A regen `rm -rf`s the world dir first.
- `make car-run` — `car` + `linux`, then launches the world. In-game edits
  and `player.dat` persist across runs until a regen.
- `make car-mats` — list `$(CAR_GLB)`'s materials (tri counts, mean
  colors, example node names) with `CAR_COLORS`/`CAR_PREFER` applied, so
  typos warn before a long run. With multiple glbs the tables merge —
  matching the global `--color`/`--prefer` semantics. Writes nothing.
- `make clean-car` — wipe the world dir (including `player.dat` and any
  in-game edits); the next `make car` regenerates it.

Overrides: `CAR_GLB` (source .glb, or a space-separated list /
`$(wildcard ...)` — list order is placement order), `CAR_WORLD`
(default `ModelCars`), `CAR_SIZE` (default 512), `CAR_GAP` (default 8),
`CAR_COLORS` and `CAR_PREFER` (empty by default; commented Ferrari
values show per-model usage — inside a makefile escape `#` as `\#`, on
the command line it can be written plainly; `make car CAR_PREFER=`
disables). `CAR_COLORS`/`CAR_PREFER` apply to every model in the list —
they only match names that exist in each file, so per-model overrides
need a single-model `CAR_GLB`. `CAR_PREFER` is an ordered priority
list of material or glTF node/geometry names — listed entries win every
voxel they touch, earlier names beat later ones; real trim edges next
to them are painted too.
