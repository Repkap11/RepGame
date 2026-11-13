# Persistence & save formats

All on-disk state goes through `MapStorage`
(`include/common/utils/map_storage.hpp`,
`src/common/utils/map_storage.cpp`). The storage root is
`getRepGamePath()` (`src/common/utils/file_utils.cpp`): `$HOME/.repgame`
on Linux, `.` on Android/Windows, the WasmFS virtual filesystem root on
WASM. Below it, one directory per world (`MapStorage::init` mkdirs it),
named by the `world_name` argument.

To read these files without starting the game, use the offline inspector:
`RepGame inspect <world> <sub>` or `rep-dbg inspect` (block/slice/column/
chunk/entities/player — all through the real loaders, read-only; see
`src/common/harness/inspect.cpp` and docs/debug-tools.md).

```
<root>/<world>/
    world_type              optional; "flat" selects the flat generator
    player.dat              player position, inventories, settings
    world.dat               server-side world clock (int64)
    chunk_<x>_<y>_<z>       player-edit runs per chunk (v1); legacy saves
                            were a full RLE BlockState volume
    chest_<x>_<y>_<z>       chest contents for a chunk
    furnace_<x>_<y>_<z>     furnace contents+progress for a chunk
    sign_<x>_<y>_<z>        sign text for a chunk
    mobs_<x>_<y>_<z>        mob records for a chunk
    items_<x>_<y>_<z>       dropped-item records for a chunk
```

The `world_type` marker file is read once by
`MapStorage::detect_flat_world` before any generation; `flat` worlds get
a sandstone floor and skip normal terrain (see `docs/mapgen.md`,
`docs/models.md` — `tools/model_to_world.py` writes it).

## Chunk files (`chunk_x_y_z`)

One file per chunk, written only when `chunk.dirty` — and holding only
the cells runtime edits touched, as sorted non-overlapping runs over
generated terrain (v1):

```
u32 magic                    // 0x4B434752 ('RGCK')
u32 version                  // 1
u32 record_size              // sizeof(BlockState) at write time
u32 run_count
run: { u32 start, u32 count, BlockState state }   // record_size bytes each
```

Every cell not inside a run is natural terrain (MapGen + StructureGen)
and is regenerated rather than read back: `Chunk::load_terrain` always
generates the chunk first, then overlays the saved runs and marks the
touched cells in `chunk.edited` (a bitset in the same
`CHUNK_BLOCK_SIZE`, halo-inclusive index space). A missing file means
"no edits", not "no chunk" — `check_if_chunk_exists`/`load_chunk_edits`
return 0 and the chunk is pure generation.

`record_size` is the forward/backward-compat mechanism: readers copy
only the shared prefix into a `BlockState`, and a file whose record
size differs from this build's sets `ChunkEdits::needs_rewrite` so it is
rewritten in v1 on the next persist. Persisting an empty run set
removes the file.

Writes are atomic: `persist_chunk_edits` writes a sibling `*.tmp` then
renames, so a crash or kill mid-write leaves the previous file intact
rather than a truncated stream that would decode as a shorter edit list.
A file that exists but fails to decode is never rewritten from a partial
or empty read — `load_chunk_edits` fails closed, `ServerLogic` refuses to
merge such a chunk into its edit cache (the new edit is dropped for that
chunk, the on-disk data survives), and `migrate_legacy_chunk_files`
skips it.

Writes happen off the render thread inside
`TerrainLoadingThread::process_value` — a departing slot persists
through the work queue before its memory is released — plus
`Chunk::persist` on shutdown, and `ServerLogic::persistChunk` on the
server (its edit cache is the same `{start,count,state}` run shape).

### Legacy format

Files written before v1 have no magic: a `u32 record_size`
(`sizeof(STORAGE_TYPE)`, 8–14 bytes historically — it can't collide
with the magic) followed by RLE runs `{ BlockState; u32 count }` tiling
the whole volume. `load_chunk_edits` decodes them into runs directly:
cells holding `LAST_BLOCK_ID` or `LEGACY_EMPTY_BLOCK_ID` (the server
diff format's hole sentinels) are skipped as natural. A file with no
holes is an old *client snapshot* (`legacy_snapshot`): stored cells are
compared against regenerated terrain at apply time, so only true
differences become edits — a snapshot identical to natural produces no
edits and is unlinked on persist. Every legacy file sets
`needs_rewrite`, so migration is lazy: chunks rewrite in v1 (or are
deleted) the next time they persist.

Fluid flow levels ride the same rotation bits (`docs/fluids.md`) — no
format change; pre-levels fluid cells (rotation 0) load as sources.

## Container sidecars

Container contents persist per chunk through `load_chests`/`persist_chests`
and `load_furnaces`/`persist_furnaces` — the same files on both sides.
Server-side they're driven generically: `ServerContainers` calls the
`load`/`persist` hooks on each `NetContainerKindDef` row
(`docs/multiplayer.md` "Containers"), converting to/from the generic
`ContainerContents` blob at the boundary. The formats below are unchanged.

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

## Furnace files (`furnace_x_y_z`)

Same envelope as the chest file:

```
u32 record_size              // sizeof(FurnaceFileRecord)
u32 num_furnaces
record: { i32 x, y, z        // world block position
          InventorySlot slots[FURNACE_INVENTORY_SLOTS]  // input/fuel/output
          i32 cook_progress
          i32 burn_ticks }
```

Readers copy the slots prefix and the progress tail independently, so
records from either side of the progress-fields addition still parse.
`persist_furnaces` removes the file when empty, like chests. Both
`FURNACE` and `LIT_FURNACE` blocks share one entry (keyed by position);
the lit state itself lives in the chunk's block data, while
`burn_ticks > 0` is what makes a loaded furnace resume burning.

## Sign files (`sign_x_y_z`)

Same envelope as the chest file (`load_signs`/`persist_signs`):

```
u32 record_size              // sizeof(SignFileRecord)
u32 num_signs
record: { i32 x, y, z        // world block position
          u8 line_len[SIGN_LINES]            // 4
          char text[SIGN_LINES][SIGN_LINE_MAX] }
```

Each record holds one sign's front text — four lines of at most
`SIGN_LINE_MAX` (15) printable-ASCII characters each (`SignText`,
`include/common/sign.hpp`). The shared-prefix read convention applies:
readers copy the `x/y/z` prefix, then whatever of the `line_len`/`text`
tail fits the saved record size, and `sign_sanitize` re-validates every
line on load. `persist_signs` *removes* the file when no non-empty text
remains in the chunk, so erased signs can't resurrect. In-world storage
is `ChunkContainers.signs` keyed by in-chunk block index; the map is
converted to/from world positions at the file boundary. Server-side the
same files are driven by `ServerSigns` (`docs/multiplayer.md` "Signs").

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
and `flags` carries the provenance bits (`NATURAL`/`TOUCHED`) plus the
sheared-sheep state (`SHEARED` — wool stays off until the mob grazes it
back; live anim bits like HURT/DYING/LOVE are masked out). The
appended `i32 grow_ticks` carries breeding state: negative is a baby's
remaining growth ticks (adult at 0), positive is an adult's re-breed
cooldown; pre-`grow_ticks` records zero-fill it (plain adults). The
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
peaceful <0|1>
world_time <int64>
render_distance <int>
min_ambient <float>
health <float>
spawn <x> <y> <z>
hotbar_selected <int>
hotbar <slot> <item_id> <quantity>
survival <slot> <item_id> <quantity>
```

Rules: `#` comments and blank lines skipped, unknown keys ignored,
missing keys keep `PlayerData` defaults, malformed lines ignored; a file
is this format iff its first non-comment line is `format <n>`, and must
then contain `pos`. **New fields go in as new keys — never rename or
reuse an existing key.**

`health` is the vitals health in half-hearts (`docs/player.md`): absent
or negative in old saves means full; a saved 0 means the player quit on
the death screen and loads as a fresh spawn (health restored to max,
`spawn_pending` re-pins the spawn column). The transient fields —
breath, hurt cooldown, dead, fall distance — are not saved.

`spawn` is the bed respawn point set by `repgame_try_sleep`
(`docs/player.md`): the line is written only while a point is set, so old
saves and un-set players respawn at the world-spawn column.

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

- In **chunks**, `1249` is a hole cell (natural terrain) alongside
  `LAST_BLOCK_ID` in legacy files, and any non-sentinel id
  `>= LAST_BLOCK_ID` sanitizes to `AIR` with a log line.
- In **inventory/chest slots**, `save_slot_id_is_empty` (id `1249` or
  anything failing `item_id_valid`) clears the slot to
  `{ LAST_BLOCK_ID, 0 }`.
- `display_id == 1249` falls back to `id`; layouts without `display_id`
  (record size ≤ 16) get `display_id = id`.

## When persistence is bypassed

- **Multiplayer**: while the session is (or was) connected —
  `multiplayer_mode()` is hostname + `connect_pending`/`ever_connected` —
  local `mobs_*`/`items_*` persistence is off: entities belong to the
  server (`docs/entities.md`, `docs/multiplayer.md`). A hostname whose
  connect fails outright leaves a purely local session that persists
  entities normally; a session that connected and then dropped stays in
  multiplayer mode so server entities can't mix into local files.
  The same `multiplayer_mode()` drives `MapStorage::chunk_io_enabled`
  (set in `game_init.cpp` before the first chunk enqueues, refreshed in
  `World::render`): while off, `load_chunk_edits`/`persist_chunk_edits`
  and the chest/furnace/sign file paths do no local IO — chunk, container
  and sign state lives on the server, so no `chunk_*`/`chest_*`/
  `furnace_*`/`sign_*` files are read or written. Chunks already loaded when a connect fails
  outright are *not* reloaded — their saved edits only appear after
  they leave and re-enter the render window.
- **Block world** (`REPGAME_BLOCK_WORLD`) touches no files at all —
  `MapStorage` methods early-out and no map dir is created.
- **Flat worlds** keep chunk saves (the sandstone floor is regenerable,
  edits persist) but the generator path is fixed.
- **Capture/fly/walktest** (`RunPolicy`) and some mob-test modes skip
  `player.dat` writes so scripted teleports don't clobber the save.
- Queued `BlockUpdateEvent`s are never persisted (`docs/redstone.md`) —
  only the resulting cells.
