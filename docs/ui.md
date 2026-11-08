# UI: HUD, inventories, chests & overlays

Everything in the screen-space UI pass hangs off `RepGameState::ui`
(`UiState`, `include/common/game/ui_state.hpp`) — panels, overlays and
the dragged stack. The *player inventories themselves* are game data
outside it (`state.hotbar`, `state.survival_inventory`); the classes
under `ui` only render and route clicks to them.

```
UiState
├── overlay    UIOverlay     crosshair, held block, dragged-item draw
├── imgui      ImGuiOverlay  debug window ('g' toggles .shown)
├── font       FontRenderer  bitmap text (13px glyphs, pixelScale steps)
├── creative   CreativeInventory  creative palette pages + its renderer
├── pause      PauseMenu     Esc menu: buttons + stepper rows
├── held_slot / holding_slot  stack currently dragged by the mouse
├── chest      ChestUi       open chest panel (at most one)
├── furnace    FurnaceUi     open furnace panel: input/fuel/output 1x1s
└── crafting   CraftingUi    2x2/3x3 grid + output + drag-split state
```

## Panels: `InventoryRenderer`

One reusable panel class (`include/common/inventory_renderer.hpp`)
draws every grid: hotbar strip, survival grid, creative palette, chest,
crafting grids. `onSizeChange` lays a `width×height` cell matrix into
screen space under `InventoryRenderOptions` — percentage bounds, bottom
gravity (hotbar), top gravity (chest), `pin_*_y` edge anchors for
stacking panels (chest sits above the player inventory),
`fixed_cell_stride` overrides. Cells draw as instanced
`UIOverlayInstance`s through `RenderChain`; block icons are projected
isometrically in the ui_overlay shader using the same
`Block::scale/offset` shape data the chunk mesh uses. Hit testing is
`whichSlotClicked`/`containsPoint` in centered y-up coords;
`changeSlotItem` repaints a single slot.

## Input routing

`RepGame::process_inventory_events` (`src/common/game/inventory.cpp`)
runs per tick while `input.inventory_open`. Click targets resolve in a
fixed order — crafting grid/output, chest panel, survival grid, hotbar,
creative palette — via each panel's `whichSlotClicked`. Pickup semantics
are shared with chests (`chest_pickup_or_swap`/`chest_right_click`):
left click picks up / merges / swaps, right click picks half or places
one. `ui.held_slot` + `holding_slot` is the dragged stack;
`ui.crafting.drag` snapshots the grid so a button-drag re-splits from
scratch across each new target cell. Mouse presses remember where they
began (`input.mouse.buttons.*_pressed_in_ui`) so drags crossing the
UI/world boundary don't double-fire (`docs/player.md`).

## Chests

A placed `CHEST` block is a 9×3 container
(`include/common/chest.hpp`; `DOUBLE_CHEST_*` halves are decorative,
`block_is_chest` is the only identity check — it's by block id so the
GL-less server can call it). Contents live in the owning `Chunk`
(`World::get_chest_contents`/`set_chest_slot`, keyed by in-chunk index,
`chests_loaded` flag, persisted per chunk — `docs/persistence.md`).

`RepGame::open_chest`/`close_chest` (inventory.cpp): opening sets
`ui.chest.{open,pos}`, raises `inventory_open`, plays the sound, and —
in multiplayer — sends `CONTAINER_OPEN` through
`state.multiplayer.containers`. Inbound `CONTAINER_*` frames only write
World state (`NetContainers` never touches `ui`); the open panel
repaints by pulling each tick — `repgame_chest_sync_icons` diffs
world-owned contents against `ui.chest.drawn[]` and repaints just the
slots that changed. Every local mutation funnels through
`chest_sync_slot`: write the chunk, repaint the slot, send
`CONTAINER_SET_SLOT` (optimistic — the server relays it to other
viewers or pushes a correcting `CONTAINER_CONTENTS` back).
Breaking an open chest loots it (`repgame_loot_container` locally, or
`pending_container_loot` + a server snapshot in MP —
`docs/multiplayer.md`).

## Furnaces

A placed `FURNACE`/`LIT_FURNACE` block is a 3-slot container
(`include/common/furnace.hpp`): input, fuel, output plus `cook_progress`
and `burn_ticks` (in UPS_RATE ticks). `block_is_furnace` is the identity
check — by block id so the GL-less server can call it; the two ids share
one contents entry and the lit variant only changes the rendered block.
Contents live in the owning `Chunk` (`World::get_furnace_contents`/
`get_or_create_furnace_contents`/`set_furnace_slot`/`set_furnace_contents`,
keyed by in-chunk index, `furnaces_loaded` flag, persisted per chunk —
`docs/persistence.md`).

`RepGame::open_furnace`/`close_furnace` mirror the chest flow: opening
sets `ui.furnace.{open,pos}`, raises `inventory_open`, and sends
`CONTAINER_OPEN`; inbound `CONTAINER_CONTENTS` snapshots land in World
and `repgame_furnace_sync_icons` pulls diffs into the three panels each
tick (slots via `ui.furnace.drawn[]`, aux into `cook_progress`/
`burn_ticks`). Every local mutation funnels through `furnace_sync_slot`
(world write + repaint + `CONTAINER_SET_SLOT`). Furnace panels are
three 1x1 `InventoryRenderer`s laid out by `repgame_furnace_layout` —
input and output on the top row, fuel centered below, pinned above the
survival inventory (`repgame_furnace_layout` runs from `changeSize`
too). The draw pass adds a cook-progress percent and remaining burn
seconds between the input/output panels. `World::tick_furnaces`
advances every loaded furnace each tick in singleplayer (fuel burn →
cook progress → output, lit-block swap via `set_loaded_block`); in
multiplayer the server ticks them instead (`docs/multiplayer.md`).
Smelt/fuel tables are data-driven — `docs/items-crafting.md` "Smelting".
Breaking a furnace loots all three slots (`repgame_loot_container`, or
`pending_container_loot` + a server snapshot in MP).

## Crafting

`ui.crafting` (`CraftingUi`): 2×2 grid inside the survival inventory,
3×3 while a crafting table is open (`table_open`/`table_pos`,
`repgame_crafting_*` helpers). Grid contents are temporary —
`repgame_crafting_return_grid` puts them back when the UI closes. Recipe
matching and the output slot are documented with the item/recipe data in
`docs/items-crafting.md`.

## HUD & menus

- `UIOverlay` draws the crosshair, the in-hand block (shaded by
  `held_block_light` sampled at the camera), and the dragged item — all
  through the shared ui_overlay shader that `PauseMenu` also borrows via
  `get_shader`. The in-hand item sits in the lower-right (right hand)
  and is drawn by `ItemEntities::draw_held` from `draw_ui_pass`, posed
  in camera space after the depth clear: flat sprites use the same
  extruded 16x16 mesh as dropped items (real 1-pixel-thick edge
  faces); isometric block icons use the same cube mesh as dropped
  block items (`block_object_shader` + `BlockObjectInstance`), shaped
  by the block's scale/offset. The poses come from
  `ImGuiDebugVars::held_*`/`held_block_*`.
- `draw_ui_pass` (`game_render.cpp`) names the selected hotbar item
  (`item_name`) just above the hotbar, black-outlined via
  `FontRenderer::drawStringOutlined`; empty slots show nothing.
- `PauseMenu` lays out buttons in centered coords (`hitTest` returns
  `BUTTON_*`), grouped into lines of one or two side-by-side cells; line
  contents are compile-time-gated
  (`SUPPORTS_FRAME_BUFFER`/`SUPPORTS_IMGUI_OVERLAY`). Three `[-][label][+]`
  stepper rows cover render distance, cave brightness
  (`min_ambient_step`), and time of day (`input.time_skip_hours`, same
  path as PgUp/PgDn); a "Mobs: Peaceful/Hostile" button flips
  `state.peaceful`. `input.menu_open` swallows gameplay input
  while it's up.
- `ImGuiOverlay` (`SUPPORTS_IMGUI_OVERLAY` platforms): `shown` toggled
  by 'g' or `SHOW_IMGUI` at start; `ImGuiDebugVars` (position, world
  time, daylight, ambient floors, `LoaderDebugStats`, entity counts) is
  refreshed per frame from `RepGame::draw`. It stays visible while the
  pause menu is open — `draw_ui_pass` draws it just before the menu, so
  the menu panel lands on top.
- `FontRenderer` draws bitmap text at integer `pixelScale` sizes —
  stack counts, pause labels, overlay text.

`RepGame::draw_ui_pass` (`src/common/game/game_render.cpp`) renders all
of it into the platform UI pass after the world draw; VR substitutes its
own quad-layer path (crosshair/in-hand are skipped there —
`docs/vr.md`).
