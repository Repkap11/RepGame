# Player: input, movement & interaction

Player state lives in `RepGameState`: `camera` (position, look angles,
velocities, pose heights — `include/common/game/player_state.hpp`),
`input` (`include/common/input.hpp`), `block_selection`,
`block_mining`, `held_swing`, `hotbar`. Per-tick work happens in
`src/common/game/game_tick.cpp` (`RepGame::tick`) and
`src/common/game/player.cpp`; this doc covers the desktop keyboard/mouse
path — VR bindings are `docs/vr.md`, and the touch controls live in the
shared `src/common/touch_controls.cpp` (see *Touch bindings* below). `RepGame::tick` itself is a short ordered
list of named phases: gameplay phases live in `game_tick.cpp`, the
scripted/debug-only phases (capture, fly, walktest, results, script) in
`src/common/game/game_harness.cpp`, and the VR phases in `vr.cpp`.

## Key bindings (Linux/SDL, `src/linux/input.cpp`)

| Input | Action |
|---|---|
| `W`/`A`/`S`/`D`, arrows (also `o`/`k`/`;`/`l`) | Move — folded into `input.movement` (`angleH`: W=0, D=+90, S=180, A=-90, diagonals ±45/±135; `sizeH` analog magnitude) |
| `Space` | Jump; fly up; swim up; climb up |
| `'` (apostrophe) | Sneak; fly down; sink while swimming; hang on climbables; dismount a ridden minecart |
| `P` (hold) | Sprint |
| `F` | Toggle flying; with the creative inventory open, opens its item-name search box instead (typing filters the palette, `Enter`/`Esc` closes it) |
| `C` | Toggle no-clip (`Ctrl+C` exits) |
| `I`/`E` | Toggle inventory |
| `T`/`Tab` | Open the chat box (`docs/chat.md`) |
| `/` | Open the chat box pre-filled with a command slash |
| `Esc` | Close chat/inventory, else pause menu |
| `` ` `` (grave) | Toggle creative/survival |
| `R` / `Shift+R` | Cycle `worldDrawQuality` up/down |
| `=`/`-` (numpad ±), `0` | Render distance +/-/reset to `RENDER_RADIUS_DEFAULT` |
| `PgUp`/`PgDn` (`+Shift`) | World time ±1h (±6h with shift) — sends `WORLD_TIME` in MP |
| `Q` / `Ctrl+Q` | Throw one item / the whole stack |
| `M` | Debug block render mode |
| `G` | ImGui debug overlay |
| `V` | Toggle vsync |
| `N` | Spawn a mob (debug) |
| `F12` | Screenshot (`./screenshots/screenshot_<n>_*.tga`, first unused `n` — earlier sessions' shots are kept) |
| Left click | Mine / attack mob |
| Right click | Place / interact; `Shift`+right-click places past interactables |
| Middle click | Pick block (survival: pulls to hotbar; creative: gives) |
| Wheel | Hotbar slot; creative-inventory page while inventory is open |

While `menu_open`, key *presses* are swallowed except Esc/inventory
(resume), F12, Ctrl+C; releases still flow so held-key flags stay
consistent. Mouse presses record where they began
(`*_pressed_in_ui`) so a drag started over the UI never mines and a drag
started in the world never moves items.

## Touch bindings (`TouchControls`, `src/common/touch_controls.cpp`)

The shared touch controls — the C++ port of Android's `UIOverlayView`,
active while `input.touch_ui` (Android at init, WASM from JS, the pause
menu's "Touch Controls" button elsewhere). Geometry is the layout pass's
`UiLayoutRects::touch_*` (docs/ui.md); a `r = min(w/4, h/2)/2` unit
sizes every button.

| Touch | Action |
|---|---|
| Joystick (bottom-left corner, hold + drag) | Move — `positionHMove(dist/maxDist, angleH)` with the same convention as WASD (drag up = 0 forward, right = +90); release resets `sizeH` to 0 |
| Jump (top-right, hold) | `movement.jumpPressed` |
| Sneak (top-left, tap) | Toggle `movement.sneakPressed` — a toggle because holding it while aiming is impractical |
| Break / Place (bottom-right corner row, hold) + Pick (top, right of Menu) | `setButtonState` left / middle / right |
| Inv (right edge, directly under Jump, tap) | `onInventoryClicked` |
| Menu (top-center, tap) | `onMenuClicked` (close inventory, else pause menu); opening the menu releases every held control |
| Chat (left edge, directly under Sneak — or right of Sneak on the top row when the lifted joystick would cover it, tap) | `chat_open_request` → `repgame_open_chat` — chat uses the shared `text_input_active` capture; on Android the box opens but there's no IME bridge yet |
| System back (Android) / browser back (WASM) | `Input::backPressed()`: text field → `text_escape`, focused creative search → `search_close`, else `onMenuClicked` |
| Drag anywhere else | Look — `lookMove(dx, dy)` at `TOUCH_LOOK_SENSITIVITY` (1.25×) per move |
| Short tap anywhere else | Tap — `screen_tap_pending` at `absPosition`: hotbar slot select with the inventory closed, a UI click when it's open (slop = `TOUCH_TAP_SLOP_FRACTION` × short screen edge) |
| Inventory open: Close / Creative\|Survival / `<` `>` | Close the inventory / arm `toggle_game_mode` / page the creative palette (wheel counter, creative only); other touches tap-track like the menu case |
| `menu_open` or `text_input_active` | Every touch is a tap: `absPosition` tracks the finger, `screen_tap_pending` on a within-slop release |

Multi-touch tracks one pointer id per control (a second finger on a held
control is ignored); `TOUCH_CANCEL` releases without a tap. On narrow
screens where a bottom-corner control would cover the hotbar, it lifts
so its bottom sits on the HUD strip's top edge (Pick stays aligned with
the joystick).

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
- **Riding a minecart.** Right-click a cart to mount
  (`camera.riding_vehicle`); sneak dismounts. All body physics above is
  skipped while riding — the seat comes from
  `repgame_vehicle_ride_follow` (`src/common/game/vehicle_ride.cpp`),
  W/S intent pushes the cart along the rail, `fall_dist` stays 0
  (no fall damage), and the player's box doesn't shove carts. See
  `docs/entities.md` Vehicles for mount/dismount/targeting details.
- **Riding a boat.** Same mount/dismount, but the controls are
  Minecraft-style: W/S paddle the boat forward/backward along its own
  hull yaw and A/D steer (≈180°/s) — the camera stays free-look and
  turns with the hull's yaw. A boat on land barely crawls.
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
  play the underfoot block's step sound; the water-entry splash scales
  its gain by entry speed (walking in is a lap, a real fall is the heavy
  splash) and only fires after ~0.5 s out of liquid, so fluid rewrites
  reshaping the cell underfoot don't re-splash; submerged strokes run
  per distance swum (all in player.cpp).

## Look & selection

Mouse deltas are low-pass filtered (`MOUSE_SMOOTHING`) into
`angle_H`/`angle_V` (clamped just inside ±90°), then
`build_camera_view` derives the look vector and view matrices.
`RayTraversal::find_block_from_to` (`include/common/utils/ray_traversal.hpp`)
marches the eye ray out to `REACH_DISTANCE`: the hit cell becomes
`pos_destroy`, the adjacent cell along the hit face `pos_create`. A
second traversal ignoring water fills `pos_create_water` so placement
against the water surface works. A third traversal runs while a
bucket is held — mode 3 (sources only) for an empty bucket, mode 2
(any fluid cell) for a filled one — and fills `pos_fluid` /
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
  item entities), and loots chest contents. Mining a piston head
  retargets the removal at its base (`piston_head_base`) so the whole
  piston drops; the head cell then pops through the survive check.
  While either mouse button is held on the world (mining, mob hits,
  placement, or air), `held_swing` (`tick_input_latches` →
  `HeldSwing::tick`) runs a looping 0..1 chop phase that
  `game_render.cpp` interpolates and applies as an extra camera-space
  rotation on the held item's pose; releasing mid-swing finishes the
  chop out to the rest pose.
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
  crafting tables, cake eating (`is_cake` — each click consumes a 2px
  slice, the last one removes the block; `docs/blocks.md`), sign text
  editing (`block_is_sign` — opens the four-line sign editor, and
  sneak-click places past it like other interactables — `docs/ui.md`),
  beds (`is_bed` — sets the respawn point and sleeps at night; see
  "Sleeping" below), dust
  toggling, buckets (`repgame_use_bucket` —
  `BUCKET` scoops the fluid source under `fluidSelectionInBounds`,
  `WATER_BUCKET`/`LAVA_BUCKET` pour a source at the flowing hit cell,
  `pos_fluid_create` or `pos_create`), bone meal (sapling or crop
  stage), hoe tilling, spawn eggs and the bow before falling through to
  placement (`docs/redstone.md`, `docs/ui.md`). A held `BOW` fires an
  arrow along the camera look vector at the standard 30-tick click
  cadence (`repgame_fire_arrow` — no charge-up; release is biased a
  step along the aim so it can't collide with the shooter's own head
  cell). Survival consumes one `ARROW` — hotbar first, then the
  survival inventory (`consumeItem` helpers on both) — and refuses to
  fire empty; creative fires free and one-shots (1000 damage vs the
  survival `ARROW_DAMAGE`). Block interactions keep precedence — a
  right-click aimed at a chest/door still opens/toggles it — and VR's
  use-binding lands here the same way (`vr.haptic_pending`).

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

## Health & food

`PlayerVitals` (`include/common/game/player_state.hpp`, `state.vitals`)
holds the local player's health in half-hearts (0..
`PLAYER_MAX_HEALTH` = 20, drawn as ten heart slots on the HUD), the
`air_ticks` breath meter, the shared `hurt_cooldown` i-frame window, the
peaceful `regen_accum`, the `dead` flag and the `fall_dist` tracker.

Every damage source funnels through `RepGame::player_damage` — survival
only (creative is invulnerable), refused while dead or inside
`PLAYER_HURT_COOLDOWN_TICKS`, which also paces the periodic sources to
~2 hits/s. Sources: mob melee via `player_hurt` (knockback + hurt sfx,
then the funnel), fall damage (the movement loop's `fall_dist`:
`floor(dist) - PLAYER_FALL_SAFE_DISTANCE` half-hearts on landing, same
accumulator as farmland trampling), lava at the eye/body/feet
(`PLAYER_LAVA_DAMAGE`), standing in `FIRE`/`SOUL_FIRE`
(`PLAYER_FIRE_DAMAGE`), explosions (`repgame_explosion_affect_player`,
linear falloff from `PLAYER_EXPLOSION_MAX_DAMAGE` plus a shove — both
local TNT and remote `EXPLOSION` frames; each client damages only
itself), and drowning once `air_ticks` runs out under water
(`PLAYER_DROWN_DAMAGE`). `repgame_tick_vitals` runs this upkeep each
tick after movement; breath drains while the head is under water and
refills at the surface.

Right-click with a food item (`"food"` in `data/items`,
`item_food_heal`) eats it: `repgame_eat_food` heals the item's value
instantly (beta-MC style, no eating animation) and consumes one item in
survival — refused at full health or on non-food. Eating plays `eat`.

While `state.peaceful` is on, survival health regenerates one
half-heart per `PLAYER_PEACEFUL_REGEN_TICKS` (MC's peaceful
difficulty).

At 0 health `player_damage` sets `dead` and raises the death screen:
`PauseMenu` dead mode ("You died!" + only Respawn and Exit — Esc can't
dismiss it; `tick_vitals` re-pins `menu_open` each tick). Inventory is
kept. `repgame_respawn` restores full vitals and re-pins the spawn
column — the recorded bed when `spawn_point_set`, else the (0,0)
world-spawn column (`spawn_pending` does the ground snap once it loads;
`repgame_resolve_pending_spawn` is the free-function body the tests
drive). Health is
persisted in `player.dat` (`docs/persistence.md`); a saved 0 — quitting
on the death screen — loads as a fresh spawn. In multiplayer the local
health rides the player-location payload and a drop flashes the avatar
red on other clients (`docs/multiplayer.md`).

## Sleeping & respawn points

Right-clicking a bed (`is_bed`) runs `repgame_try_sleep` and always
records the clicked cell as `state.spawn_point` (persisted as `player.dat`'s
`spawn` key). The bed block itself is two cells — foot on the clicked
cell, head one block in the placer's facing direction (docs/blocks.md) —
and either half answers the click. From dusk on (`world_time` mod
`DAY_LENGTH_TICKS` ≥ `SLEEP_EARLIEST_TICKS` — when the daylight factor
starts dimming, just before the half-day sunset) it additionally sleeps: `world_time` jumps
just past the next dawn (`SLEEP_WAKE_OFFSET` — the boundary itself is
still sunrise-dim) and goes out as `WORLD_TIME` when connected — one sleeper
skips the night for everyone, the same permissive model as the
PgUp/PgDn clock keys. Sleeping is refused while a live hostile mob is
within `SLEEP_MONSTER_RADIUS` (8 blocks —
`MobManager::hostiles_within`); the spawn point is still set either way.
Sneak-click places against the bed like the other interactables, and
"can't sleep" feedback is log-only.

`resolve_pending_spawn` honors the point on respawn: it keeps the camera
over the bed column until the chunk reports generated data, stands the
player on the bed top (stepping up past a low ceiling, bounded), and —
when the bed is missing or unreachable — clears the point and falls back
to the world-spawn column. Fresh saves and beds never clicked spawn at
(0,0) as before.

## Game modes

`input.toggle_game_mode` flips `state.game_mode`
(`GameMode_Creative`/`GameMode_Survival`, persisted in `player.dat`).
Creative: instant break, unlimited placement, the creative palette
inventory. Survival: timed mining, item consumption, drops, chest
loot, health, mob `HIT_DAMAGE` melee with knockback (`player_hurt` —
see "Health & food"). Whether hostile mobs target the player is a
separate `state.peaceful` toggle (pause menu "Mobs" button, `peaceful`
key in `player.dat`, `RepGame::player_targetable` =
`!peaceful && !no_clip && !dead`); `NET_PLAYER_FLAG_TARGETABLE` carries it to
remote clients (`docs/multiplayer.md`).

## Scripted/debug paths

`RepGame::tick` can override input each tick (`apply_harness_input_overrides`
in `game_harness.cpp`): `REPGAME_TELEPORT` (position/angle/spin — parsed
once into `RunPolicy::teleport`), `REPGAME_FLY` and walktest (`RunPolicy`,
`src/common/harness/`) drive deterministic stress runs — see `docs/env-vars.md`
and the `capture` skill. `WT` log lines from walktest print position,
velocity and standing state per tick.
