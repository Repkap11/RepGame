# Blocks & textures (data-driven)

Block definitions live one JSON file per `BlockID` in `data/blocks/<name>.json`.
`id` is the persisted/network block id — never renumber; `LAST_BLOCK_ID` is
2047 (the maximum `BlockCell` can hold via `BLOCK_CELL_ID_MASK`). Ids may
contain gaps — `block_id_defined()`/`BLOCK_DEFINED_BITS` reports which ids
have a file; undefined ids get the C++ defaults (`is_pickable=false`, all
faces `TEX_AIR`) so creative skips them. **Id 1249 is permanently reserved**:
it was the empty-slot sentinel in pre-2047 saves and the importer fails if
any file claims it; load paths map it to empty (`save_slot_id_is_empty`) /
`AIR` (`save_chunk_block_id`). Removing a block frees its id for reuse — old
saves holding a removed id get the AIR/empty migration paths (the file name
must equal the lowercased `name`). Face keys in `"textures"`: `all`, `side`,
`top`, `bottom`, `left`, `right`, `front`, `back` (specific beats `side` beats
`all`, mapped to the `FACE_*` constants); `"icon"` names the non-isometric
inventory texture and implies `icon_is_isometric=false`. Remaining keys are
snake_case `Block` fields — unknown keys are a hard error; fields equal to the
defaults are omitted. `"aliases"` lists extra enum names for the same id.

Texture layers are independent `TextureID`s. `data/textures/<name>.png` holds
repo-authored 16x16 tiles; `data/textures.json` carries per-texture metadata:

- `source` — override image source. `textures/...` paths are relative to the
  Bedrock resource pack root (extensionless, `.png`/`.tga` auto-detected);
  plain names try `data/textures/`, `textures/blocks/*.png|tga`, then
  `textures/items/`.
- `frame` — index into a vertically stacked flipbook (default 0); the crop
  must be 16x16 after framing.
- `tint` — `"#rrggbb"` multiplied into RGB only (alpha preserved).
- `alpha` — `0..255` written into every nonzero-alpha pixel (transparent
  pixels stay 0). For Bedrock sources that are opaque where our tile was
  translucent (e.g. `water_still` vs `water`).
- `overlay` — `{source, tint?, frame?}`: a second tile alpha-composited over
  the processed base, e.g. `grass_side_carried` is Bedrock `dirt` plus the
  grayscale `grass_side` fringe tinted to the legacy green. The tint applies
  to the overlay only.
- `extract` — `{from: [x,y], size: [w,h], to: [x,y]}`: blit a rect of the
  resolved tile onto a fresh transparent tile. Torch top/bottom faces sample
  the window that tracks the cuboid's x/z position (cols/rows 7-8 for the
  2-wide stick), not the spot the sprite paints the cap or base — `torch_top`
  is Bedrock `torch_on` with the cap rect (rows 6-7) copied to the sampled
  window (rows 7-8), `torch_bottom` likewise with the stick end (rows
  13-14). `data/textures/redstone_torch_on.png` is the Java-edition tile on
  purpose: Java paints the lit jewel's visible 2-column core pink/red, while
  Bedrock's clipped core is plain yellow-white and reads as a normal torch.
  `redstone_torch_glow` (`keep`) is the solid fd0000 tile for the lit
  torch's inward-facing 3x3x3 glow shell (Java's `template_redstone_torch`),
  emitted in `chunk.cpp` with the `BLOCK_COORDS_INSIDE_OUT |
  BLOCK_COORDS_HALF_PIXEL` `face_shift` flags — INSIDE_OUT mirrors the unit
  cube so every quad faces inward (back-face culling leaves the far walls),
  HALF_PIXEL makes scale/offset/tex_offset 1/32-block units instead of
  1/16.
- `random_rotate` — tile participates in per-block random rotation. Used by
  dirt, grass top, snow and all leaf tiles.
- `keep` — pack even when no block references it (e.g. `player_selection`).

`make bedrock_samples_build` clones Mojang's bedrock-samples (pinned
`BEDROCK_SAMPLES_TAG`) into `bedrock-samples/` (git-ignored);
`make blocks-import` runs `tools/blocks_import.py` with
`BEDROCK_SAMPLES=bedrock-samples/resource_pack` and regenerates the committed
outputs (idempotent/deterministic — a dirty diff after rerun is a bug):

- `include/common/block_ids_gen.hpp` — `BlockID` enum, explicit values + aliases
- `include/common/texture_ids_gen.hpp` — `#define`-only `TEX_<NAME>` layer ids
  (safe for the shader preprocessor; compare as `uint(TEX_*)` in GLSL), plus
  `TEXTURE_COUNT`, `TEXTURE_ROTATABLE_COUNT`, `BLOCK_TEXTURE_ATLAS_ROWS`
- `src/common/block_definitions_gen.inc` — all 6 `textures[]` + `icon_texture`
  per block plus non-default fields
- `src/common/block_names_gen.inc` / `src/common/texture_names_gen.inc` —
  per-id name tables backing `block_definition_get_name` / `texture_name`
  (nullptr for undefined ids)
- `bitmaps/blocks.png` — 24-column atlas, layer `i` at col `i%24`, row `i/24`

Layer order is deterministic: `random_rotate` textures first (name-sorted),
then the rest name-sorted, so the vertex shader tests
`texture_id < uint(TEXTURE_ROTATABLE_COUNT)` — all layers are 0-based end to
end. Adding a block = add `data/blocks/foo.json` (+ tile under
`data/textures/` or a `source` in `textures.json`), `make blocks-import`,
rebuild. `make mobs-import` similarly regenerates `mob_models_gen.hpp` +
`mobs.png` from `tools/mobs_manifest.txt` (see `docs/mobs.md`). For scripting,
`BLOCKS_IMPORT_DATA_DIR` / `BLOCKS_IMPORT_OUT_DIR` redirect the importer's
input/output (used for testing against a copy of `data/`). `TEXTURE_COUNT`
must stay ≤ 2048 (GLES3 guarantees far fewer layers; the importer enforces
it).

New blocks get their property set by render class, copied from a template
block rather than hand-tuned: opaque cubes use the defaults; binary-cutout
tiles copy `GLASS`/leaves (see-through, `should_draw_*` off);
partial-alpha tiles copy a translucent template like `GLASS_LIGHT_BLUE`;
cross-quad plants copy the `Flowers` render template. Unrenderable shapes
(stairs, fences, walls, doors, signs, beds, rails, panes, trapdoors...) are
placeholder full cubes until a render class exists for them — see
`data/shapes.json` below for the workaround. Default
hardness is 1.5. Light emitters set `emits_light` and mirror the torch-style
flags (`no_light`, `casts_shadow:false`); see existing `data/blocks/*.json`
for the emitted levels.

`blocks_light_down` (derived, no JSON key): bottom-aligned partial blocks —
casts_shadow, opaque, scale.x/z=16, offset 0, scale.y<16, e.g. bottom slabs
and grass path. The post-pass clears `casts_shadow` (cell holds light,
lit from above/sides) while light.cpp never spreads light downward from it
and the skylight column fill stops at it — slab roofs shade but path tops
next to full blocks aren't dark. Floor cells also get a bit in
`Chunk::light_shade_mask` (sibling of `light_air_mask`): the light
texture's A channel is 0 for them, and chunk_fragment re-darkens the
ground layer a floor sits in — the old opaque-cell contact shadow —
while wall faces beside it stay undarkened.

The creative menu lists `CREATIVE_BLOCKS` from
`include/common/creative_order_gen.hpp`, computed by the importer. Pickable
blocks are deduped by visual signature (6 face textures + icon +
icon_is_isometric + scale/offset/tex_offset + render_order): each identical
group contributes one representative (prefer the block named after its
material, then the lowest shape rank, then a pre-import id from
`data/kept_block_ids.txt`, then lowest id); hidden duplicates stay real,
placeable blocks. `"creative": true|false` in a block JSON forces listing
on/off. The list is grouped by material — the name minus `STRIPPED_`/`WAXED_`
prefixes and one shape suffix, with dye-coloured blocks grouped into colour
families (WOOL, CONCRETE, STAINED_GLASS, ...) sorted in dye order.

A block JSON may set `"shape": "<name>"` (defined in `data/shapes.json`,
three kinds). **Mask shapes** (stairs, wall, banner,
hanging_sign, sign) are for silhouettes we can't render: the importer
replaces the 4 side faces with generated `<face_tex>__<shape>` tiles — the
resolved source tile with the mask's `.` pixels cut to transparent black —
and applies the shared `cutout` property set (see-through, no light
blocking, no shadow). Top/bottom keep the plain material texture.
**Model shapes** (fence, fence_gate) have real multi-box geometry: the
importer applies the `cutout` set plus the entry's own properties
(`is_fence`, `scale`, `offset`, ...) and keeps the block's plain textures;
the entry's `"icon_mask"` grid only cuts a generated `<side_tex>__<shape>`
tile used as the flat inventory icon (`icon_is_isometric=false`).
**Template shapes** (slab, button, pressure_plate, carpet, trapdoor) copy
geometry (`scale`/`offset`/`tex_offset`) plus behaviour properties
(`is_button`, `is_pressure_plate`, `collides_with_player`, ...) from a named
kept block via `"template"`; `"drop"` removes copied keys and other entry
keys override the template but not the block's own JSON. Textures always
stay the block's own.

`"spine_shell": true` (cactus) replaces the block's mesh with Java's
three-element cactus model: a full-cell box drawing only top/bottom, plus
two 14-wide boxes drawing only their side quads, which sit 1px inside each
cell edge and sample the whole side tile — the tile's opaque interior forms
the inset body while its mostly-transparent edge columns land on the cell
boundary as spines; the quads cross at the corners (independent planes, not
a real cube). Side quads live inside the cell so they never z-fight;
top/bottom sit on the boundary and are culled when the neighbor's opposing
face covers it (`mesh_face_seethrough`, Java's `cullface`). With
`spine_shell` the block's `scale`/`offset`/`tex_offset` describe only the
pick/collision/icon box (cactus uses `[14,16,14]`/`[1,0,1]`), and
`can_mesh_*` is disabled so emission stays per-cell — see `cactus.json` and
the `block->spine_shell` block in chunk.cpp phase 2.

Fences and fence gates draw several box instances per cell in the CPU
meshing pass (chunk.cpp phase 2, `fence_boxes`/`fence_gate_boxes` in
block.cpp): a 4x16x4 post plus 2px rails that connect to neighbors.
Wooden fences connect to any wooden fence, nether brick only to nether
brick, and every fence connects to gates whose span axis matches the
direction and to `is_solid` blocks; rails on both sides of an axis merge
into one 0..16 box, and rail end faces get `NO_LIGHT_NO_DRAW`. Gates rotate
on placement and store open/closed in rotation bit 2
(`BLOCK_ROTATE_GATE_OPEN`; facing is `rotation & 3`, the gate spans world x
when `(rotation & 1) == 0`); right-click toggles them (sneak + right-click
places instead), and open gates don't collide. `block_yaw` masks the open
bit for single-box consumers (raycast, selection, debris), so
gates never hit the `rotation >= 4` wall-torch branches. Collision is
multi-box (`fence_collision_boxes` via `block_collision_boxes` in
collision.cpp): the post plus a 4px bar spanning from the post face to
the cell edge on each connected side (both sides merge to 0..16), all
1.5 blocks tall like Minecraft — closed gates also collide at 1.5
blocks from the cell floor — so players and mobs can neither slip
between adjacent posts nor jump the fence. Because a box can reach above
its cell, the collision scan includes one layer below the player AABB.
Open gates still don't collide. Selection/raycast stay a single box —
the fence post or the gate's bounding box.

Doors are two-cell blocks: every lower-half id is linked to an upper-half
id by `door_partner` (validated by the importer). Both halves use the
`door_lower`/`door_upper` model shapes — a 16x16x3 slab pushed to the
placer-facing cell edge (offset z 13, matching MC's NORTH AABB) — collide,
render and raycast as single boxes, and carry the slab's current yaw in
rotation bits 0-1, so `block_yaw` masks bit 2 for them exactly like gates.
The flag bit means open on the lower and hinge-right on the upper:
`door_yaw(F, open, hinge)` swings the slab to `F + (hinge ? 3 : 1)` when
open, `door_rotations`/`door_facing` pack and recover the two halves, and
only the toggle logic ever re-reads both cells. Placement is atomic in
`PlayerBlockPlacedEvent` (a lower needs a solid floor below and a
placeable cell above; the upper is unplaceable on its own) and picks the
hinge so a second door on the placer's left becomes the pair's hinge-right
member — MC double doors. `block_can_survive_at` requires the partner half
plus the floor, so breaking either half pops the other through the normal
neighbour-update kill path; drops and picks map through `door_item_id`.
Right-click toggles non-`door_redstone_only` doors through `DoorEvent`
(TOGGLE), like gates; iron doors fall through to placement. Doors are
`is_redstone_mechanism`, so each half stores 0/1 power: a state update
whose combined lower|upper power changed queues `DoorEvent(SYNC_POWER)`
one tick out, giving MC edge semantics — rising opens, falling closes, and
a hand toggle in between persists. The full power model, dust shapes and
the event queue everything rides on are in `docs/redstone.md`.

`REPGAME_BLOCK_WORLD=1` ("block world") is a debug mode for eyeballing
every block: `make linux-run-blocks` runs it. Normal terrain, structures,
water, mobs, persistence and multiplayer are all off — nothing is read or
written under `~/.repgame` and no server is contacted. The ground is solid
SANDSTONE for y≤0 (solid, not a one-block floor, so the light cascade has
no void volume to sweep) with every defined non-AIR block placed
once at y=1 on a 2-spaced grid (32 per row, starting at x=2/z=2), ordered
`CREATIVE_BLOCKS` first then the remaining defined ids ascending — the
layout lives in `MapGen::block_world_id`. The z<0 half of the y=1 plane
holds a fixed fence showcase: an oak run with an inline gate, an L corner,
a T and a + junction, a lone post, a fence against planks, an oak fence
against nether brick (must not connect), and a nether brick run. The HUD shows the targeted
block's `NAME (id)` and face textures under the crosshair; spawn is
(0.5, y=2, 0.5) facing +x/+z in creative mode.
