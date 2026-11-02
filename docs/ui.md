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

A placed `SINGLE_CHEST_LATCH` block is a 9×3 container
(`include/common/chest.hpp`; `DOUBLE_CHEST_*` halves are decorative,
`block_is_chest` is the only identity check — it's by block id so the
GL-less server can call it). Contents live in the owning `Chunk`
(`World::get_chest_contents`/`set_chest_slot`, keyed by in-chunk index,
`chests_loaded` flag, persisted per chunk — `docs/persistence.md`).

`RepGame::open_chest`/`close_chest` (inventory.cpp): opening sets
`ui.chest.{open,pos}`, raises `inventory_open`, plays the sound, and —
in multiplayer — sends `CHEST_OPEN`; `CHEST_CONTENTS` snapshots repaint
the panel live. Every local mutation funnels through `chest_sync_slot`:
write the chunk, repaint the slot, send `CHEST_SET_SLOT`. Breaking an
open chest loots it (`repgame_loot_chest_contents` locally, or
`pending_chest_loot` + a server snapshot in MP — `docs/multiplayer.md`).

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
  `get_shader`.
- `PauseMenu` lays out button rows in centered coords (`hitTest` returns
  `BUTTON_*`); rows are compile-time-gated
  (`SUPPORTS_FRAME_BUFFER`/`SUPPORTS_IMGUI_OVERLAY`) and the steppers
  reuse `min_ambient_step`. `input.menu_open` swallows gameplay input
  while it's up.
- `ImGuiOverlay` (`SUPPORTS_IMGUI_OVERLAY` platforms): `shown` toggled
  by 'g' or `SHOW_IMGUI` at start; `ImGuiDebugVars` (position, world
  time, daylight, ambient floors, `LoaderDebugStats`, entity counts) is
  refreshed per frame from `RepGame::draw`.
- `FontRenderer` draws bitmap text at integer `pixelScale` sizes —
  stack counts, pause labels, overlay text.

`RepGame::draw_ui_pass` (`src/common/game/game_render.cpp`) renders all
of it into the platform UI pass after the world draw; VR substitutes its
own quad-layer path (crosshair/in-hand are skipped there —
`docs/vr.md`).
