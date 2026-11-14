---
name: ui-layout
description: Add, move or debug screen-space UI panels — hotbar, inventories, chest/crafting/furnace screens, pause/death menus — through the shared Clay layout pass (UiLayoutRects, UiLayoutInput, rects() consumers, text layer order). Use when changing UI geometry or text, adding a panel or overlay, or debugging an overlap/overflow/off-screen bug.
---

# UI layout

Reference lives in `docs/ui.md`. All screen-space geometry is owned by
one Clay pass in `src/common/ui_layout.cpp` — this skill is the
procedure and the invariants.

## The pipeline

```
renderer metrics (slab/cell sizes, strides)
        │  UiLayoutInput            (game_init.cpp: ui_layout_input)
        ▼
UiLayout::compute()   Clay tree -> UiRects   (ui_layout.cpp)
        │  UiLayout::validate()     overflow mask + overlap warnings
        ▼
state.ui.layout.rects()  —  the ONLY source of positions
        │  set_items_pos / apply_layout -> rebuild
        ▼
draw code  +  click routing   (both read the same rects)
```

## Invariants — break these and you get the old overlap bugs back

- **Never compute a screen position in draw or input code.** If a
  position isn't in `rects()`, add it there — declare it in the Clay
  tree, don't derive it from another draw call's output.
- **Declare unconditionally, draw conditionally.** Every screen's
  elements stay in the tree every pass (both crafting variants, all
  containers) so rects are fresh across open/close/mode toggles.
  Draw-side `if` only decides what paints.
- **Clay decides where panels go; renderers decide how cells quantize.**
  `InventoryRenderer::compute_layout` = sizing only (its positions are a
  baseline `set_items_pos` overrides); `whichSlotClicked`/
  `containsItemsPoint` stay renderer-side for slot-level math.
- **Text layers = flush order.** `text_append*` just queues glyphs;
  they rasterize at `text_flush`. `draw_ui_pass` flushes the
  HUD/inventory text *before* `pause.draw` and the menu's text after —
  an append on the wrong side of a flush draws in the wrong layer.
- **HUD content hides under `menu_open`** (item label precedent) — a
  dimmed sliver peeking past the panel edge reads as a bug.

Existing consumers of the pass include the touch controls
(`UiLayoutRects::touch_*`, drawn/hit-tested only while
`input.touch_ui`) — see `docs/ui.md`.

## Adding a UI element

1. Add the `UiRect`(s) to `UiLayoutRects` in
   `include/common/ui_layout.hpp`.
2. Add any sizing inputs to `UiLayoutInput` and fill them in
   `ui_layout_input()` (game_init.cpp) — read renderer metrics, never
   constants duplicated at both ends.
3. Declare the element in `UiLayout::compute()` — anchored to the root
   or floating off an existing element (container screens float off the
   survival slab's top edge; the pause menu floats screen-centered).
   Emit its rect after `Clay_EndLayout`.
4. Consume the rect in draw code *and* hit-test code — and add it to
   the `uistate` dump in `src/common/harness/script.cpp` if it's a
   panel.
5. Extend `test_ui_layout` (`src/common/tests/`): assert the rect
   across the aspect-ratio matrix (16:9, 21:9, 32:9, 9:16, 480x320)
   and pin `rects.overflow`.

## Verifying

| Question | Command |
|---|---|
| Geometry sane across aspects? | `RepGame --tests` → `test_ui_layout` |
| What rects did the pass emit? | `.rds` script with `uistate` → rects + `overflow` mask + applied slab positions in `results.jsonl` |
| Does it look right? | rep-dbg script: `menu 1`, `search 1`, `inventory 1`, `container chest\|furnace\|table x y z`, `give`/`slot`, `damage`, `screenshot` — see `tests/scenarios/` |
| Regression suite | `make scenarios` (15 scripts incl. `pause_menu`, `vitals`) |

## Debugging an overlap

- `uistate` first — the rects tell you which side moved.
- `UiLayout::validate` already warns once per overlapping sibling pair
  at resize time and sets `UI_OVERFLOW_*` bits for rects escaping the
  viewport — check `stdout.log` and the `uistate` overflow field.
- Elements escaping the viewport are usually an anchor problem (e.g.
  anchoring to a panel that grew screen-tall at ultrawide — anchor to
  the items slab, not the decorative panel).
- Tiny screens: Clay clamps rather than centers — don't assert exact
  positions there, assert the overflow mask.
