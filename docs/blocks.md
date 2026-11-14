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
saves holding a removed id get the AIR/empty migration paths, but only while
the id stays undefined: once a new def claims it, those cells become the new
block, so reuse freed ids only for defs that never reached real saves. The
16 stub `*_candle_cake` ids freed in 2026-11 are the current reuse pool:
428, 603, 619, 679, 695, 714, 739, 834, 903, 1013,
1046, 1368, 1389 — claim one of them in a new `"id"` before taking from the
top of the range. (The file name
must equal the lowercased `name`.) Face keys in `"textures"`: `all`, `side`,
`top`, `bottom`, `left`, `right`, `front`, `back` (specific beats `side` beats
`all`, mapped to the `FACE_*` constants); `"icon"` names the non-isometric
inventory texture and implies `icon_is_isometric=false`. Remaining keys are
snake_case `Block` fields — unknown keys are a hard error; fields equal to the
defaults are omitted. `"aliases"` lists extra enum names for the same id.

`scale`/`offset`/`tex_offset` are the sub-block geometry trio, in pixels
(0-16): `scale`/`offset` place the block's quads inside the cell (pick box,
selection outline and collision all derive from them) and the quads sample
the tile window `[-tex_offset, -tex_offset + scale]` per axis. For a sprite
drawn at a fixed position in the tile (torches, flowers, crops) that means
`tex_offset = -offset` — the quad samples the texels it covers. Leaving
`tex_offset` at 0 samples the tile corner instead and visibly crops the
sprite. Deviations are deliberate artistic alignment (slabs sample the
middle rows, buttons/heads centre their sprite) — set the values from the
sprite's opaque bounding box for cross-quad blocks.

That window is in *sampled space*: `loadTexture` mirrors every tile in X
at GL upload, so sampled column `c` is PNG column `15 - c` (the cube
texcoords and face-rotation remap are all authored for the flipped tile,
so the flip is load-bearing — don't remove it). `extract` rect fields are
PNG space by default; `"mirror": true` on a rect reinterprets `to` in
sampled space for strips/bands that must land on an exact sampled column.
The importer recomputes each face's sampled window and warns when a
window-filling solid strip is shifted so it clips one edge while leaving
empty margin on the other — the signature of `to`/`tex_offset` written in
the wrong space. Narrow organic sprites and art deliberately offset
inside (or wider than) its window are ignored.

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
  1/16. A rect may set `"mirror": true` to write `to` in sampled space
  (see the X-flip note under `scale`/`offset`/`tex_offset`): the rect's
  content is flipped and pasted at `16 - to.x - size.x`, so the tile as
  sampled shows it at `[to.x, to.x + size.x)` in authored order —
  `chain_block` uses it to park Bedrock's 3px chain strip on sampled
  cols 6-8, the window the `Flowers` cross quads cover for a
  `scale [3,16,3]`/`offset [6,0,6]` box. The two cross quads sample
  different face slots — the first diagonal reads `textures[FACE_BACK]`,
  the second `textures[FACE_FRONT]` — so a def can give each plane its
  own tile: chains override `"front"` with the `*_block_2` tiles
  (Bedrock's `chain2`-family strip, offset so the two planes' links
  interlock instead of overlapping).
- `random_rotate` — tile participates in per-block random rotation. Used by
  dirt, grass top, snow and all leaf tiles.
- `flip` — whole-tile transform applied last: `"x"` mirrors left-right,
  `"y"` flips top-bottom, `"transpose"` swaps the axes. Use when a face's
  baked quad winding samples a directional tile backwards or on the wrong
  axis — the left flank's u runs opposite the right's (`vd_data_solid`),
  so the bed's side profile needs a `flip:"x"` variant on `left`, and its
  `bed_head_top` needs `"transpose"` to move the pillow band from the x
  edge to the head edge.
- `keep` — pack even when no block references it (e.g. `player_selection`).

`make bedrock_samples_build` clones Mojang's bedrock-samples (pinned
`BEDROCK_SAMPLES_TAG`) into `bedrock-samples/` (git-ignored);
`make blocks-import` runs `tools/blocks_import.py` with
`BEDROCK_SAMPLES=bedrock-samples/resource_pack` and regenerates the committed
outputs (idempotent/deterministic — a dirty diff after rerun is a bug;
outputs are only rewritten when their content changes, so a field-only JSON
edit rebuilds just the `.inc` TU that consumes it, not the `*_ids_gen.hpp`
includers):

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
(walls, banners, rails, panes...) are placeholder full cubes
until a render class exists for them — see `data/shapes.json` below for the
workaround. Default
hardness is 1.5. Light emitters set `emits_light` and mirror the torch-style
flags (`no_light`, `casts_shadow:false`); see existing `data/blocks/*.json`
for the emitted levels.

`"mineable"` (optional, `"pickaxe"`/`"axe"`/`"shovel"`/`"shears"`/`"none"`)
sets the tool family that speeds up survival mining of the block
(`Block::mineable`, `MINEABLE_*`). When absent the importer derives it from
the block name via the ordered `MINEABLE_RULES` substring table in
`tools/blocks_import.py` — first match wins — so `<SPECIES>_*` families land
on axe, stone/mineral/metal names on pickaxe, dirt/sand/powder on shovel
and leaves/wool/vines on shears; everything unmatched (plants, decorations,
unbreakables) stays none. Only set the key when the heuristic is wrong.

`"climbable": true` (ladder) makes the block's whole cell act as a
ladder for the player: while the body AABB overlaps it, forward input or
jump ascends, sneak hangs, and descent is capped — see `docs/player.md`
movement physics and `Collision::on_climbable`. Independent of
`collides_with_player`: ladders stay non-colliding thin slabs.

`blocks_light_down` (derived, no JSON key): bottom-aligned partial blocks —
casts_shadow, opaque, scale.x/z=16, offset 0, scale.y<16, e.g. bottom slabs
and grass path. The post-pass clears `casts_shadow` (cell holds light,
lit from above/sides) while light.cpp never spreads light downward from it
and the skylight column fill stops at it — slab roofs shade but path tops
next to full blocks aren't dark. Floor cells also get a bit in
`ChunkLight::shade_mask` (sibling of `air_mask`): the light
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
three kinds). **Mask shapes** (wall, banner) are for silhouettes we can't
render: the importer
replaces the 4 side faces with generated `<face_tex>__<shape>` tiles — the
resolved source tile with the mask's `.` pixels cut to transparent black —
and applies the shared `cutout` property set (see-through, no light
blocking, no shadow). Top/bottom keep the plain material texture.
**Model shapes** (fence, fence_gate, stairs) have real multi-box geometry: the
importer applies the `cutout` set plus the entry's own properties
(`is_fence`, `is_stairs`, `scale`, `offset`, ...) and keeps the block's
plain textures; the entry's optional `"icon_mask"` grid cuts a generated
`<side_tex>__<shape>`
tile used as the flat inventory icon (`icon_is_isometric=false`) — stairs,
fences and gates don't use it, they draw real multi-box isometric icons
instead (`block_icon_boxes`).
**Template shapes** (slab, button, pressure_plate, carpet, trapdoor,
standing_sign, wall_sign, hanging_sign) copy
geometry (`scale`/`offset`/`tex_offset`) plus behaviour properties
(`is_button`, `is_pressure_plate`, `collides_with_player`, ...) from a named
kept block via `"template"`; `"drop"` removes copied keys and other entry
keys override the template but not the block's own JSON. Textures always
stay the block's own.

Signs are thin boxes whose faces sample the Bedrock entity/item sprites.
A `*_STANDING_SIGN` (`is_standing_sign`) renders as two boxes
(`standing_sign_boxes` in block.cpp, emitted through the same multi-box
path as fences/stairs): a 16x9x2 board plate at the top of the cell plus a
2x7x2 post under its middle — the post's top/bottom caps are skipped
(coplanar with the board bottom and the supporting face). The def's own
`scale [8,16,8]`/`offset [4,0,4]` then describe only the pick box — MC's
selection column, so the board's outer edges aren't clickable. All four
lateral quads share one composed `sign_*_standing` tile: the entity
texture's 16-wide board band pasted into the rows the plate covers plus a
frame-1 `extract` of the 2-wide post strip into the column the post
covers (extract accepts a list of rects, each with optional
`source`/`frame`; the post lives on entity rows 16-31, outside frame 0).
The board's front/back window is depth-anchored like every lateral face
([offset.z, +width) = [7,23) — off-tile), so the mesher zeroes the
board box's `tex_offset.z`, anchoring it at the full-width band instead;
its top/bottom caps then sample rows 0-1 of the plain planks tiles. A
`*_WALL_SIGN`
is a 2px plate hugging the clicked block face (`scale [16,9,2]`,
`offset [0,4,0]`, `rotate_on_placement` + `needs_place_on_any_solid.front`
— the ladder/button wall-mount convention): its front/back quads sample a
`sign_*_wall` tile, the `extract`-lifted 16x12 board band out of the
species' 64x32 `textures/entity/*sign*` texture (the band fills tile rows
2-13 edge to edge, leaving the 9-row face window opaque margin on both
sides; the item icon is unusable here — it's a small board+post sprite
whose outline bleeds dark texels into the crop), while edge quads stay
plain planks. A
`*_HANGING_SIGN` (`is_hanging_sign`) renders as three boxes
(`hanging_sign_boxes`): the same 16x9x2 board plate parked at the bottom
of the cell plus two 3x7x3 chain strands rising from the board's top edge
to the support block overhead (`needs_place_on_any_solid.bottom` — the
support sits at pos - dir, i.e. `bottom` means "colliding block
overhead"); strand caps are skipped (coplanar with the support face and
the board's top quad). Lateral quads sample the `sign_*_hanging`
composite tile: the entity texture's board band pasted into the rows the
plate covers plus three `mirror` extracts of the chain link strip into
the strand windows (front/back anchor u at the z offset, left/right at
each x offset). Hanging signs share the sign yaw —
`rotate_on_placement` — so the board turns with the placer's look angle.
All three are
non-colliding, see-through, non-shadow-casting and carry
`breaks_in_liquid` + `waterloggable` (the kelp pattern): flow treats the
cell as a wall and never pops them, while a direct water write waterlogs
the sign instead of breaking it. Signs share the one generic 4-yaw
rotation, so
a wall sign reads its support through `block_supports_present` like any
ladder and pops when the block behind it breaks.

All three sign forms — standing, wall and hanging — carry editable
front text: `block_is_sign`/`block_is_wall_sign`/`block_is_hanging_sign` in
`include/common/sign.hpp` classify the ids without the definition table
so the dedicated server can use them too. Text lives in
`ChunkContainers.signs`, persists in `sign_*` sidecar files
(`docs/persistence.md`), syncs over `SIGN_TEXT`
(`docs/multiplayer.md`), edits through the sign panel (`docs/ui.md`),
and renders in world space via `WorldTextRenderer`
(`docs/rendering.md`).

Each species' standing and wall signs share one item (MC's single sign
item): the standing block's `wall_variant` names its wall form and the
wall block's `standing_variant` points back — a paired block-ref like
`door_partner` that the importer requires to be symmetric. Placement
(player.cpp) runs `block_wall_variant_rotation` against the clicked
face: a side click writes the wall variant when it survives there, a
floor/ceiling click keeps the standing block. `block_item_id` maps the
wall block back to the standing block, so drops, pick-block and the
survival inventory all yield the shared item; wall signs also carry
`creative: false` so the creative list shows one sign entry per species.

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

Pistons are the opposite special case: their mesh box depends on state, not
just the def — a head plate (`is_piston_head`, the
`*_ARM_COLLISION` blocks) is thin on its facing axis while the def carries
only the canonical front-facing plate, and an extended base
(`is_piston` + power) loses the 4px at its head end.
`block_adjust_coord_based_on_state` bakes the real box per state and
`piston_head_shape`/`piston_base_shape` (block.hpp) give the same
world-space box for all six rotations. Phase-2 greedy merging keys off that
effective box rather than the def scale (chunk.cpp): a run only tiles along
axes the box fully spans, so head plates merge in the plate plane and never
along their thin axis. In `chunk_vertex.glsl` the top/bottom face UV window
must still pick up `mesh_size` for the non-yaw up/down rotations — without
it a merged run draws one tile stretched over the whole quad instead of
repeating per cell. The arm is two instances (like Java's split between
`piston_head` and `piston_extended`): each head cell emits a second
instance — the 4x4 shaft spanning from the plate back to the base end —
def-space for the yaw rotations, world-baked for up/down — with both end
caps skipped and one instance per merged-run cell; each powered base cell
emits the matching 4x4x4 stub inside its recessed throat so the shaft
reads continuous from plate to body. The shaft uses the authored
`piston_arm` tile (wood boards with steel edges, after Bedrock's
`pistonArm.png`); since the grain must run along the axis on every
lateral, the horizontal LEFT/RIGHT slots — whose u runs along the shaft —
get the rotated twin `piston_arm_h`. Both plate faces get the head tile —
the interior face shows through the throat gap — and the head defs carry
an explicit `tex_offset: [0,0,0]`: the importer's default `tex_offset =
-offset` would wrap the 4px-shifted tile across the plate's front face
and push the rim sampling windows off the band quarter. For neighbor-face
coverage `mesh_face_seethrough` special-cases `is_piston_head`: only the
head-end boundary is covered (`MESH_PISTON_HEAD_COVERED`), since the
canonical def's `is_seethrough_face` would claim a full side face on the
wrong axis for up/down rotations and cull solid neighbors into holes.
Heads are raycast-targetable despite staying `is_pickable: false`
(creative + drop exclusion — the door upper precedent): `contains_block`
ORs `is_piston_head` in and picks against `piston_head_boxes` — the plate
plus the 4x4 arm shaft, the same two boxes the mesher emits and collision
blocks with, so the arm can't be clicked or walked through. The mirrored
`piston_base_boxes` covers the extended base (recessed body + throat
stub); the selection outline draws the pair. For lighting both base
defs are `casts_shadow: false` like the heads — light flows through a
piston cell (they shade nothing, and a piston over grass doesn't count
as cover), while mesh culling still treats them as the full cubes they
draw as. Mining or pick-blocking a
head retargets the base (`piston_head_base` in BlockNextToChangeEvent.cpp —
the same pairing `block_can_survive_at` checks), so breaking it drops the
piston and pops the head through the survive check.

During a move each touched cell holds `MOVING_PISTON` (id 243): an
invisible, non-solid `piston_immovable` placeholder that still collides with
the player. The mesher skips it outright — the carried block is drawn by
the sliding `BODY_PISTON` body — while `display_id`/`rotation` encode the
carried block so a cell that outlives its side-table entry still lands
correctly (see docs/redstone.md for the move lifecycle).

Fences and fence gates draw several box instances per cell in the CPU
meshing pass (chunk.cpp phase 2, `fence_boxes`/`fence_gate_boxes` in
block.cpp): a 4x16x4 post plus 2px rails that connect to neighbors.
Wooden fences connect to any wooden fence, nether brick only to nether
brick, and every fence connects to gates whose span axis matches the
direction and to `is_solid` blocks; rails on both sides of an axis merge
into one 0..16 box, and rail end faces get `NO_LIGHT_NO_DRAW` — `skip_faces`
bits are definition-space `FACE_*` indexes, and the mesher counter-rotates
them by the box's yaw into the world-space `packed_lighting` slots (the
shader's `faceType` quads are world faces). A closed
gate's leaf rails are emitted pre-split where the center upright crosses
them — an unsplit rail's z faces would sit coplanar with the upright's and
z-fight. Gates rotate
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
Open gates still don't collide. Raycast and the `MouseSelection` outline
consume `fence_pick_boxes` — the rendered boxes with each direction's two
rails merged into one bar, so the gap between the rails isn't
click-through — while gates stay a single bounding box. Fence and gate
items draw the model boxes as their icons (`icon_is_isometric`, no
`icon_mask`): `fence_icon_boxes` shows a connected segment and
`fence_gate_icon_boxes` the closed leaf — both yawed to span z so the
long profile lands on the iso view's RIGHT face (a held fence/gate
points away instead of lying flat across the hand). Every icon box
instance shares one screen depth and later boxes draw nearer: the fence
splits each rail where it crosses the post so the halves behind emit
first and the halves in front last (the middle segment is inside the
post's footprint and unneeded). The gate splits each rail the same way
where it crosses the upright — halves behind, upright, halves in front —
while the posts already bracket the leaf: the far post emits first so
the leaf reads over it and the near post last to cover the rail end cap
inside it.

Stairs render as 2-3 box instances per cell (chunk.cpp phase 2,
`stairs_boxes` in block.cpp): a full-width half slab plus a full-height
step half — `rotation` bits 0-1 hold the facing (the yaw index doubles as
the world-direction index, 0=-z/1=+x/2=+z/3=-x, matching
`getPlacedRotation`'s buckets) and bit 2
(`BLOCK_ROTATE_STAIRS_TOP`) is the upside-down half, placed by MC's rule:
clicking a block's bottom face, or the upper half of a side face
(`BlockSelection::hit_y_in_cell` from the selection raycast's hit t).
`block_yaw` masks bit 2 so single-yaw consumers see facing 0-3. The
straight/inner/outer corner shape is never stored — `stairs_shape_derive`
(MC's `getStairsShape`) derives it live from the four horizontal neighbors
each mesh/collision/raycast pass: a perpendicular same-half stair in the
facing direction makes an outer corner, one behind makes an inner corner,
and a same-facing stair on the merge's probe side suppresses it. The boxes
are emitted in definition space and yawed by the same `face_shift`
remap the shader shares with collision/raycast (`block_rotate_box`);
step faces coplanar with the slab surface get `NO_LIGHT_NO_DRAW` via
`skip_faces`. Collision (`block_collision_boxes`), raycast
(`contains_block`'s `ray_aabb_hit` loop — nearest box wins, so the
upper-front void stays walk-through-pickable) and the selection outline
(`MouseSelection` keeps `FENCE_MAX_BOXES` instances — fences need 5,
stairs 3 — extras collapsed to zero scale) all consume the same
`stairs_collision_boxes`
world-space list, so a neighbor change re-shapes every consumer at once.
Half-height steps ride the normal `STEP_HEIGHT` path, so walking up a
staircase needs no special movement code. Stair items draw their real
shape everywhere an icon appears (`icon_is_isometric`, no `icon_mask`):
`stairs_icon_boxes` gives the straight bottom-half stair yawed 270 so the
tread faces the iso view's front, and inventory slots, the dragged item,
the in-hand block and dropped item entities each emit one instance per
box (`block_icon_box_count`). Fences and gates ride the same path —
`block_icon_boxes` dispatches on the block flags and `ICON_MAX_BOXES`
bounds every per-box instance array.

Half slabs (OAK_SLAB etc. — the derived `is_slab` flag marks any colliding,
bottom-aligned, half-height box over the full footprint, so the legacy
`*_DOUBLE_*_SLAB` ids that share that box count too while SLAB-named full
cubes like STONE_SLAB2 don't) ride the same top-half bit as stairs:
`BLOCK_ROTATE_SLAB_TOP` (rotation bit 2) is set by the same placement rule
(clicked bottom face or upper side half). There is no separate top-slab
geometry — every single-box consumer mirrors the def box into the cell's
upper half (`offset_y = 16 - scale_y - offset_y` in collision.cpp,
ray_traversal's `contains_pixel`, the `MouseSelection` outline,
`block_adjust_coord_based_on_state` for the mesh instance, and
`block_debris`'s particle box), and `block_yaw` masks the rotation to 0 so
the bit never reaches yaw consumers. Face coverage flips the same way:
`mesh_slab_face` maps a top slab's world TOP onto def-space BOTTOM (and
vice versa) inside `mesh_face_seethrough`/`mesh_vis_logic.hpp`, so the
slab's real top face draws at the cell boundary and a full block above
culls it. Slabs don't combine into double slabs — a top slab under a
bottom slab simply renders as a full-height pair. Lighting: slab cells
are `blocks_light_down` light floors, and the fragment shader's contact
shadow reads the A channel snapped to `v_light_pos`'s layer — a mid-cell
face lands exactly on a layer boundary, so the snap is biased along the
face normal to keep the face's own cell from self-shading (a top slab's
underside reads the layer below it, not the floor cell it hangs from).

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
one game tick out, giving MC edge semantics — rising opens, falling closes, and
a hand toggle in between persists. The full power model, dust shapes and
the event queue everything rides on are in `docs/redstone.md`.

Tall plants are the door pattern generalized to two-cell foliage (tall
grass): a lower-half id and upper-half id linked by `tall_plant_partner`
with exactly one `is_tall_plant_upper` (both importer-validated);
`block_is_tall_plant` is the trait. Placement in
`PlayerBlockPlacedEvent` writes both cells when the lower's normal
support holds and the cell above is replaceable; a lone upper is
unplaceable. `block_can_survive_at` requires the partner half, so
breaking either half (or the ground) pops the other through the
neighbour-update kill path. Uppers are `is_pickable: false` (hidden
from creative) but stay raycast-targetable via `block_is_tall_plant` in
`contains_block`, and drops/picks map through `block_item_id` (the
generalized `door_item_id`).

Cake is edible: `"is_cake": true` (plain `CAKE` only — the stub
`*_candle_cake` defs were full-cube placeholders and were removed; their
ids join the freed-id pool above). Right-click
eats one 2px slice off the +x edge — the bite count lives in
`BlockState::rotation` (0..`CAKE_MAX_BITES` = 6, the same slot fluid
levels/stair shapes use — cake's box is yaw-symmetric so
`rotate_on_placement` was a no-op and went away to free the bits) — and
eating past `CAKE_MAX_BITES` removes the block with no drop and no break
debris (the removal queues `BreakDebris::None` — the block was eaten, not
mined; remote clients still debris on the packet replay). The shrunken
box is the shared per-site tweak: `block_adjust_coord_based_on_state`
cuts `scale_x` and swaps the +x face to `cake_inner` (a `keep` texture —
only C++ references it), and `block_collision_boxes`, `contains_pixel`
and the `MouseSelection` outline apply the same `-2*bites`. `block_yaw`
masks the rotation to 0 like slabs so a large bite count never reaches
the `rotation >= 4` wall-torch branches, and `block_supports_present`
skips the face rotation for `is_cake` so a bitten cake still reads its
floor support. `casts_shadow: false` because the cell is mostly air —
the default-true flag made even a 2px sliver darken a whole cell column.
Old-world caveat: cakes placed while
`rotate_on_placement` was set may have yaw 1-3 saved in rotation — they
render as that many bites taken once disturbed.

`BED` is a two-cell block like a door turned on its side: the foot half
(the item, `data/blocks/bed.json`) and the head half (`bed_head.json`)
link through `bed_partner`, with exactly one `is_bed_head` — the same
paired-ref convention as `door_partner`. Placement writes both cells:
the foot lands on the clicked cell and the head one block in the
placer's facing direction (the shared 4-yaw `rotate_on_placement`
rotation lives in both halves' `BlockState::rotation`);
`block_bed_partner_dir` turns a half's yaw into its partner offset.
`bed_foot_can_place` checks the head cell is replaceable plus both
cells' floor support (each half also carries
`needs_place_on_any_solid.top`), `block_can_survive_at` requires the
partner half — so breaking either pops the other through the
neighbour-update kill path — and `break_drops` yields the one `BED`
item whichever half dies first (`block_item_id` maps the head back to
the foot). Both halves are `scale [16,9,16]`, `is_seethrough`,
`casts_shadow: false`, `piston_immovable` (a push would orphan a cell),
and `is_bed` — right-clicking either runs `repgame_try_sleep`
(docs/player.md). The head is `is_pickable: false` + `creative: false`:
ray-targetable but unlisted, like a door upper (`is_bed` is on the pick
whitelist in `ray_traversal.cpp`, same as `is_door`). Face tiles are the
Bedrock pack's real two-block bed set (`bed_feet_end`, `bed_head_end`,
`planks_oak` bottoms); the two touching inner faces use the fully
transparent `air` tile so the coplanar quads never z-fight. Three faces
sample their tiles through `textures.json` `flip` variants because the
baked quad winding isn't symmetric: `left` gets `*_side_mirror`
(`flip:"x"` — the left quad's u runs opposite the right's, so the side
profile would show the pillow at the foot end), and the tops get
`bed_head_top_rot` (`"transpose"` — moves the pillow band onto the head
edge) and `bed_feet_top_flip` (`"y"` — parks the frame lip at the foot
edge). The flat item icon is Bedrock's `bed_red` sprite. Old
single-block BED cells read back as foot halves — they keep working
until a neighbour edit asks them to re-survive, at which point the
missing head pops them.

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
against nether brick (must not connect), and a nether brick run. The ImGui debug overlay
shows the targeted block's `NAME (id)` ("Looking At"); spawn is
(0.5, y=2, 0.5) facing +x/+z in creative mode.

## Trees & leaves

`BlockCell` has no spare bits for a placed/natural flag, so leaves come in
two ids per species: the natural `<NAME>` block the generator and grown
trees write, and a `<NAME>_PERSISTENT` twin (`natural_variant` /
`persistent_variant` importer-validated ref fields — each points back and
all six face textures must match). Placing a leaf item writes the
persistent variant (`block_placed_leaf_id` in the placement path);
`block_item_id` maps persistent leaves back to the natural item, so picks,
drops and shears always yield the natural leaf. Helpers in
`block.hpp`: `block_is_natural_leaf`, `block_is_persistent_leaf`,
`block_is_leaf`, `block_natural_leaf_id`. `Block::is_log` is a derived
flag (no JSON key): members of the `"logs"` item tag hold leaves up.
`Block::is_flower` is derived the same way from the `"flowers"` item tag —
the bee's breed food (`docs/mobs.md`).

Decay: when a neighbour change may have removed a natural leaf's support,
`BlockNextToChangeEvent` queues a jittered `LeafDecayEvent` (0.5–2.5 s,
deduped through `RepGameState::pending_leaf_decay`). The event BFSes
face-adjacent leaf cells of any species up to `LEAF_DECAY_RANGE` = 6
(`leaf_has_log_support`); an `is_log` face neighbour or an unloaded cell
(`LAST_BLOCK_ID`) counts as supported. Unsupported leaves get an AIR
write with `BreakDrops::Always`, whose neighbour updates wake the next
leaves — a severed canopy unravels over a few seconds. Persistent leaves
never decay; breaking leaves (or shears on them) keeps the normal drop
rolls.

Growth: saplings grow on random ticks (`World::process_random_ticks` —
24 samples/chunk, live chunks within 2 XZ / 1 Y of the player, light ≥ 9,
1/3 grow chance ≈ 3.5 min mean) or instantly on bone meal. `tree_grow_try`
(`tree_grow.cpp`) resolves the `TreeSpec` from the sapling's world
position and writes it through the shared builders in
`include/common/tree_shapes.hpp` — the same code the new terrain
generator runs, so shapes are a pure function of position and two clients
grow identical cells. Random ticks are off in capture mode, fly mode, the
block world and flat worlds, and paused while the player is flying or
no-clipping (traversal would otherwise persist frontier chunks nobody
edited — every natural write marks its chunk `dirty`). Bamboo, dead,
garbage and potted saplings and the mangrove propagule never grow.

## Grass & dirt

Grass spreads source-side on the same random-tick sampler
(`World::grass_random_tick`, called for every sampled `GRASS`,
`SNOWY_GRASS` or `MYCELIUM` cell): a grass cell covered by a
`casts_shadow` block — the light engine's opacity test — or by a cell
holding fluid (`World::grass_covered`; `fluid_in_cell` counts
waterlogged cells as water, so submerged grass dies) dies back to
`DIRT` (torches, leaves and partial blocks only shade, they don't
cover; an unloaded cell above counts as open), and an uncovered one
rolls 1-in-3
(`GRASS_SPREAD_CHANCE_DEN`) for up to `GRASS_SPREAD_ATTEMPTS` (4) target
picks in MC's reach (±1 horizontally, −3/+1 vertically). A pick converts
its target when it is `DIRT` and the cell above the target is not
covered either. Snowy grass spreads plain `GRASS`, mycelium spreads `MYCELIUM`;
podzol does not spread. Writes use the crop-growth path
(`set_loaded_block_natural` + `BLOCK_UPDATE`), so each client simulates
and broadcasts its own conversions. This is also what re-greens sheep
grazes and farmland tramples — both convert the cell to `DIRT`
(`docs/mobs.md`, `docs/player.md`).

## Crops & farmland

Crop chains are data-driven: a stage block's `"crop_next"` ref key points
at the next growth stage (`null`/absent = mature, the chain terminator).
`Block::crop_next` holds the resolved id and `Block::is_crop` is derived
for every chain member, mature included. Wheat has 8 stages
(`WHEAT_0`..`WHEAT_7`), carrots 5 (`CARROT_0`..`CARROT_4`), potatoes 4
(`POTATOES`..`POTATOES_3`) and beetroots 4 (`BEETROOT_0`..`BEETROOT_2` →
`BEETROOTS`). All stages are `"creative": false` — farming yields the
produce items, not the stage blocks.

`block_can_survive_at` requires an `is_crop` block to sit directly above
`FARMLAND`, which covers planting rules, creative placement and the
pop-off kill path when the field reverts: trampling converts farmland to
dirt (`repgame_convert_block` in `PlayerBlockPlacedEvent.cpp`), the
neighbor update kills the crop, and its stage-appropriate drops roll.
Stage drops live in each JSON's `"drops"`: mature wheat drops `WHEAT`
plus 1–3 `WHEAT_SEEDS`, carrots/potatoes drop 2–5 of their produce,
mature beetroot drops `BEETROOT` plus 1–3 `BEETROOT_SEEDS`, and young
stages drop a single seed/produce. The grass/fern/tuft foliage variants
roll a wheat-seed chance.

Random ticks advance crops along `crop_next` on the same sample/light/1-in-3
cadence as saplings (`CROP_GROW_CHANCE_DEN`), writing the next stage with
`set_loaded_block_natural` — not a player edit, so the pen rule stays
out — plus a `BLOCK_UPDATE`. Bone meal on a crop skips the light check
and advances exactly one stage per click (player.cpp).

Old-save caveat: leaves placed before persistent variants existed save as
the natural id and may decay once disturbed when no log is within range.

## Fluids

`flows != 0` marks a fluid (water, lava): the value is the per-step
spread delay in ticks at `UPS_RATE`. Companion keys
(`include/common/block_definitions.hpp`):

- `flow_range` — max flowing level; capped at `FLUID_MAX_LEVEL` (7)
  because the level lives in `BlockState::rotation`'s 3 bits (sources
  are rotation 0, flowing cells rotation 1..`flow_range`).
- `flow_slope` — how far sideways spread searches for a drop
  (`fluid_slope_distance`).
- `forms_sources` — MC's infinite-water rule: a fed cell with two
  adjacent plain sources over a solid/source cell becomes a source.
- `waterloggable` — the block may live inside a water cell
  (`BlockState::waterlogged` is the per-cell flag; such cells are walls
  to flow — only source-level water writes waterlog them, via
  `block_state_normalize`).
- `breaks_in_liquid` — a fluid writing over the block pops it
  (water carries survival drops, lava burns them), unless the block is
  also waterloggable — those get waterlogged instead of broken.

The sim itself — `FluidUpdateEvent`, level recompute, drop-seeking
spread, lava/water reactions — is `docs/fluids.md`; sloped-surface
rendering is `docs/rendering.md`.

## Rails

`is_rail` blocks (`RAIL`, `POWERED_RAIL`, and the hidden render-only
`RAIL_CORNER` tile) are 1px plates: scale
[16,1,16], air on every face but the top tile, non-colliding, and they
survive only over a colliding block — checked directly in
`block_can_survive_at` rather than through `needs_place_on_any_solid`,
because a rail's `rotation` is its shape, not a yaw. Picking is the one
exception: `rail_pick_height_px` makes ascending shapes select on an 8px
box (MC behaviour; flat and curved stay 1px), applied in
`ray_traversal.cpp` and `mouse_selection.cpp` so the outline matches.

All track logic is pure and unit-tested in `include/common/rail.hpp` /
`src/common/rail.cpp`: the `RailShape` enum (NS, EW, four ASC slopes,
SE/SW/NW/NE curves), `rail_endpoints`/`rail_render_yaw` for the mesher
and future cart physics, `rail_resolve_shape` for connection resolution
(straights beat curves, sticky endpoints keep T-junctions stable, a lone
uphill link makes an ascending shape, powered rails never curve, unloaded
neighbours freeze the shape), and `powered_rail_chain_powered` for
powered-rail chaining (`docs/redstone.md`). State encoding borrows
dust's trick: shapes 0-5 live in `rotation` with `display_id == id`;
the four curves keep `id == RAIL` but display `RAIL_CORNER` (the turned
tile; SE/SW/NW/NE store rotations 3,0,1,2). The stored curve rotation
is not the render yaw: `rail_render_yaw` maps the curves through a
measured table (the turned tile draws an N+W arc at yaw 0 and turns
clockwise per yaw step), so SE/SW/NW/NE render at yaw 2/3/0/1.

Placement pre-resolves the shape from `camera.angle_H`
(`src/common/game/player.cpp`) so there's no wrong-shape flash, and
`perform_checks` re-resolves on every neighbour change. In the mesher
(`chunk.cpp` phase 2) rails emit one instance per cell with
`face_shift = rail_render_yaw(shape)` — plus `BLOCK_COORDS_SLOPE` for
ascending shapes, which makes `chunk_vertex.glsl` raise the uphill end
of the plate (the rotation nibble doubles as the uphill direction:
0=-z, 1=+x, 2=+z, 3=-x). Every rail copies its top tile onto the
bottom face so the plate's underside isn't air: a flat rail over an
opaque support keeps the underside culled (it would z-fight the
support's coplanar top face), but over glass or another non-full
support it shows like any block's bottom face. An ascending rail's
underside is the tilted plate's back — always drawn, and lit like the
top face in `chunk_vertex.glsl` (`BLOCK_COORDS_SLOPE` + FACE_BOTTOM),
matching MC's two-sided slope quad.
