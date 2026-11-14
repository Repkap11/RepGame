# Redstone, mechanisms & block events

All delayed block behaviour — redstone power propagation, water spread,
support checks, gravity releases, piston/door moves — runs through one
scheduled-event system. There is no per-cell redstone sim: a block write
wakes its neighbours, each neighbour recomputes its own state, and any
change queues another write. Power levels live on the cells
(`BlockState::current_redstone_power`, held in `Chunk::extras` when
nonzero), so a chunk reload restores the whole powered network without
replaying anything.

## The two clocks

RepGame runs two clocks. The **physics clock** is `tick_number` itself,
incremented once per update at `UPS_RATE` (144/s). The **redstone clock**
is Minecraft's 20 Hz game tick ("gt"):
`redstone_tick_now(state) = tick_number * REDSTONE_UPS / UPS_RATE`
(`include/common/block_update_events/schedule.hpp`) — a pure function of
`tick_number`, so there is no persisted or synced state for it. Each
clock has its own `BlockUpdateQueue`: `RepGameState::blockUpdateQueue`
for physics events, `RepGameState::redstoneQueue` for redstone events.

`repgame_drain_block_updates` runs once per physics tick from
`RepGame::process_block_updates` (`src/common/game/player.cpp`) after
the random ticks: first the physics queue, then the redstone queue at
the current redstone time. The two queues have *different* firing
semantics:

- Physics (`processAllBlockUpdates`) fires events with `tick_number`
  *strictly less* than now — an event tagged "now" lands next tick.
- Redstone (`processUpTo`) first pops every event with `tick <= now`
  into a local batch, then runs the batch — so an event queued while
  the batch runs, even one due now, waits for the next drain.

A delay of 0 therefore means "the next drain" on either clock: physics
delay 0 fires next physics tick, redstone delay 0 fires on the next
game-tick drain (which happens every physics tick, so dust stays at
about two physics ticks per hop). Explicit delays on the redstone queue
are what wait for game-tick boundaries.

## Scheduling

`include/common/block_update_events/schedule.hpp` (included from .cpp
files only) is the only way to queue an event:

- `schedule_block_update<E>(state, delay, args...)` — physics clock,
  fires `delay` ticks out; 0 = next tick.
- `schedule_redstone_update<E>(state, delay_gt, args...)` — redstone
  clock, fires once `redstone_tick_now` reaches now + `delay_gt`; 0 =
  the next game-tick drain.

**Never schedule from `this->tick_number`** — inside an event it is in
whichever base queued it, so it can't seed a target time on either
clock. `BlockUpdateEvent::performAction(RepGameState&)` takes no queue,
which forces every site through these helpers with an explicit clock.

`queue_neighbor_block_updates(state, pos)` — the neighbour wakes every
block write fans out — always schedules on the physics clock with delay
0, whichever queue the write came from.

Events are pooled — `addBlockUpdate<E>(args...)` placement-news them
into fixed slots carved from seed_fifo's shared 4KB block pool, so a
fluid drain or TNT chain costs no heap traffic. Events are never
persisted: a queued fuse or pending release is lost on save (a button
saved while pressed is revived by the re-queued release in
`player.cpp` — see "Buttons" below).

Which events wait on which clock:

| Site | Clock | Delay |
|---|---|---|
| `perform_checks` merged power/display write | redstone | 0 |
| ...same write when the affecting torch's power changes | redstone | `REDSTONE_TORCH_DELAY_GT` (2) |
| `perform_checks` support kill (AIR write) | physics | 0 |
| `FallingBlockEvent` | physics | 2 |
| `LeafDecayEvent` | physics | jittered |
| `FluidUpdateEvent` | physics | `fluid_schedule_update` |
| `RepeaterEvent` schedule/re-arm | redstone | `2 * delay` |
| `RepeaterEvent` on/off write | redstone | 0 |
| `PistonEvent` on a power edge | redstone | 0 |
| `PistonLandEvent` per moved cell | redstone | `PISTON_MOVE_GT` (2) |
| `DoorEvent(SYNC_POWER)` | redstone | `DOOR_DELAY_GT` (1) |
| `tnt_ignite` fuse | physics | `TNT_FUSE_TICKS` |
| Button press write | physics | 0 (instant) |
| Button release | redstone | 20 / 30 gt |
| `PressurePlateEvent` start/write/recheck | redstone | 0 / 0 / `PRESSURE_PLATE_RECHECK_GT` (20) |
| Right-click toggles, buckets, harness writes, place/break | physics | 0 |
| `TntExplodeEvent` | physics | unchanged |
| Neighbour wakes (all writes) | physics | 0 |

The event types live in `src/common/block_update_events/`:

- `PlayerBlockPlacedEvent` — the universal "write this BlockState here"
  carrier. `state_update=false` is a real place/break (survive-checks,
  replaces `can_be_placed_in` cells only, spawns debris); `true` is an
  in-place mutation of the same block (power/display/rotation change —
  dropped entirely if the block meanwhile changed id, so a stale update
  can't resurrect a broken block). It validates
  (`player_placement_valid`, `block_can_survive_at`), writes via
  `World::set_loaded_block` + `multiplayer.set_block`, then fans out
  `queue_neighbor_block_updates` — the 6 face neighbours plus the
  diagonal dust cells whose corner gates them (see "Dust"). It also owns
  the edge triggers: door power sync, piston extend/retract, TNT
  ignition (see "Mechanisms").
- `BlockNextToChangeEvent(tick, pos, offset)` — "something changed at
  `pos`, re-evaluate `pos+offset`". `performAction` runs `perform_checks`
  in *both* directions: the neighbour re-derives its own state, and the
  changed cell re-checks the neighbour's effect on it (a block broken
  under a torch must tell the torch, not just be told).
- `FluidUpdateEvent` — the water/lava flow sim, scheduled by
  `fluid_schedule_update` on face-adjacent wakes and by the random-tick
  rescue. The whole model lives in `docs/fluids.md`.
- `RepeaterEvent` — delayed power re-emission for repeaters; see
  "Repeaters" below.
- `FallingBlockEvent`, `DoorEvent`, `PistonEvent`,
  `PressurePlateEvent`, `TntExplodeEvent` — mechanism-specific; below.

## The power model

`perform_checks` (`BlockNextToChangeEvent.cpp`) recomputes one cell's
`current_redstone_power` from scratch on every neighbour update, merges
it with the dust-shape recompute into a single `PlayerBlockPlacedEvent`
state update, and only queues when the merged state differs — that's
what terminates propagation (a ring settles when no recompute changes
anything). MC's two power strengths map onto the stored level:

- **Hard (>1) power** is emitted by sources: `initial_redstone_power`
  blocks (redstone block — always emits at that level), powered redstone
  torches (`REDSTONE_TORCH_POWER` = 15), and pressed buttons/plates
  strongly powering the block they're attached to
  (`activator_attached_to`). A torch also strongly powers the block
  directly above it. Hard power is what dust reads.
- **Weak (1) power** is what a solid block gets from powered dust whose
  shape points into it (`dust_points_toward`), dust directly above it
  (always feeds down), or a redstone torch beside it that isn't attached
  to it. Weak power turns torches off and activates mechanisms but is
  *not* read by dust.
- **Mechanisms** (`is_redstone_mechanism`: pistons, lamps, doors, TNT)
  activate next to any powered emitting block — powered solids, pressed
  buttons/plates — but their stored level is capped at 1 ("activated,
  not emitting"), so two adjacent mechanisms can't latch each other.
  Only `is_solid` blocks hold power at all; air/water/glass and powered
  mechanisms themselves store 0 for transmission purposes.

Which blocks participate is data-driven by `Block` fields
(`include/common/block_definitions.hpp`): `is_redstone_dust`,
`transmits_redstone_power` (dust), `connects_to_redstone_dust` (blocks
dust visually connects to), `initial_redstone_power`,
`affected_by_redstone_power` (solids that can hold weak power),
`is_solid` (can hold hard power — note pistons are opaque but non-solid
so they never transmit), `is_torch`, `is_button`, `is_pressure_plate`,
`is_redstone_mechanism`, `is_piston`, `is_sticky_piston`,
`is_piston_head`, `piston_immovable`, `is_tnt`, `is_inverse_tnt`.

## Dust

Dust is the only block whose *display* differs from its id:
`display_id` is one of the `REDSTONE_WIRE_*` shapes (DOT, LINE_1/2,
L_Q1-4, T_B/R/F/L, CROSS) recomputed in `perform_checks` from
`connects_to_redstone_dust` neighbours plus the gated diagonals — dust
climbs onto a solid side block while the corner cell above it is open,
and drops through a non-solid side cell. `queue_neighbor_block_updates`
wakes those same diagonal dust cells (an up-diagonal only while the
corner above is open, a down-diagonal only while the side is non-solid),
because a corner filling up both re-shapes the dust and cuts its power
edge.

Power: `compute_dust_power` BFS-walks the *connection graph*
(`dust_feeders`: shaped horizontal edges plus the same gated diagonals)
and takes `max(adjacent_source_power - hops)` — sources via
`dust_adjacent_source_power` skip dust, weakly-powered blocks, and
torches attached to the cell itself. Other dust's stored power is
deliberately ignored: during propagation it's stale, and reading it let
loops keep each other lit (or flicker) instead of settling. Right-click
with dust in hand toggles a connected dust between CROSS and DOT
(`player.cpp`); a DOT feeds only the block below.

## Mechanisms

**Torches** (`is_torch`, `REDSTONE_TORCH`) emit hard power 15, strongly
power the block above, weakly power their other sides, and never power
their attachment (rotation 0=below, 4..7=attached to a horizontal face).
Their own power level is `0` when the attachment block is powered at all
(weak or hard) — that's the inverter. State changes wait
`REDSTONE_TORCH_DELAY_GT` (2 gt): when a neighbour write lands on a cell
whose update was triggered by a torch's own power change, `perform_checks`
queues the merged write at that delay instead of 0.

**Buttons** (`is_button`) press on right-click (`repgame_press_button`
in `player.cpp`): the pressed state update with `REDSTONE_SOURCE_POWER`
is instant (physics clock), while the release waits on the redstone
clock — `STONE_BUTTON_PRESS_GT` = 20 gt for stone,
`WOOD_BUTTON_PRESS_GT` = 30 gt for wood. The release is re-queued every
press so a saved-pressed button still pops out.
**Pressure plates** (`is_pressure_plate`) press while stood on:
`RepGame::process_pressure_plates` detects a foot AABB overlap per tick
and starts a self-rescheduling `PressurePlateEvent` chain
(`PRESSURE_PLATE_RECHECK_GT` = 20 gt between checks, about a second of
MC release time), deduped by `RepGameState::watched_pressure_plates` so
exactly one chain exists per plate. Both strongly power their
attachment block.

**Pistons** (`is_piston`/`is_sticky_piston`) move on a power edge:
`PlayerBlockPlacedEvent` queues `PistonEvent` at redstone delay 0 when a
piston's power crosses 0 either way. The event first lands every in-flight
move of that base (`piston_finish_moves` — MC's finish-on-reverse), then
starts the slide. Powered extends: scan up to `PISTON_PUSH_LIMIT` (12) solid
movable cells until a free cell (air or `can_be_placed_in`) or a destroyable
non-solid (broken into debris); unloaded cells and `piston_immovable`/
powered-piston/head/`MOVING_PISTON` cells block the push.

Each destination cell gets a `MOVING_PISTON` (id 243) placeholder — an
invisible, `piston_immovable` block whose `display_id`/`rotation` encode the
carried block as a fallback — while the full carried `BlockState` (so a
pushed repeater keeps its delay) sits in `RepGameState::piston_moves` beside
a `BODY_PISTON` kinematic body that slides source→destination over
`PISTON_MOVE_GT` (2 gt) on the redstone clock. The start writes are
local-only (`piston_set_local`: world write + neighbour wake, no relay);
`PistonLandEvent` per touched cell lands the carried state and relays then,
so remote clients snap straight to the final cells. A land event on an
unloaded cell just drops its table entry. Stale `MOVING_PISTON` cells (lost
queue on unload, or a write that raced the slide) are rescued by the random
tick in `world.cpp` through the fallback encoding, and all in-flight moves
land in `cleanup` so `MOVING_PISTON` never persists.

Unpowered retracts: the head slides back into the base and is discarded at
arrival; sticky pistons pull the block that was in front into the vacated
head cell as a `MOVING_PISTON` move, and the pull cell relays AIR. The arm
itself is an `is_piston_head` block
(`PISTON_ARM_COLLISION`/`STICKY_PISTON_ARM_COLLISION`) kept alive by
`block_can_survive_at`'s base check. Every touched cell queues a
physics-clock neighbour update, so redstone and support re-evaluate around
the whole moved chain; the body physics and rendering are in
`docs/entities.md`.

**Doors** are `is_redstone_mechanism` blocks whose power edge queues
`DoorEvent(SYNC_POWER)` `DOOR_DELAY_GT` (1 gt) out: combined lower|upper
power rising opens, falling closes, and a hand toggle in between
persists (MC edge semantics). `DoorEvent(TOGGLE)` is the right-click
path, immediate on the physics clock. Placement,
pairing and hinge rules: `docs/blocks.md`.

**TNT** (`is_tnt`) ignites on a *rising* edge in the same event that
applies the power change: `tnt_ignite` removes the block, spawns a
primed body with a small hop, and queues `TntExplodeEvent` at
`tick + TNT_FUSE_TICKS`. `is_inverse_tnt` blasts restore terrain instead
of destroying it — TNT caught in the restore sphere still chain-ignites.
The body physics, blast and multiplayer rules are in
`docs/entities.md`.

**Falling blocks** (`falls_with_gravity`): `perform_checks` queues
`FallingBlockEvent` 2 ticks out when a gravity block sits over a loaded
non-colliding cell — the delay lets a quick support restore win. Sim and
sync: `docs/entities.md`.

**Powered rails** (`POWERED_RAIL`) are mechanisms that also power down the
track: with no direct activation (the `block_neighbor_power` scan every
mechanism runs), `perform_checks` sets power 1 when
`powered_rail_chain_powered` (`src/common/rail.cpp`) finds a directly
activated powered rail within 8 connected hops — a BFS that follows shape
endpoints, slope rises included, and reads only direct activation
(`mechanism_direct_power`), never stored rail power, so a powered loop
can't hold itself on. Powered rails swap their top tile to
`rail_golden_powered` in `block_adjust_coord_based_on_state` (same trick
as the lit lamp). Shape/track rules are in `docs/blocks.md`.

**Repeaters** (`is_repeater`, `REPEATER`) are diodes: they read power
only from the block *behind* them, wait a configured delay, then emit
hard power 15 out the front — refreshing the signal and strongly
powering the cell in front. Facing is `rotation & 3` (0=-z, 1=+x,
2=+z, 3=-x) with output toward facing; `repeater_output_dir`/
`repeater_points_into` in `src/common/repeater.cpp` hold the mapping.
The delay (1–4) lives in `display_id`, swapped between
`REPEATER` and the hidden render-only `REPEATER_DELAY_2/3/4` ids — the
id never changes, only the display. Each delay step is
`REDSTONE_REPEATER_STEP_GT` (2) game ticks, so delay-4 waits
`repeater_delay_gt` = 8 gt. Right-click cycles 1→2→3→4→1
(`player.cpp`, skipped while sneaking).

Repeater state changes don't run through `perform_checks` — they need a
real delay, so `BlockNextToChangeEvent::performAction` schedules a
`RepeaterEvent` for any repeater at either of its two positions
(deduped by `RepGameState::pending_repeater_ticks`, one pending event
per cell). When it fires it re-reads the input side: input still powered
(or the output already on) writes the matching power level; a powered
output with input now low writes 0. The one MC diode quirk: an
*unpowered* repeater turns on even if the input already went away, then
re-arms itself for another delay — so a 1-tick input pulse still
produces a full delayed output pulse. `perform_checks` leaves a
repeater's stored power untouched; only `RepeaterEvent` changes it.

Power reads honour the diode direction: `dust_adjacent_source_power` and
`block_neighbor_power` treat a repeater as a source *only* when
`repeater_points_into` the cell being read — a repeater powering its
output block hard-powers it at 15, while blocks beside or behind it see
nothing. Dust connects visually to a repeater only along its axis (in
front or behind), so side dust stays a cross and gets no power.

## Other event behaviours

`perform_checks` also drives non-redstone reactions: **leaf decay**
(each `BlockNextToChangeEvent` ends by queueing a jittered
`LeafDecayEvent` for either of its two positions holding natural leaves,
deduped through `RepGameState::pending_leaf_decay` — see
`docs/blocks.md`), **fluid updates** (either of the event's two
positions holding a `flows != 0` cell — but only on manhattan-1 wakes —
gets a `FluidUpdateEvent` via `fluid_schedule_update`, deduped through
`RepGameState::pending_fluid_updates`; the flow sim, levels and
reactions are in `docs/fluids.md`) and **support kills** (`block_can_survive_at` fails → an AIR
write: torches losing their attachment, doors/piston-heads losing their
partner, `needs_place_on_any_solid` faces). Support kills carry
`BreakDrops::Survival`: the queued `PlayerBlockPlacedEvent` AIR write
spawns the broken block's drops through `break_drops`
(`include/common/break_drops.hpp`, shared with the player break path and
leaf decay) — a fluid write destroying a `breaks_in_liquid` block rolls
drops the same way (`docs/fluids.md`). Two-cell blocks (doors, tall plants) drop only while their
partner cell still holds the partner — whichever half goes second sees
an empty partner, so a pair yields exactly one item. `BreakDrops` modes:
`None` (default), `Survival`, `Always`. Right-click interactions
(buttons, chest open, crafting table, gate/door toggles, dust dot
toggle) all live in the right-click chain in
`src/common/game/player.cpp`; sneak+right-click skips them and places
instead.

## Multiplayer note

Events only run on the client that caused them. Remote `BLOCK_UPDATE`s
write cells directly (`handle_frame`) without queueing neighbour events,
so each peer's redstone is advanced by whichever client owns the action
that changed it — matching the "causer simulates" rule for bodies
(`docs/entities.md`).

## Tests

`test_redstone` (`src/common/tests/test_redstone.cpp`, `make tests
TESTS=redstone`) builds a torch/dust/piston/lamp scenario and asserts
the MC semantics listed at the top of that file — read its header
comment for the canonical example. `test_repeaters` (same file,
`TESTS=repeater`) covers facing, delay cycling, pulse extension and
hard power. `test_redstone_clock` (same file) covers the clock itself:
the `redstone_tick_now` mapping, delay-0 and delay-n firing semantics,
dust speed, and the torch/piston/button/plate game-tick delays.
`test_doors` (same file) and
`test_falling_blocks`/`test_tnt` (`test_blocks.cpp`) exercise the same
queues. Tests drive the clocks with `test_run_game_ticks` /
`test_run_redstone_ticks` (`src/common/tests/test_util.hpp`).
