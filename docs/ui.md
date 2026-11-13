# UI: HUD, inventories, chests & overlays

Everything in the screen-space UI pass hangs off `RepGameState::ui`
(`UiState`, `include/common/game/ui_state.hpp`) — panels, overlays and
the dragged stack. The *player inventories themselves* are game data
outside it (`state.hotbar`, `state.survival_inventory`); the classes
under `ui` only render and route clicks to them.

```
UiState
├── layout     UiLayout      one Clay pass -> all UiRects (ui_layout.*)
├── overlay    UIOverlay     crosshair, held block, dragged-item draw
├── imgui      ImGuiOverlay  debug window ('g' toggles .shown)
├── font       FontRenderer  bitmap text (13px glyphs, pixelScale steps)
├── creative   CreativeInventory  creative palette pages + renderer, plus
│              the 'f'-opened item-name search box (docs/player.md keys)
├── pause      PauseMenu     Esc menu: buttons + stepper rows
├── held_slot / holding_slot  stack currently dragged by the mouse
├── chest      ChestUi       open chest panel (at most one)
├── furnace    FurnaceUi     open furnace panel: input/fuel/output 1x1s
├── sign_edit  SignEditUi    modal sign editor: 4 text lines + cursor
├── chat       ChatUi        chat box: one-line editor + scrollback
└── crafting   CraftingUi    2x2/3x3 grid + output + drag-split state
```

## Panels: `InventoryRenderer`

One reusable panel class (`include/common/inventory_renderer.hpp`)
draws every grid: hotbar strip, survival grid, creative palette, chest,
crafting grids. `compute_layout` sizes a `width×height` cell matrix under
`InventoryRenderOptions` — percentage bounds, `fixed_cell_stride`
overrides — and `set_items_pos` + `rebuild` place and repaint it once the
shared layout pass decides where the slab goes. Cells draw as instanced
`UIOverlayInstance`s through `RenderChain`; block icons are projected
isometrically in the ui_overlay shader using the same
`Block::scale/offset` shape data the chunk mesh uses. Hit testing is
`whichSlotClicked`/`containsItemsPoint` in centered y-up coords;
`changeSlotItem` repaints a single slot.

Screen-space placement is owned by `UiLayout` (`include/common/ui_layout.hpp`):
one Clay pass declares the whole UI tree — the bottom HUD strip (item
label reservation, vitals row, hotbar slab), the survival/creative
inventory slabs, the container screens floating off the survival slab's
top edge, and the floating pause menu — and emits named `UiRect`s in
centered y-up coords (`ui_layout.cpp` owns the conversion from Clay's
top-left y-down space). `RepGame::changeSize` runs it as:
`compute_layout` sizing for every renderer, the pass, then `apply_layout`
/`repgame_apply_ui_layout` adopting the rects into the renderers. Draw
code and click routing both read `state.ui.layout.rects()` — nothing
re-derives positions at draw time. The pass also self-checks:
`UiLayout::validate` flags screens whose rects escape the viewport
(`UiLayoutRects::overflow`, a `UI_OVERFLOW_*` bitmask — inspectable,
surfaces in `rep_tests` across aspect ratios) and warns once per
newly-overlapping sibling pair, so an overlap regression shows up at
resize time rather than as a visual bug.

Container screens — chest, the furnace trio, and the crafting grid +
output (both 2x2 and 3x3 variants declared, so `table_open` toggles just
re-apply) — get a window backdrop that unifies them with the player
inventory: the layout pass computes each `*_backdrop` rect as the padded
union of the screen's panels and the survival items slab,
`UIOverlay::draw_panel_backdrop` paints one flat quad through the shared
ui_overlay shader *before* the panels and inventory slab composite inside
it, and `off_inventory_panels` treats the rect's dead areas (the gap
included) as inside-the-window rather than "outside every panel".
Outside a backdrop, clicks hit-test the visible items slab
(`containsItemsPoint`) — the layout panel rect is laid out far wider than
the slab, so the items rect is what "outside the window" means for
held-stack drops.

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

The creative palette has a search box (`CreativeInventory::search_text`):
`f` while the palette is open flips `input.search_focused` (armed as
`search_toggle` in `keysInput`, applied by `process_inventory_events`),
which swallows subsequent presses into `input.search_*` pending buffers
the same tick drains into `searchAppendChar`/`searchBackspace`. A
non-empty query filters `CREATIVE_BLOCKS` by `item_name`
(`creative_search_matches` — case-insensitive, `_` as a space) and pages
the match list; `Enter`/`Esc` or hiding the palette closes and clears it.

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
`state.multiplayer.containers`. The layout pass places the chest slab
where the crafting grid sits — survival-stride cells, bottom edge
floating just above the survival items slab — inside the
`chest_backdrop` window framing chest + survival slabs. Inbound
`CONTAINER_*` frames only write World state (`NetContainers` never
touches `ui`); the open panel
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
three 1x1 `InventoryRenderer`s placed by the layout pass — input and
output on the top row, fuel centered below, all floating above the
survival inventory. The draw pass adds a window backdrop
(`furnace_backdrop`) unifying the group with the player inventory, plus
a cook-progress percent and remaining burn seconds in the `furnace_mid`
gap between the input/output panels. `World::tick_furnaces`
advances every loaded furnace each tick in singleplayer (fuel burn →
cook progress → output, lit-block swap via `set_loaded_block`); in
multiplayer the server ticks them instead (`docs/multiplayer.md`).
Smelt/fuel tables are data-driven — `docs/items-crafting.md` "Smelting".
Breaking a furnace loots all three slots (`repgame_loot_container`, or
`pending_container_loot` + a server snapshot in MP).

## Signs

Standing, wall and hanging signs carry four lines of front text (`SignText`,
`include/common/sign.hpp`, `SIGN_LINE_MAX` = 15 printable ASCII chars per
line) stored per position in `ChunkContainers.signs`. The editor
(`ui.sign_edit`, `src/common/game/sign_edit.cpp`) opens on
right-clicking one (sneak-click still places instead, like
containers) — `repgame_open_sign_editor` snapshots the stored text into
the edit buffer, closes every other screen, and raises
`input.text_input_active`, which releases the pointer and switches the
SDL event loops to `SDL_StartTextInput`/`SDL_EVENT_TEXT_INPUT` so real
text (capitals, shifted symbols) arrives through `input.text_*` counters
rather than the raw `key_down` path the search box uses.

`repgame_tick_sign_editor` drains those counters each tick through
`sign_edit_apply_input` (pure — test-driven): printable chars append to
the active line, Backspace deletes (or climbs a line when empty),
Up/Down and Enter navigate lines, Enter on the last line or Escape
commits. The tick also closes *without* committing when the chunk
unloads, the cell stops being a sign (after a few open-time grace
ticks), or the player walks out of reach — the same `+2` slack the
container screens use. Commit (`repgame_commit_sign_edit`) writes
`World::set_sign_text` and, in multiplayer, sends `SIGN_TEXT`; the
server's broadcast is the authoritative copy
(`docs/multiplayer.md`).

The panel is a Clay-declared screen (`sign_panel` + `sign_lines[4]` +
`sign_title`/`sign_hint`, scale `sign_k` fit-solved like the pause
menu). `repgame_draw_sign_editor` draws a full-screen backdrop quad plus
per-line field quads through the shared ui_overlay shader at menu layer
z (depth cleared first, `ORDER_Z_SIGN_*`), with the active line
brightened and a `_` cursor; text is the normal `FontRenderer` batch.
It draws late in `draw_ui_pass`, after the pause menu, and is skipped
under `capture` so screenshots stay world-only. Text in the world
itself is the `WorldTextRenderer` pass — `docs/rendering.md`. Android
and VR have no text-input path yet, so the editor is compiled out there.

## Chat

`ui.chat` (`ChatUi`, `src/common/game/chat_ui.cpp`) is the `t`/`Tab`-opened
chat box (`/` opens it pre-filled with a command slash) — a one-line
editor over the message scrollback. It borrows the sign editor's whole
text-capture path: the key handler only arms `input.chat_open_request`
on a down edge (and only when no other screen is up), the tick
(`repgame_tick_chat`) performs the open — `text_input_begin` clears the
buffers so the opening key never leaks into the line — and drains the
shared `input.text_*` counters through `chat_edit_apply_input` (pure —
test-driven). Enter submits via `chat_submit` (plain text to the wire,
`/` lines to the local command dispatcher), Escape closes, Up/Down
recalls `ChatUi::sent` history. Messages themselves live in
`state.chat` (`ChatLog`) — see `docs/chat.md`.

Like the sign panel, the chat panel is Clay-declared
(`chat_panel`/`chat_input`/`chat_lines` rects, `UI_OVERFLOW_CHAT`):
a left-anchored scrollback region over a one-line input bar, bottom edge
above the hotbar strip. `repgame_draw_chat` draws backdrop quads through
the shared ui_overlay shader before the first HUD text flush in
`draw_ui_pass`, wraps each visible message to the panel width, and is
skipped under `capture` and while menu/inventory/sign screens are up.

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
  `ImGuiDebugVars::held_*`/`held_block_*`/`bow_*`, tunable live from the
  overlay's "Held Item" section while the cursor is free
  (inventory/menu open) when `IMGUI_OVERLAY_LIVE_TUNE` is set to 1 in
  `imgui_overlay.cpp`; edits are logged via `pr_debug` on slider
  release / the "Log pose" button so a settled pose can be baked back
  into the `ImGuiDebugVars` defaults. The bow adds `bow_spin` (in-plane
  icon rotation about the sprite center).
- `draw_ui_pass` (`game_render.cpp`) names the selected hotbar item
  (`item_name`) just above the hotbar — above the hearts row in
  survival — black-outlined via `FontRenderer::text_append_outlined`;
  empty slots show nothing, and the label hides while `menu_open` so
  it can't peek out past the pause panel's edge. All UI text is
  batched: callers queue glyphs with `text_append*` and `text_flush`
  rasterizes the queue — so flush order *is* layer order. The pass
  flushes the HUD/inventory text before `pause.draw` and the menu's
  text after, keeping menu text on top and HUD text underneath.
- `UIOverlay::draw_vitals` renders the survival health row above the
  hotbar — ten `TEX_HEART_CONTAINER` slots with `TEX_HEART` /
  `TEX_HEART_HALF` filled by `vitals.health` (half-hearts), plus a
  `TEX_BUBBLE` breath row while `air_ticks` is below max underwater —
  through the same ui_overlay shader atlas path as the crosshair.
  The layout pass keeps the survival inventory slab's bottom edge above
  the HUD strip (label reservation + vitals + hotbar), and the
  chest/crafting/furnace screens follow since they float off the
  survival items top.
- `PauseMenu` content is static tables (`build_lines`/`Line::kinds`):
  rows of one or two side-by-side cells, compile-time-gated
  (`SUPPORTS_FRAME_BUFFER`/`SUPPORTS_IMGUI_OVERLAY`). The layout pass
  owns the geometry — `pause_panel` is a floating screen-centered Clay
  element (measured against real font metrics, fit-solved by shrinking
  the integer text scale `pause_k` until it fits), `pause_title` the
  title band, `pause_rows[i]` the row bands `hitTest`/`split_row` slice
  into equal cells (`hitTest` returns `BUTTON_*`). Three `[-][label][+]`
  stepper rows cover render distance, cave brightness
  (`min_ambient_step`), and time of day (`input.time_skip_hours`, same
  path as PgUp/PgDn); a "Mobs: Peaceful/Hostile" button flips
  `state.peaceful`. `input.menu_open` swallows gameplay input
  while it's up. Dead mode (`set_dead`, raised by
  `RepGame::player_damage` at 0 health) relayouts to a "You died!"
  screen with only Respawn (the RESUME button, relabelled) and Exit —
  `tick_vitals` re-pins `menu_open` each tick so it can't be dismissed
  without respawning (`docs/player.md`).
- `ImGuiOverlay` (`SUPPORTS_IMGUI_OVERLAY` platforms): `shown` toggled
  by 'g' or `SHOW_IMGUI` at start; `ImGuiDebugVars` (position, world
  time, daylight, ambient floors, `LoaderDebugStats`, entity counts) is
  refreshed per frame from `RepGame::draw`. `draw_ui_pass` draws it
  first — the bottom UI layer — so the hotbar, inventory panels (the
  creative search box included), held item and pause menu all composite
  on top of it. Its FPS line shows avg / 1% low / 0.1% low from
  `FrameTimeStats` (`include/common/utils/frame_stats.hpp`) — each over
  its own trailing window (1s avg, 2s 1% low, 5s 0.1% low, refreshed
  every 0.25s) where "X% low" is the average FPS of the slowest X% of
  frames in that window; the same stats extend the once-a-second `FPS:`
  pr_debug line in the SDL3 main loop (`main_loop_fps_log`).
- `FontRenderer` draws bitmap text at integer `pixelScale` sizes —
  stack counts, pause labels, overlay text.

`RepGame::draw_ui_pass` (`src/common/game/game_render.cpp`) renders all
of it into the platform UI pass after the world draw; VR substitutes its
own quad-layer path (crosshair/in-hand are skipped there —
`docs/vr.md`).
