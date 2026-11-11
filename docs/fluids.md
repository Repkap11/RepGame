# Fluids (water & lava)

Any block with `flows != 0` in its JSON is a fluid (`block_is_fluid`,
`include/common/fluid.hpp`) — currently WATER and LAVA. The flow sim is
`FluidUpdateEvent` (`src/common/block_update_events/FluidUpdateEvent.cpp`),
one delayed event per cell riding the shared `BlockUpdateQueue`
(`docs/redstone.md`). Per-fluid tuning lives in `data/blocks/*.json`:
`flows` (per-step delay in ticks at `UPS_RATE` — water 36 ≈ 0.25 s,
lava 216 = 1.5 s, the MC speeds), `flow_range` (max flowing level —
7 water, 3 lava), `flow_slope` (how far sideways-spread hunts for a
drop — 4/2) and `forms_sources` (the infinite-water rule, water only).

## Storage

The flow level lives in `BlockState::rotation` — 3 bits, so
`FLUID_MAX_LEVEL` = 7 caps every `flow_range`. Level 0 is a source,
1..`flow_range` is flowing (one level per horizontal block from the
feed); `fluid_level()`/`fluid_state()`/`fluid_in_cell()` are the
canonical accessors. A waterlogged cell always reads as level-0 water —
`fluid_in_cell` reports WATER for it even though its id is the host
block. Sources are rotation 0, so every fluid cell saved before levels
existed loads as a source: old flooded water becomes permanent rather
than draining. The chunk `BlockCell` already carried those rotation
bits (and `waterlogged`), so save and wire formats are unchanged.

## Update model

`fluid_schedule_update(state, pos)` queues a `FluidUpdateEvent` for a
fluid cell `def->flows` ticks out — or `+1` tick when a lava cell can
already react with adjacent water, so obsidian/cobblestone feel
instant. `RepGameState::pending_fluid_updates` dedupes: at most one
pending update per cell.

Two things schedule it:

- A face-adjacent `BlockNextToChangeEvent` — the shared neighbor wake
  fired by every `PlayerBlockPlacedEvent` schedules an update for each
  of its two positions when it holds fluid (`BlockNextToChangeEvent.cpp`,
  manhattan-1 wakes only; the diagonal dust wakes don't reach fluids).
- The random-tick rescue in `World::process_random_ticks`: a sampled
  cell with `fluid_level > 0` gets re-scheduled. Events are never
  persisted, so flow outliving its queue (chunk unload, frozen
  multiplayer flow) resumes within one random tick — 24 samples per
  62³ chunk per tick, ~69 s per cell. Sources are stable and skip this.
  The rescue inherits the random-tick gates: off in capture/fly mode,
  the block world and flat worlds, and while flying/no-clipping.

All writes a fluid event makes are themselves `PlayerBlockPlacedEvent`s
on the queue, so every write re-wakes its face neighbors — a flow
advances one step per `flows` ticks and the queue drains when the
shape settles.

## Sim rules

`FluidUpdateEvent::performAction`:

- **Level recompute** (flowing cells only; sources skip it). A cell
  with the same fluid above is part of a falling column and stays
  level 1. Otherwise the desired level is `best + 1`, where `best` is
  the weakest feed among the four horizontal same-fluid neighbors
  (`fluid_fed_effective` — sources feed 0, and so do cells with fluid
  above them, which carry source strength sideways). `best >
  flow_range - 1` means nothing feeds it strongly enough: the cell
  recedes to AIR. A desired level that differs from the stored one is
  written back and the event returns — the neighbor wakes reschedule
  the cell, so spread always runs against the corrected level.
- **Down first.** If `fluid_fallable` (the cell below doesn't hold this
  fluid and would accept a level-1 write), the below cell gets a
  level-1 write — a falling column, not a spread.
- **Sideways, gated.** Sideways spread needs `grounded` — the cell
  can't keep descending *and* the cell below is a wall: any same-fluid
  cell below (a live fall or a still pool) deliberately does not
  ground, or every mid-column cell would spill sideways as a level-1
  curtain. Sources bypass the gate and spread while they fall.
  Sideways writes go out at `eff + 1`, `eff` being 0 for falling
  columns and the cell's own level otherwise.
- **Drop-seeking.** The direction pick runs `fluid_slope_distance`
  per direction: a BFS (≤ `flow_slope` steps, over
  `fluid_passable` cells) for the nearest cell that can descend.
  With no drop in reach the flow spreads all four ways; otherwise only
  toward the closest drop(s). This is what makes water pour over the
  nearest edge instead of fanning out evenly.
- **Acceptable targets** (`fluid_accepts`): an improving level for the
  same fluid, or a cell empty of either fluid that is `can_be_placed_in`
  or `breaks_in_liquid && !waterloggable && !waterlogged` — the
  break-in-liquid write carries `BreakDrops::Survival` for water
  (plants pop with drops) and `BreakDrops::None` for lava (drops burn).
  Waterlogged and waterloggable cells, the other fluid and unloaded
  cells are walls, so flow never waterlogs anything — only an
  explicit source-level water write waterlogs a cell, in
  `block_state_normalize` (`Chunk::set_block`).
- **Infinite sources** (`forms_sources`): a fed cell whose best feed
  is 0 with at least two *plain* source neighbors (level 0, no same
  fluid above) over a `collides_with_player` or level-0 cell below
  snaps to level 0 — MC's renewable water.

## Lava ↔ water reactions

- Lava with water in any horizontal neighbor or the cell above
  (`lava_touching_water`) petrifies in place: a source becomes
  OBSIDIAN, a flowing cell COBBLESTONE. The scheduler already
  shortens the delay to 1 tick when the contact is visible.
- Lava directly *over* water writes STONE into the water cell and
  doesn't fall — pouring lava on a pond crusts the surface.
- Water and lava can never write over each other
  (`fluid_accepts` rejects both directions); only the lava changes.

## Buckets

`repgame_use_bucket` (`src/common/game/bucket.cpp`) runs the three
bucket items in the right-click chain (`docs/player.md`). A held
bucket arms a fluid raycast in
`RepGame::update_block_selection` — `RayTraversal::find_block_from_to`
filling `BlockSelection::pos_fluid` / `pos_fluid_create` /
`fluidSelectionInBounds`. The empty `BUCKET` uses `pick_mode 3`, which
hits only source cells — flowing cells are transparent (MC's
SOURCE_ONLY), so a fall or shallow flow can't shadow the source behind
it; a filled bucket uses `pick_mode 2`, which stops at any fluid cell
so a pour can aim at the flow itself. An empty `BUCKET` scoops only a plain
source (`fluid_level == 0`, not waterlogged — the water under a slab
can't be drained); a filled `WATER_BUCKET`/`LAVA_BUCKET` pours
`fluid_state(fluid, 0)` into the hit flowing cell, `pos_fluid_create`,
or `pos_create` like a placed block — water poured onto a
waterloggable block waterlogs it via `block_state_normalize`. Survival
swaps the held item empty↔full (`bucket_swap_item`, in-hand swap like
MC, then the rest of the hotbar, then the survival inventory — only a
completely full inventory throws the result ahead); creative doesn't
consume. All world
changes go through the queue, so scoop-removal recedes the flow
naturally and poured sources sim normally.

## Rendering

Fluid surfaces render per-cell at MC's average-corner heights —
`docs/rendering.md` "Sloped fluids" has the full picture. In short:
`fluid_pack_corners` computes each cell's four corner heights from
`fluid_corner_height` (1/64 units, `FLUID_HEIGHT_UNITS`), packed into
`BlockCoords::fluid_h0`/`fluid_h` and flagged `BLOCK_COORDS_FLUID` in
`face_shift` (fluids never yaw — rotation is the sim level). The
vertex shader drops the `vd_data_water` shell cube's top verts onto
the corner heights, rotates top UVs onto `v_flow_dir` so the flow
strip scrolls downhill, and scales side-strip v by surface height.
`fluid_face_vis_mask` recomputes per-face visibility on the CPU in
`Chunk::calculate_populated_blocks` because water/lava's `no_light`
is `NO_LIGHT_BRIGHT` — indistinguishable from lit in phase 1.
All-source-height corners (`FLUID_CORNERS_FLAT`) keep the still top
texture and greedy merge; anything else emits 1x1 with the flow
texture on top. The same helpers also emit the water shell instance
for a waterlogged cell at the top of a water column.
`fluid_surface_height(world, pos)` is the gameplay-side twin (swim
check, underwater-render check) — 1.0 under a fluid column, else the
level height.

## Multiplayer

Fluids follow the causer-simulates rule (`docs/redstone.md`): the
client whose action changed the world runs the `FluidUpdateEvent`s;
remote `BLOCK_UPDATE`s write cells without queueing neighbor events.
Every fluid write goes through `PlayerBlockPlacedEvent` →
`multiplayer.set_block`, so peers receive each level/flow cell as an
ordinary block change. A flow freezes when its simulating client
leaves; another client's random tick re-arms the stranded flowing
cells.

## Known limits

- `flow_range` is capped at `FLUID_MAX_LEVEL` = 7 — a wider range
  needs a wider storage field than the 3-bit `rotation`.
- Buckets can't scoop waterlogged cells, and the mode-1 float
  raycast (lily pads / `can_float_on_water` placement) tests
  `id == WATER` only — waterlogged cells don't count as water there.
- The translucent-pass `split` compositing still cuts at the fixed
  sea-level plane (`docs/rendering.md`), so elevated or flowing
  surfaces misorder against glass the same way ponds do.
- The random-tick rescue doesn't run in flat/capture/fly worlds
  (same gate as crop growth); flows there move only on neighbor
  wakes — which is what the scripted scenario exercises.

## Tests

`test_fluids` (`src/common/tests/test_fluids.cpp`, `make tests
TESTS=fluids`) drives the event directly — level recompute, spread,
slope-seeking, reactions, `forms_sources`, corner heights. The
scripted end-to-end flow lives in `tests/scenarios/fluid_flow.rds`.
