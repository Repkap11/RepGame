# Redstone, mechanisms & block events

All delayed block behaviour — redstone power propagation, water spread,
support checks, gravity releases, piston/door moves — runs through one
scheduled-event system. There is no per-cell redstone sim: a block write
wakes its neighbours, each neighbour recomputes its own state, and any
change queues another write. Power levels live on the cells
(`BlockState::current_redstone_power`, held in `Chunk::extras` when
nonzero), so a chunk reload restores the whole powered network without
replaying anything.

## The event queue

`BlockUpdateQueue` (`include/common/BlockUpdateQueue.hpp`,
`RepGameState::blockUpdateQueue`) is a priority queue of
`BlockUpdateEvent`s ordered by `tick_number`, drained once per tick by
`RepGame::process_block_updates` (`src/common/game/player.cpp`) near the
top of `RepGame::tick`. Events are pooled — `addBlockUpdate<E>(args...)`
placement-news them into fixed slots carved from seed_fifo's shared 4KB
block pool, so a fluid drain or TNT chain costs no heap traffic. Events
fire only when their `tick_number` is
*strictly less* than the current tick — so an event tagged "now" lands
next tick, and `tick + n` means at least `n` full ticks of delay.
`REDSTONE_DELAY` (1) is therefore a 2-tick hop per neighbour. Events are
never persisted: a queued fuse or pending release is lost on save (a
button saved while pressed is revived by the re-queued release in
`player.cpp` — see "Buttons" below).

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
(weak or hard) — that's the inverter.

**Buttons** (`is_button`) press on right-click (`player.cpp` queues a
pressed state update with `REDSTONE_SOURCE_POWER`, plus a release event
at `tick + UPS_RATE` or `+1.5*UPS_RATE` for wood — the release is
re-queued every press so a saved-pressed button still pops out).
**Pressure plates** (`is_pressure_plate`) press while stood on:
`RepGame::process_pressure_plates` detects a foot AABB overlap per tick
and starts a self-rescheduling `PressurePlateEvent` chain
(`PRESSURE_PLATE_RECHECK_TICKS` = 8 ticks between checks), deduped by
`RepGameState::watched_pressure_plates` so exactly one chain exists per
plate. Both strongly power their attachment block.

**Pistons** (`is_piston`/`is_sticky_piston`) move on a power edge:
`PlayerBlockPlacedEvent` queues `PistonEvent` one tick out when a
piston's power crosses 0 either way. Powered extends: scan up to
`PISTON_PUSH_LIMIT` (12) solid movable cells until a free cell (air or
`can_be_placed_in`) or a destroyable non-solid (broken into debris);
unloaded cells and `piston_immovable`/powered-piston/head cells block
the push. The arm itself is an `is_piston_head` block
(`PISTON_ARM_COLLISION`/`STICKY_PISTON_ARM_COLLISION`) written into the
cell in front, kept alive by `block_can_survive_at`'s base check.
Unpowered retracts the head, and sticky pistons pull the block that was
in front. Every moved cell goes through `piston_set_block`, which writes
+ relays + queues neighbour updates, so redstone and support re-evaluate
around the whole moved chain.

**Doors** are `is_redstone_mechanism` blocks whose power edge queues
`DoorEvent(SYNC_POWER)`: combined lower|upper power rising opens,
falling closes, and a hand toggle in between persists (MC edge
semantics). `DoorEvent(TOGGLE)` is the right-click path. Placement,
pairing and hinge rules: `docs/blocks.md`.

**TNT** (`is_tnt`) ignites on a *rising* edge in the same event that
applies the power change: `tnt_ignite` removes the block, spawns a
primed body with a small hop, and queues `TntExplodeEvent` at
`tick + TNT_FUSE_TICKS`. `is_inverse_tnt` blasts restore terrain instead
of destroying it. The body physics, blast and multiplayer rules are in
`docs/entities.md`.

**Falling blocks** (`falls_with_gravity`): `perform_checks` queues
`FallingBlockEvent` 2 ticks out when a gravity block sits over a loaded
non-colliding cell — the delay lets a quick support restore win. Sim and
sync: `docs/entities.md`.

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
leaf decay). Two-cell blocks (doors, tall plants) drop only while their
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
comment for the canonical example. `test_doors` (same file) and
`test_falling_blocks`/`test_tnt` (`test_blocks.cpp`) exercise the same
queue.
