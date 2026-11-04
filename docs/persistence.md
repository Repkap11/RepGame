# Persistence & save formats

All on-disk state goes through `MapStorage`
(`include/common/utils/map_storage.hpp`,
`src/common/utils/map_storage.cpp`). The storage root is
`getRepGamePath()` (`src/common/utils/file_utils.cpp`): `$HOME/.repgame`
on Linux, `.` on Android/Windows, the WasmFS virtual filesystem root on
WASM. Below it, one directory per world (`MapStorage::init` mkdirs it),
named by the `world_name` argument.

```
<root>/<world>/
    world_type              optional; "flat" selects the flat generator
    player.dat              player position, inventories, settings
    world.dat               server-side world clock (int64)
    chunk_<x>_<y>_<z>       RLE BlockState volume per chunk
    chest_<x>_<y>_<z>       chest contents for a chunk
    mobs_<x>_<y>_<z>        mob records for a chunk
    items_<x>_<y>_<z>       dropped-item records for a chunk
```

The `world_type` marker file is read once by
`MapStorage::detect_flat_world` before any generation; `flat` worlds get
a sandstone floor and skip normal terrain (see `docs/mapgen.md`,
`docs/models.md` — `tools/model_to_world.py` writes it).

## Chunk files (`chunk_x_y_z`)

One file per chunk, written only when `chunk.dirty`:

```
u32 record_size              // sizeof(STORAGE_TYPE) at write time
record_size * N              // N RLE runs covering the whole volume
```

Each run is `{ BlockState blockState; u32 count }` packed, and the runs
tile the full `CHUNK_BLOCK_SIZE` internal (halo-inclusive) index space.
The `record_size` prefix is the forward/backward-compat mechanism:
`load_blocks_decode` reads whatever size the file claims and copies only
the shared prefix into a `BlockState`, so records written by a build
with a different `BlockState` layout still parse. A file whose record
size differs from the current build's marks `chunk.dirty` so it gets
rewritten in the new format on the next save.

Two read paths share the decoder: `load_chunk` fills the live `Chunk`'s
cells/extras (`store_state`), and `load_blocks` fills a raw
`BlockState[]` for the server with `expect_empty_blocks` set (see
"Migration"). Writes happen off the render thread inside
`TerrainLoadingThread::process_value` — a departing slot persists
through the work queue before its memory is released — plus
`Chunk::persist` on shutdown and `persist_dirty_blocks` on the server.

## Chest files (`chest_x_y_z`)

```
u32 record_size              // sizeof(ChestFileRecord)
u32 num_chests
record: { i32 x, y, z        // world block position
          InventorySlot slots[CHEST_SLOTS] }
```

Same record-size prefix trick; readers copy the `slots` prefix only.
`persist_chests` *removes* the file when the map is empty — a missing
file and "no chests" are equivalent, so deleted chests can't resurrect.

## Mob / item sidecars (`mobs_*`, `items_*`)

```
u32 record_size              // sizeof(MobFileRecord|ItemFileRecord)
u32 count
packed records...
// mobs only, after the records:
u32 removed_count
u8 removed[removed_count]    // tombstoned natural-roll indices
```

`MobFileRecord`/`ItemFileRecord` are `__attribute__((packed))` structs
with the shared-prefix read convention (a larger saved record still
loads). `MobFileRecord` ends in `u64 net_id` — the deterministic
`net_natural_mob_id` for natural-spawn products, 0 for ordinary mobs —
and `flags` carries the provenance bits (`NATURAL`/`TOUCHED`). The
`removed` section tombstones natural-roll indices that must not
respawn because that mob was touched (it lives on as a record, or died
after being touched). A pre-`net_id` record size and a missing `removed`
section both load fine (old files: `net_id == 0`, empty tombstones).
Invalid records are dropped on read: `type >= MOB_TYPE_COUNT`
for mobs, `!item_id_valid || count <= 0` for items.

File *existence* means nothing for mobs anymore — the deterministic
natural roll always runs on load (pristine naturals are recreated, never
saved; see `docs/mobs.md`). `persist_mobs` therefore *removes* the file
when records and `removed` are both empty, and stale "populated marker"
files from older saves are unlinked the next time that chunk is
persisted. `persist_items` still writes whenever it runs so stale
records don't come back. Loaders: `MobManager`/`ItemManager`
per-chunk ownership (`docs/mobs.md`, `docs/items-crafting.md`); the
server keeps the same files via `ServerEntities::persist_chunk_entities`.

## `player.dat`

`PlayerData` (`include/common/player_data.hpp`,
`src/common/utils/player_data.cpp`) is written once at exit and read
once at start (`RepGame::init`/`cleanup` in
`src/common/game/game_init.cpp`). The current format is versioned text:

```
format 1
pos <x> <y> <z>
angles <angle_H> <angle_V>
flying <0|1>
no_clip <0|1>
draw_quality <int>
game_mode <int>
world_time <int64>
render_distance <int>
min_ambient <float>
hotbar_selected <int>
hotbar <slot> <item_id> <quantity>
survival <slot> <item_id> <quantity>
```

Rules: `#` comments and blank lines skipped, unknown keys ignored,
missing keys keep `PlayerData` defaults, malformed lines ignored; a file
is this format iff its first non-comment line is `format <n>`, and must
then contain `pos`. **New fields go in as new keys — never rename or
reuse an existing key.**

`player_data_parse` falls through to five frozen legacy binary layouts
(`PlayerDataLegacy` → `PlayerDataBinary` in player_data.cpp) when no
`format` line is present. Those structs are frozen — never edit them;
`long` stays `long` because it was 4 bytes in WASM/Windows saves and 8
in Linux saves. A fresh spawn (`spawn_pending`) happens only when no
layout parses.

## `world.dat`

A single `int64` — the server's world clock in ticks. Written by
`ServerLogic` (periodic flush + shutdown) and read on `init`; clients
don't write it (their clock lives in `player.dat`'s `world_time`). See
`docs/multiplayer.md`.

## Migration rules

Block id `1249` (`LEGACY_EMPTY_BLOCK_ID`) was the empty-slot sentinel in
pre-2047 saves and is permanently reserved — never reuse it. On load:

- In **chunks**, `1249` becomes `AIR` (or `LAST_BLOCK_ID` under
  `expect_empty_blocks`, so the server's diff map stays clean), and any
  id `>= LAST_BLOCK_ID` sanitizes to `AIR` with a log line.
- In **inventory/chest slots**, `save_slot_id_is_empty` (id `1249` or
  anything failing `item_id_valid`) clears the slot to
  `{ LAST_BLOCK_ID, 0 }`.
- `display_id == 1249` falls back to `id`; layouts without `display_id`
  (record size ≤ 16) get `display_id = id`.

## When persistence is bypassed

- **Multiplayer**: once a hostname is given (`multiplayer_mode()`),
  local `mobs_*`/`items_*` persistence is off for the session — entities
  belong to the server (`docs/entities.md`, `docs/multiplayer.md`).
  Chest/chunk state is server-side too.
- **Block world** (`REPGAME_BLOCK_WORLD`) touches no files at all —
  `MapStorage` methods early-out and no map dir is created.
- **Flat worlds** keep chunk saves (the sandstone floor is regenerable,
  edits persist) but the generator path is fixed.
- **Capture/fly/walktest** (`RunPolicy`) and some mob-test modes skip
  `player.dat` writes so scripted teleports don't clobber the save.
- Queued `BlockUpdateEvent`s are never persisted (`docs/redstone.md`) —
  only the resulting cells.
