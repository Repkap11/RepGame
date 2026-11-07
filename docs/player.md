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
| `Space` | Jump; fly up; swim up |
| `'` (apostrophe) | Sneak; fly down; sink while swimming |
| `P` (hold) | Sprint |
| `F` | Toggle flying |
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
  the liquid's surface plane (`WATER_HEIGHT`) — a crouched dip doesn't
  flip into swim and oscillate. Feet-in-liquid but body above the
  surface is wading (slower walk, normal physics).
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
against the water surface works. `World::set_selected_block` feeds the
`MouseSelection` highlight box, and `mobs.resolve_target` lets a closer
mob pre-empt the block target for left clicks.

- **Mining.** Survival: holding left click accumulates
  `block_mining.progress_ticks` against `def->hardness * UPS_RATE`
  (`hardness < 0` is unbreakable); retargeting resets progress.
  Creative breaks instantly. `break_selected_block` plays the dig sound,
  rolls drops (`block_roll_drops` — survival only, spawned as item
  entities), and loots chest contents.
- **Placement.** `change_block` rejects positions that would intersect
  the player AABB (`check_collides_with_block`, including the cell a
  door's upper half occupies) and runs `player_placement_valid` /
  `can_be_placed_in` / `block_can_survive_at` rules; the write itself is
  a `PlayerBlockPlacedEvent` on the block-update queue
  (`docs/redstone.md`), which is also where debris spawns. Rotation is
  chosen by yaw (`rotate_on_placement`), face for torches, pitch for
  pistons (`getPlacedRotation`).
- **Use.** The right-click chain handles buttons, doors/gates, chests,
  crafting tables, and dust toggling before falling through to
  placement (`docs/redstone.md`, `docs/ui.md`).

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
