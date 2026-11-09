# Player: input, movement & interaction

Player state lives in `RepGameState`: `camera` (position, look angles,
velocities, pose heights — `include/common/game/player_state.hpp`),
`input` (`include/common/input.hpp`), `block_selection`,
`block_mining`, `hotbar`. Per-tick work happens in
`src/common/game/game_tick.cpp` (`RepGame::tick`) and
`src/common/game/player.cpp`; this doc covers the desktop keyboard/mouse
path — VR bindings are `docs/vr.md`, and Android/touch has its own input
file under its platform dir. `RepGame::tick` itself is a short ordered
list of named phases: gameplay phases live in `game_tick.cpp`, the
scripted/debug-only phases (capture, fly, walktest, results, script) in
`src/common/game/game_harness.cpp`, and the VR phases in `vr.cpp`.

## Key bindings (Linux/SDL, `src/linux/input.cpp`)

| Input | Action |
|---|---|
| `W`/`A`/`S`/`D`, arrows (also `o`/`k`/`;`/`l`) | Move — folded into `input.movement` (`angleH`: W=0, D=+90, S=180, A=-90, diagonals ±45/±135; `sizeH` analog magnitude) |
| `Space` | Jump; fly up; swim up; climb up |
| `'` (apostrophe) | Sneak; fly down; sink while swimming; hang on climbables |
| `P` (hold) | Sprint |
| `F` | Toggle flying; with the creative inventory open, opens its item-name search box instead (typing filters the palette, `Enter`/`Esc` closes it) |
| `C` | Toggle no-clip (`Ctrl+C` exits) |
| `I`/`E` | Toggle inventory |
| `Esc` | Close inventory, else pause menu |
| `` ` `` (grave) | Toggle creative/survival |
| `R` / `Shift+R` | Cycle `worldDrawQuality` up/down |
| `=`/`-` (numpad ±), `0` | Render distance +/-/reset to `RENDER_RADIUS_DEFAULT` |
| `PgUp`/`PgDn` (`+Shift`) | World time ±1h (±6h with shift) — sends `WORLD_TIME` in MP |
| `Q` / `Ctrl+Q` | Throw one item / the whole stack |
| `M` | Debug block render mode |
| `G` | ImGui debug overlay |
| `V` | Toggle vsync |
| `N` | Spawn a mob (debug) |
| `F12` | Screenshot |
| Left click | Mine / attack mob |
| Right click | Place / interact; `Shift`+right-click places past interactables |
| Middle click | Pick block (survival: pulls to hotbar; creative: gives) |
| Wheel | Hotbar slot; creative-inventory page while inventory is open |

While `menu_open`, key *presses* are swallowed except Esc/inventory
(resume), F12, Ctrl+C; releases still flow so held-key flags stay
consistent. Mouse presses record where they began
(`*_pressed_in_ui`) so a drag started over the UI never mines and a drag
started in the world never moves items.

## Movement (`RepGame::process_movement`)

Tick-rate physics at `UPS_RATE` (144 Hz). The player is an AABB
`PLAYER_WIDTH` wide × `player_height` tall centered on x/z;
`camera.pos` is the *eye*, `eye_height` above the feet.

- **Pose.** Sneak shrinks to `PLAYER_SNEAK_HEIGHT`/`PLAYER_SNEAK_EYE_HEIGHT`
  and drops the eye by `PLAYER_SNEAK_EYE_DROP`; standing back up is gated
  by a `Collision::collides_at` headroom check each tick.
  `eye_height_render` eases toward the target for render smoothing.
- **Horizontal.** Input builds a yaw-plane target velocity
  (`movement_speed` picked from the `MOVEMENT_SENSITIVITY_*` ladder —
  walk/sprint/fly/swim/wade/sneak variants); `horizontal_vel` approaches
  it at a per-tick rate chosen by medium and intent
  (`PLAYER_GROUND/AIR/WATER_ACCEL`, `*_FRICTION`, `PLAYER_FLY_ACCEL`).
  Pitch never steers.
- **Vertical.** `y_speed` integrates `-gravity`
  (`GRAVITY_STRENGTH`, `WATER_GRAVITY_STRENGTH` swimming, 0 flying),
  capped by `TERMINAL_VELOCITY`/`SWIM_TERMINAL_VELOCITY` (flying sets
  `y_speed` directly from jump/sneak at `FLY_VERTICAL_SPEED`). Grounded
  jump needs `standing_on_solid` and applies `JUMP_STRENGTH`; swimming
  eases `y_speed` toward jump/sink targets under `PLAYER_WATER_ACCEL`
  with `WATER_VERTICAL_DRAG` when idle.
- **Swim vs wade.** Swimming requires the *standing* body center below
  the liquid's surface — `fluid_surface_height`, so a shallow flowing
  cell wades rather than swims (`WATER_HEIGHT` when the body cell
  isn't liquid) — a crouched dip doesn't
  flip into swim and oscillate. Feet-in-liquid but body above the
  surface is wading (slower walk, normal physics).
- **Climbing.** While the body AABB overlaps a cell holding a block
  flagged `"climbable"` (`Collision::on_climbable` — ladders; the whole
  cell counts even though the slab geometry is thin), holding jump or
  pressing *forward* into a wall that blocks the move ascends at
  `CLIMB_VERTICAL_SPEED` — strafe/back input blocked by a wall does not
  climb. Sneak hangs in place (gravity off for the tick); otherwise
  gravity keeps pulling but descent is capped at `CLIMB_SLIDE_SPEED`,
  so a falling player is caught by the ladder. The branch sits between
  flying and swimming — flying and `no_clip` bypass it — and climb
  descent doesn't count toward fall distance.
- **Collision.** `Collision::check_move`
  (`include/common/utils/collision.hpp`) resolves the requested
  `movement_vector` axis-by-axis with sub-steps and step-up
  (`STEP_HEIGHT` — slabs, snow layers), writes back the applied
  displacement, and sets `standing_on_solid`. Sneaking adds the edge
  guard: horizontal moves that would walk off a ledge are clamped.
  `no_clip` skips it entirely. The same move-box core
  (`Collision::move_box`) collides mobs/items against voxels *and*
  `BlockBodies` AABBs (`docs/entities.md`). A grounded tick zeroes
  `y_speed` so a step-up can't launch the player; a blocked axis zeroes
  that velocity component.
- **Feel.** Footsteps accumulate collision-resolved ground distance and
  play the underfoot block's step sound; water entry splashes and
  submerged strokes run per distance swum (both in player.cpp).

## Look & selection

Mouse deltas are low-pass filtered (`MOUSE_SMOOTHING`) into
`angle_H`/`angle_V` (clamped just inside ±90°), then
`build_camera_view` derives the look vector and view matrices.
`RayTraversal::find_block_from_to` (`include/common/utils/ray_traversal.hpp`)
marches the eye ray out to `REACH_DISTANCE`: the hit cell becomes
`pos_destroy`, the adjacent cell along the hit face `pos_create`. A
second traversal ignoring water fills `pos_create_water` so placement
against the water surface works. A third mode-2 traversal runs while a
bucket is held: it hits fluid cells and fills `pos_fluid` /
`pos_fluid_create` / `fluidSelectionInBounds` (`docs/fluids.md`).
`World::set_selected_block` feeds the
`MouseSelection` highlight box, and `mobs.resolve_target` lets a closer
mob pre-empt the block target for left clicks.

- **Mining.** Survival: holding left click accumulates
  `block_mining.progress_ticks` by `item_mining_speed(held, block)` per
  tick until `def->hardness * UPS_RATE` (`hardness < 0` is unbreakable);
  a matching tool class (pickaxe on `MINEABLE_PICKAXE` blocks, axe on
  `MINEABLE_AXE`, shovel on `MINEABLE_SHOVEL`, shears on `MINEABLE_SHEARS`)
  multiplies the rate — anything else mines at 1. Retargeting resets
  progress. Creative breaks instantly. `break_selected_block` plays the
  dig sound, rolls drops (`block_roll_drops` — survival only, spawned as
  item entities), and loots chest contents.
- **Placement.** `change_block` rejects positions that would intersect
  the player AABB (`check_collides_with_block`, including the cell a
  door's upper half occupies) and runs `player_placement_valid` /
  `can_be_placed_in` / `block_can_survive_at` rules; the write itself is
  a `PlayerBlockPlacedEvent` on the block-update queue
  (`docs/redstone.md`), which is also where debris spawns. Rotation is
  chosen by yaw (`rotate_on_placement`), face for torches, pitch for
  pistons (`getPlacedRotation`); wall-mounted blocks — anything whose
  `needs_place_on_any_solid` requires a side face, like ladders and
  buttons — instead anchor to the face the ray actually hit
  (`block_mount_rotation` on `pos_create - pos_destroy`), with
  top/bottom clicks falling back to yaw. Stairs and half slabs also
  take their top-half bit from the clicked face/hit height (bottom
  face or upper side half => top-half block, `docs/blocks.md`).
- **Use.** The right-click chain handles mob feeding (a breedable mob
  takes precedence over block clicks), buttons, doors/gates, chests,
  crafting tables, dust toggling, buckets (`repgame_use_bucket` —
  `BUCKET` scoops the fluid source under `fluidSelectionInBounds`,
  `WATER_BUCKET`/`LAVA_BUCKET` pour a source at the flowing hit cell,
  `pos_fluid_create` or `pos_create`), bone meal (sapling or crop
  stage), hoe tilling and spawn eggs before falling through to placement
  (`docs/redstone.md`, `docs/ui.md`).

## Farming

`player.cpp`'s right-click chain gives hoes, seeds and bone meal their
farm behaviours ahead of block placement (sneak-click skips them like
the other interactions):

- **Tilling** — a held hoe (`item_is_hoe`, the `*_HOE` tool class)
  converts the clicked soil: grass/snowy grass/dirt/grass path →
  `FARMLAND`, podzol/mycelium/coarse dirt → `DIRT`, but only when the
  cell above is open (`can_be_placed_in`). `repgame_convert_block`
  (PlayerBlockPlacedEvent.cpp) does the write + `BLOCK_UPDATE` + neighbor
  updates for every soil conversion (tilling, trampling, growth).
- **Planting** — items with a `"plants"` JSON key (seeds/produce —
  `item_plant_block`) go through `change_block` like a placed block:
  the adjacent cell must be open and `block_can_survive_at` requires
  `FARMLAND` below an `is_crop` block (see `docs/blocks.md`).
- **Trampling** — landing on farmland after >0.5 blocks of real fall
  (accumulated like the mob sim's `fall_dist`; flying, no-clip and
  swimming don't count) converts the field to `DIRT` and the crop pops
  through the neighbor kill path. Mobs trample the same way via
  `MobTickOutput::tramples` (`docs/mobs.md`).

## Game modes

`input.toggle_game_mode` flips `state.game_mode`
(`GameMode_Creative`/`GameMode_Survival`, persisted in `player.dat`).
Creative: instant break, unlimited placement, the creative palette
inventory. Survival: timed mining, item consumption, drops, chest
loot, mob `HIT_DAMAGE` melee with knockback (`player_hurt` — there is
no player health yet; damage is only forwarded). Whether hostile mobs
target the player is a separate `state.peaceful` toggle (pause menu
"Mobs" button, `peaceful` key in `player.dat`, `RepGame::player_targetable`
= `!peaceful && !no_clip`); `NET_PLAYER_FLAG_TARGETABLE` carries it to
remote clients (`docs/multiplayer.md`).

## Scripted/debug paths

`RepGame::tick` can override input each tick (`apply_harness_input_overrides`
in `game_harness.cpp`): `REPGAME_TELEPORT` (position/angle/spin — parsed
once into `RunPolicy::teleport`), `REPGAME_FLY` and walktest (`RunPolicy`,
`src/common/harness/`) drive deterministic stress runs — see `docs/env-vars.md`
and the `capture` skill. `WT` log lines from walktest print position,
velocity and standing state per tick.
