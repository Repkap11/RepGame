---
name: add-content
description: Add a block, item or crafting recipe — the data-driven content pipeline (data/blocks, data/items, data/recipes, textures.json, make blocks-import). Use when creating or editing game content definitions.
---

# Add content (block / item / recipe)

Field reference and rules live in `docs/blocks.md` (block JSON, textures,
shapes, creative order) and `docs/items-crafting.md` (items, drops,
recipes). This skill is the procedure.

## Adding a block

1. Add `data/blocks/<lowercase_name>.json` — `id` (never renumber a used
   id; pick a free one, never 1249), `name`, `textures` face keys. Unknown
   keys are a hard error; fields equal to defaults are omitted.
2. Give it pixels: a repo-authored 16x16 tile under `data/textures/`, or a
   `source` entry in `data/textures.json` pointing into the Bedrock pack
   (plus `frame`/`tint`/`alpha`/`overlay`/`extract`/`random_rotate`/`keep`
   as needed).
3. Pick the render-class template (docs/blocks.md): opaque cubes use
   defaults; binary-cutout tiles copy `GLASS`/leaves; partial-alpha copy
   `GLASS_LIGHT_BLUE`; cross-quad plants copy `Flowers`. Unrenderable
   silhouettes stay full cubes unless a `"shape"` in `data/shapes.json`
   covers them (mask/model/template shapes — fences and doors already have
   real geometry).
4. Light emitters set `emits_light` + the torch flags (`no_light`,
   `casts_shadow:false`) — copy levels from an existing emitter JSON.

## Adding a pure item

1. Add `data/items/<lowercase>.json` — `id` >= 2048 (never renumber),
   `name` (uppercase enum name), `icon` (texture resolved like block
   textures), optional `max_stack`, `aliases`.

## Adding a recipe

1. Add `data/recipes/<name>.json` — exactly one of `pattern` (shaped,
   with `key`) or `ingredients` (shapeless); `result` + `count`.
   Ingredients are `"NAME"`, `"#tag"` (tags in `data/item_tags.json`), or
   a list of alternatives.

## Regenerating

1. First time only: `make bedrock-samples` (clones the pinned resource
   pack; needed whenever a `source` resolves into it).
2. `make blocks-import` — validates everything and regenerates the
   committed outputs (`*_gen.*`, `bitmaps/blocks.png`, recipes). It must
   be idempotent/deterministic: a dirty diff after a rerun is a bug.
3. Rebuild (`make linux`) and eyeball with `make linux-run-blocks` —
   the block world lays out every block on a grid and shows the targeted
   block's name/id/face textures under the crosshair.
