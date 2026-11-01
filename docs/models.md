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
out/model_venv/bin/python tools/model_to_world.py MODEL --world NAME \
    [--size 64] [--up y|z] [--x 4] [--repgame-dir ~/.repgame] \
    [--no-glass] [--force] [--verbose] [--color NAME=#RRGGBB]...
./out/linux/release/RepGame NAME
```

| Option | Meaning |
|---|---|
| `--size` | Longest model side in blocks (default 64). |
| `--up z` | Model is Z-up (most CAD/Blender-native exports); default Y-up (glTF). |
| `--x` | World x of the model's min corner (default 4, keeps spawn at 0,0 clear). The model is centered on z=0 and sits on y=1. |
| `--no-glass` | Opaque palette only. |
| `--force` | Overwrite a world that already has chunk files. Only files it writes are replaced; nothing is deleted, so stale chunks from a bigger previous run can remain — `rm -rf` the world for a clean regen. |
| `--verbose` | Per-material triangle counts and effective colors — use this to find material names for `--color`. |
| `--color NAME=#RRGGBB` | Repeatable. Paint the material named NAME a flat sRGB color, ignoring its texture/factor. Unmatched names warn. |

### How it works

1. **Palette.** Average color of each candidate block's tile in
   `bitmaps/blocks.png` (ids/textures from `data/blocks/*.json` and
   `include/common/texture_ids_gen.hpp`): concrete, wool, non-glazed
   terracotta as opaque; glass + stained glass as transparent.
2. **Load.** `trimesh` loads the scene; the scene graph is baked to world
   space and normalized so the longest extent is `--size` blocks.
3. **Sample.** Every triangle is sampled barycentrically (~0.5-block
   spacing). Each sample's color comes from the material's
   `baseColorTexture` × `baseColorFactor` (combined in linear space,
   converted back to sRGB), else face/vertex colors, else the material
   color. Alpha < `ALPHA_THRESHOLD` (0.75, material × texture) makes the sample a glass
   candidate.
4. **Vote.** Each sample maps to its nearest palette block in CIELAB and
   votes for its voxel, weighted by triangle area / samples-per-triangle so
   dense meshes don't outvote large panels. Highest weight wins; opaque
   beats glass on ties.
5. **Write.** Chunk files in MapStorage's RLE format (`uint32` record size,
   then packed `BlockState` + `uint32` count records over the 64³ padded
   chunk, index `((y+1)*64 + (x+1))*64 + (z+1)`), plus `world_type`.
   `player.dat` is not written.

### Limitations / gotchas

- Colors snap to the nearest block in Lab space. Saturated colors may land
  on a neighbor hue: e.g. Ferrari `#D40000` → ORANGE_CONCRETE because the
  RED_CONCRETE tile is dark (~`#8E2121`). Pick override colors near the
  target block's tile color.
- The model is a hollow shell (surface voxels only).
- `KHR_texture_transform` is ignored (base UVs used as authored).
- Where hidden inner geometry shares a voxel with the outer surface it can
  win the vote, giving dark speckles on painted panels.
- Memory/time scale with triangle count and `--size`: the 2.2M-triangle
  Ferrari at `--size 256` takes ~1 min and ~3.2 GB RSS.
- Model colors come only from the file. If a viewer shows a different color
  than you get, check `--verbose` — the material may genuinely be untextured
  grey (the Ferrari's `Carpaint` is).

## make car / car-run (makefiles/model.mk)

Example pipeline for the Ferrari Daytona SP3 model:

- `make car` — creates `out/model_venv`, unzips `fixV.glb` from `CAR_ZIP`
  into `out/models/ferrari/`, and generates `~/.repgame/$(CAR_WORLD)`.
  Incremental: `world_type` is the stamp; it regenerates only when the glb,
  `tools/model_to_world.py`, or the args (`out/models/<world>.args`, rewritten
  only when `CAR_SIZE`/`CAR_COLORS` change) are newer, or the world dir is
  gone. A regen `rm -rf`s the world dir first.
- `make car-run` — `car` + `linux`, then launches the world. In-game edits
  and `player.dat` persist across runs until a regen.
- `make car-reset` — wipe the world and regenerate.

Overrides: `CAR_ZIP` (source zip), `CAR_WORLD` (default `ModelFerrari`),
`CAR_SIZE` (default 256), `CAR_COLORS` (default
`--color Carpaint=#8E2323`; inside a makefile escape `#` as `\#`, on the
command line it can be written plainly).
