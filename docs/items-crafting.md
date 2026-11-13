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

Non-mining breaks go through the same helper: `break_drops`
(`include/common/break_drops.hpp`) rolls `block_item_id`'s drops and
spawns the stacks — used by the player destroy path, support-loss kills
(`PlayerBlockPlacedEvent` `BreakDrops::Survival`, so they drop only in
survival) and leaf decay (`BreakDrops::Always`). Shears held while
breaking leaves skip the roll and drop the leaf item itself.

Tools and consumables: `SHEARS` (`shears` icon, `max_stack` 1, recipe
two iron ingots diagonal) harvests leaf blocks; `BONE` drops from
zombies; `BONE_MEAL` (`dye_powder_white_new` icon, shapeless 1 bone ->
3 meal) grows a sapling instantly on right-click (`tree_grow_try`) or
advances a crop one `crop_next` stage, consuming one in survival.
`<TYPE>_SPAWN_EGG` items
(`textures/items/spawn_eggs/` icons) hatch their mob on right-click —
see `docs/mobs.md`. `MINECART` (`minecart_normal` icon, `max_stack` 1,
recipe five iron ingots `"# #"/"###"`) places a minecart on a rail on
right-click (`docs/entities.md`). Rails themselves are block items:
`RAIL` crafts 16 from six iron + a stick (`"# #"/"#S#"/"# #"`) and
`GOLDEN_RAIL` crafts 6 from six gold + stick + redstone dust
(`"# #"/"#S#"/"#R#"`, `REDSTONE_WIRE_CROSS` — see `docs/blocks.md`).
`BUCKET` (3 iron ingots in a V, `max_stack` 16) and
the `max_stack` 1 `WATER_BUCKET`/`LAVA_BUCKET` scoop plain fluid
sources (level-0, non-waterlogged cells only) and pour new sources
on right-click (`item_is_bucket`/`item_bucket_fluid`,
`repgame_use_bucket` in `src/common/game/bucket.cpp` — see
`docs/fluids.md`); a held bucket
arms the mode-2 fluid raycast into `BlockSelection::pos_fluid`.
`BOW` (`bow_standby` icon, `max_stack` 1; 3 sticks + 3 string in the
spiral pattern) releases an `ARROW` projectile on right-click — see
`docs/entities.md` "Arrows" for the flight/hit/pickup sim and
`docs/player.md` "Use" for the input. `ARROW` (`arrow` icon) crafts
flint + stick + feather x4 and doubles as both the ammo drawn by the bow
and the item a picked-up stuck arrow returns.

Farm produce items (`WHEAT`, `WHEAT_SEEDS`, `CARROT`, `POTATO`,
`BEETROOT`, `BEETROOT_SEEDS`, `BREAD`) are ordinary pure items; the
plantable ones carry `"plants": "<BLOCK_NAME>"` — the stage-0 crop id the
right-click path places like a block (the crop's farmland survive rule
does the field check). `item_plant_block(item)` resolves it. The same
items double as breeding food via `MobTypeDef::breed_items`
(`docs/mobs.md`; bees eat `is_flower` blocks instead), and `BREAD` crafts shapeless 3 wheat.

Edible items carry `"food": <half-hearts>` (apple, bread, raw + cooked
meats, produce). The importer lands it in `ItemGenDef::food_heal`;
`item_food_heal(item)` resolves it for the right-click eat path
(`repgame_eat_food` — heals instantly in beta-MC style, consumes one in
survival; `docs/player.md`).

Mining speed: `ItemGenDef` carries `tool_class` (the `MineableClass` the
item accelerates) and `mine_speed` (per-tick multiplier). Optional item
JSON keys `"tool"` (`"pickaxe"`/`"axe"`/`"shovel"`/`"shears"`/`"hoe"`) and
`"mining_speed"` override the importer's name-derived defaults: a
`*_PICKAXE`/`_AXE`/`_SHOVEL`/`_HOE` suffix gives the class and the material
prefix the speed (`TOOL_TIER_SPEED` — wooden 2, stone 4, iron 6, diamond
8; `SHEARS` is shears at 8). Hoes additionally till soil on right-click
(`item_is_hoe`; `MINEABLE_HOE` is their mining class, which nothing is
weak to — see "Farming" in `docs/player.md`).
`item_mining_speed(item, block)`
(`src/common/item.cpp`) returns the multiplier when the tool's class
matches the target's `Block::mineable`, else 1 — see "Mining" in
`docs/player.md`.

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
bounce. A box left embedded in solid terrain (a block placed, pushed or
fallen into its cell) is depenetrated every tick by
`Collision::unstick_box` — it slips out the cheapest open side, or floats
up onto the covering block when walled in (a block per tick under a
column). Break/mob drops pop with `item_drop_hop` (peak ~0.2–0.35 blocks,
<0.3 sideways) so they land in the broken cell. In a fluid the item rides
a submersion spring — buoyancy proportional to the submerged fraction
minus gravity, keyed off the bilinear corner-height surface
(`item_surface_at` — the same `fluid_corner_height` field the mesher
draws, so the float target is continuous across cell boundaries), with
drag lerped air->water and a 0.03 rise cap — settling into a shallow float
rather than bouncing on a binary in/out-of-water test. It also drifts with
the current down the local surface slope, and a falling column (flowing
level under or over same-fluid cells) drags it down. The contact isn't
center-only: when the cell at the bottom face holds no fluid, every
footprint cell is sampled and the highest surface wins, so a stream keeps
pushing an item whose center has crossed a lip while the back of its box
is still over the last water cell — center-only sampling wedged items on
the lip. Lava and open fire
destroy the entity on contact — a small debris burst (the item's own
texels for block items, fire for pure ones), a `fizz` within 16 blocks,
and `NET_REMOVE_DIED`. Integration (and aging)
freezes in capture mode and while the
item's chunk isn't `CHUNK_LIVE`. Same-item entities within 0.5 blocks merge
into the older one every 10 ticks, capped at `item_max_stack`. Despawn at 5
minutes (`ITEM_DESPAWN_TICKS`). `MAX_ITEM_ENTITIES` = 512; spawning at the
cap evicts the oldest. `impulse(center, radius, strength)` is called
wherever `bodies.impulse` is — items are light, so the shove is tripled.

Sources: survival block-break rolls `block_roll_drops` and spawns each
result at the cell center with a small random hop (creative keeps the block
instead); survival mob kills spawn the type's drops at the mob's center.
`Q` throws one of the selected hotbar item, `Ctrl+Q` the whole stack
(from eye + 0.3·look, velocity look·0.045 + up 0.01, ~3 blocks on flat ground).
Only the key down-edge throws; holding Q repeats at a tick-limited
`DROP_ITEM_REPEAT_TICKS` cadence in `inventory.cpp` (OS key-repeat events
don't reach the throw). While the inventory is open
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

Rendering: isometric block items draw as a 0.25 cube shaped by the block's
pixel-space scale/offset (`BlockObjectInstance`,
`block_object_vertex.glsl` + `object_fragment`) — the shader folds the unit
cube into the block's sub-box and crops each face's UV window
(`block_object_shape(..., item_centered=true)` recentres x/z on the spin
axis so a slab rests on the ground and a chest stays centred); everything
else
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
reads the file into `Chunk::sidecars.items`, `World::render` hands
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

## Smelting

`data/smelting.json` drives the furnace: `{"smelt_ticks": <n>,
"recipes": [{"input": <ing>, "output": <name>}], "fuels":
[{"item": <ing>, "ticks": <n>}]}`. `<ing>` uses the same name/`#tag`/
option-list syntax as recipe ingredients (`data/item_tags.json` —
`#logs`/`#planks` cover every wood variant). `make blocks-import` emits
`src/common/smelting_gen.inc` (`SMELT_RECIPES`, `FURNACE_FUELS`,
`SMELT_TICKS`); `src/common/smelting.cpp` exposes the pure lookups
`smelt_result_item` / `furnace_fuel_ticks` (no GL — shared by client,
tests and server).

A smelt row's `input`/`output` may be a block id or a pure item id; ores
are listed both ways (the ore *block* and its `RAW_*` item) so mining
drops and furnace input agree. Cooked foods (`BEEF`→`COOKED_BEEF`, etc.)
and logs→`CHARCOAL` are recipes like any other; fuel covers coal,
charcoal, every log/plank family and sticks, with `ticks` in UPS_RATE
units. Ore *blocks* drop themselves (not `RAW_*`) via their `"drops"`
entries; the raw items remain valid furnace input for saves/mining tools
that produce them.
