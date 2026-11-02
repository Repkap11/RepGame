# Items, dropped items and crafting

## Items

`ItemID` (`uint32_t`, `include/common/item.hpp`) is the inventory-slot id
space. Ids 0..2046 are block items (same value as the `BlockID`),
`LAST_BLOCK_ID` (2047) is the empty-slot sentinel, and pure (non-block)
items are `ITEM_ID_BASE` (2048) and up. `InventorySlot` stays a packed
`{ ItemID, quantity }` pair; only block items can be placed or
middle-click-picked.

Pure items are data-driven, same rules as blocks: one JSON per item in
`data/items/<lowercase>.json` with keys `id` (>= 2048, never renumber),
`name` (the uppercase enum name), `icon` (a texture name resolved exactly
like block textures, including the `textures/items/` fallback and
`textures.json` metadata), optional `max_stack` (default 64) and optional
`aliases`. Unknown keys are a hard error. `make blocks-import` validates
them and emits `include/common/item_ids_gen.hpp` (enum + `item_id_defined`),
`src/common/item_definitions_gen.inc`, `src/common/item_names_gen.inc`,
packs the icons into `bitmaps/blocks.png`, and appends the items to
`include/common/creative_order_gen.hpp` (`CREATIVE_BLOCKS` holds ItemIDs).

Core API in `item.hpp`/`src/common/item.cpp`: `item_is_block`,
`item_id_valid`, `item_icon_texture`, `item_icon_is_isometric` (pure items
are flat), `item_max_stack`, `item_name`. Block items forward to the
`Block` definition.

Blocks declare drops with an optional `"drops"` JSON key:
`[{"item": "<block or item name>", "min": 1, "max": 1, "chance": 1.0}]`.
Absent = drop itself (door_item_id mapping plus the pickable /
non-transparent / non-water gate in `RepGame::break_selected_block`);
`[]` = drops nothing (glass, panes). Within one list, entries with
`chance < 1` are mutually exclusive — one roll picks the first entry whose
cumulative chance covers it (their chances must sum to <= 1.0, enforced by
the importer); `chance == 1` entries always drop. The importer emits
`src/common/block_drops_gen.inc`; `block_roll_drops(BlockID, uint32_t *rng_state,
ItemStack *out, int max_out)` rolls it deterministically (mutating state —
reseed for reproducible tests). Examples in tree: stone→cobblestone,
grass→dirt, ores→their item (lapis 4-8), leaves→sapling 5% + stick 2%
(+apple 0.5% oak/dark oak), gravel→flint 10% else gravel.

Mob drops live in `MobTypeDef::drops` (`MobDrop[2]` of
`{ ItemID item; int min; int max; }`, `LAST_BLOCK_ID` = unused slot).

## Dropped items

`ItemEntities` (`include/common/item_entities.hpp`), owned by `World` as
`world.items`, is the client-local dropped-item simulation — modelled on
BlockBodies (headless tick, render chains wired only after `init()`,
`render_inited` pattern, `update_render(alpha, chunk_loader)`, `draw` in the
main and reflection passes). Each entity is a 0.25 cube (box bottom-center
`pos`, `prev_pos` for interpolation): MC's item gravity/drag rescaled from
20 TPS to 144 UPS (gravity 0.00077/tick² ≈ 16 blocks/s², air drag 0.9972 —
gentler than the player's `GRAVITY_STRENGTH`), ground friction 0.6, no
bounce. Break/mob drops pop with `item_drop_hop` (peak ~0.2–0.35 blocks,
<0.3 sideways) so they land in the broken cell. In water
buoyancy +0.0015/tick with 0.9 drag and a 0.03 upward cap floats items to
the surface. Integration (and aging) freezes in capture mode and while the
item's chunk isn't `CHUNK_LIVE`. Same-item entities within 0.5 blocks merge
into the older one every 10 ticks, capped at `item_max_stack`. Despawn at 5
minutes (`ITEM_DESPAWN_TICKS`). `MAX_ITEM_ENTITIES` = 512; spawning at the
cap evicts the oldest. `impulse(center, radius, strength)` is called
wherever `bodies.impulse` is — items are light, so the shove is tripled.

Sources: survival block-break rolls `block_roll_drops` and spawns each
result at the cell center with a small random hop (creative keeps the block
instead); survival mob kills spawn the type's drops at the mob's center.
`Q` throws one of the selected hotbar item, `Ctrl+Q` the whole stack
(from eye + 0.3·look, velocity look·0.045 + up 0.01, ~3 blocks on flat ground). While the inventory is open
and a stack rides the cursor, a left click outside every visible panel
throws the whole stack, a right click throws one — "outside" is decided by
`InventoryRenderer::containsPoint` over the chest (if open), survival or
creative, and hotbar panels. Closing the inventory still returns the held
stack to the inventory first.

Pickup runs inside the item tick: items with `pickup_delay == 0` within
2.0 blocks of the player magnet toward the chest, and overlapping the feet
AABB (expanded 1.0 xz / 0.5 y) is collected via
`RepGame::give_item_to_player` — which returns the count that didn't fit,
leaving the remainder on the ground. A `pop` sfx plays on pickup.

Rendering: isometric block items draw as a 0.25 cube through the
falling-block path (`ParticlePosition`, `object_shader`); everything else
(pure items, sprite-icon blocks) draws as an extruded sprite — one shared
1536-quad mesh (per-texel front/back cells + per-texel-column/row edge
slices, 1/16 deep — every quad pinned to one texel's center uv so the
alpha test is all-or-nothing per quad and MSAA pixel-center shading can't
drop covered samples at texel boundaries) via
`item_sprite_vertex.glsl`, instanced with `{uint layer, mat4 transform,
vec2 light}`. The fragment's `u_ExactTexel` path in `object_fragment.glsl`
texelFetches at lod 0 so edge slices alpha-test per texel without mipmap
bleed. Sprites scale 0.35, stand upright, spin ~2 rad/s and bob while
grounded/in water; both paths are lit by `ChunkLoader::light_sample`. The
debug overlay shows `Items: <n>`.

- `REPGAME_ITEM_TEST="name,x,y,z[,count][;...]"` — exact deterministic
  spawns at init (block or item name, case-insensitive), zero velocity and
  `INT_MAX/2` pickup delay so they stay put for captures. ITEM_TEST spawns
  are non-persistent (never written to an items file) and get `spin_seed` 0
  so sprites face ±z — capture along the z axis sees the icon face-on.

Persistence mirrors the mob sidecar end to end: `items_x_y_z` beside
`mobs_x_y_z` (`u32 record_size`, `u32 count`, packed `ItemFileRecord
{item, count, pos[3], vel[3], age_ticks}` — readers copy the shared prefix
and skip records failing `item_id_valid`/`count <= 0`). `load_terrain`
reads the file into `Chunk::pending_items`, `World::render` hands
just-finalized chunks to `ItemEntities::adopt_chunk`, the ~16-tick
`poll_owned` persists+removes items whose owning chunk left the window,
and `World::cleanup` persists every owned chunk. A chunk whose items were
all picked up rewrites its file empty so stale records can't resurrect.
Pickup delay is not persisted (loaded items are pickable immediately).
No reads or writes in capture mode, fly mode, block world, or
`REPGAME_MOBS=0`/`REPGAME_MOB_TEST` runs — same gates as mobs. In
multiplayer the local file paths are off entirely: item identity,
persistence and pickup are server-arbitrated (see `docs/entities.md`
"Multiplayer entity sync").

## Crafting

Data-driven recipes: one JSON per recipe in `data/recipes/*.json` plus
shared tags in `data/item_tags.json` (`{"tag": ["NAME", ...]}` — referenced
as `"#tag"` in ingredients). `make blocks-import` validates them (unknown
keys, unknown names/tags, malformed patterns and out-of-range counts are
hard errors) and emits `src/common/recipes_gen.inc` — option arrays plus a
`CraftRecipe` table in sorted filename order, so output is deterministic.

Recipe format — exactly one of `pattern` (shaped) or `ingredients`
(shapeless):

```
shaped:    {"pattern": ["#", "#"], "key": {"#": <ing>}, "result": "STICK", "count": 4}
shapeless: {"ingredients": [<ing>, ...], "result": "OAK_PLANKS", "count": 4}
<ing> := "NAME" | "#tag" | ["NAME" | "#tag", ...]
```

A shaped pattern is 1–3 equal-length rows (≤3 chars); space is an empty
cell, every pattern char must be in `key` and every key must be used.
`count` defaults to 1 and is capped by `item_max_stack(result)`.

For the recipe inventory see `data/recipes/`.

`include/common/crafting.hpp` / `src/common/crafting.cpp` hold the pure
matcher (no GL): shaped recipes compare the bounding box of the non-empty
grid cells to the recipe's w×h and also try the horizontally mirrored
layout; shapeless assignment backtracks so overlapping option lists work.
`crafting_find_recipe(grid, dim)` returns the first match in table order.
`test_crafting` in `src/common/tests/test_crafting.cpp` covers it.

UI: the grid slots live in `RepGameState::ui.crafting` (see `docs/ui.md`;
renderer order —
slot 0 is the BOTTOM-left cell; they get flipped to top-first before the
matcher). A 2x2 grid shows in the survival inventory; right-clicking a
CRAFTING_TABLE opens a 3x3 in either game mode (closing on the same
gone/out-of-reach checks as a chest). Grid contents are temporary:
`repgame_crafting_return_grid` (idempotent, called from the every-tick
inventory-closed branch, the creative-mode toggle, and `cleanup()` before
`save_player_data()`) moves them survival → hotbar → thrown item entity.
Layout uses fixed-stride `InventoryRenderer` panels sharing the survival
panel's cell stride, with the group (grid + gap + 1x1 output) centred and
the grid's bottom pinned a quarter stride above the survival panel's items.
While the table is open, shift-clicking a survival or hotbar slot moves the
stack (half for right-shift-click) into the 3x3 grid via `crafting_grid_add`
(merge matching stacks, then empty cells, top-left first); without a table
the usual survival↔hotbar shift-move applies. Drag crafting is supported on
grid slots only: holding a stack and dragging with the left button splits it
evenly across the slots (right button places one each) via the pure
`crafting_drag_distribute` helper, re-applied from a drag-start snapshot each
tick for a live preview.
