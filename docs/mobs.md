# Mobs

Client-local mobs — passive pig, cow, chicken, sheep, bee and cat plus
hostile zombie and creeper — and network-driven remote players (`MOB_PLAYER`),
driven by a type table and models imported from Bedrock
`geo.json`. Simulation (gravity, collision,
auto-jump, buoyancy, idle/wander/panic AI, hostile chase/fuse AI, hit
flash, death tip-over) runs
in `MobManager::tick` (`src/common/mobs.cpp`); rendering rebuilds one
instanced `RenderChain<ParticleVertex, MobPartInstance>` per frame from
CPU-evaluated bone transforms (`mob_vertex.glsl` + `object_fragment`;
negative `v_flash` is a red hurt tint, positive is the creeper's white
fuse flash). Mobs are drawn in both the main
and reflection passes and share the chunk light sampling. The auto-jump
hops a blocked mob over a wall only when the wall's real collision top
(`Collision::block_collision_top`) is within a block of its feet —
fences and closed gates occupy one cell but collide 1.5 high, so a mob
pressing against one waits instead of hopping forever.

Wandering is a pick-align-commit cycle: an idle mob (`ai_state == 0`,
2–6 s) picks a ground column within ±8 blocks, steers toward it at up to
4°/tick only until aligned (|Δyaw| ≤ 2°), then *locks* the heading
(`leg_locked`) and walks it straight — it never re-aims mid-leg. A leg
ends on arrival, overshoot (the mob has walked past the pick), or a
distance-scaled timeout (~1.5× the walk plus 1 s, capped at 20 s), then
the mob idles and re-picks. Constant-velocity legs are deliberate: they
read less twitchy and, in multiplayer, dead-reckon perfectly — a locked
walker emits no `ENTITY_STATE` at all. A leg pinned on collision
(`blocked_ticks` > ~0.5 s of pushing with <30% progress) latches a
stand-still for the rest of the leg — speed 0, frozen heading — instead
of grinding the wall and streaming dead-reckon corrections for a mob
going nowhere (the counter only clears when the leg ends; see
`netsync_stale` in `docs/entities.md` for why silent walkers still
check in every couple of blocks).

Hostiles target the player only while targetable
(`MobTickInput.player_targetable` = not peaceful + not no-clip; in-game
that means `RepGame::player_targetable` — `state.peaceful` is the pause
menu "Mobs" toggle, persisted in `player.dat`. `MobTickInput`/`MobTickOutput`
carry `player_hits` and `explosions` per tick). They acquire within 16
blocks, drop past 24, and chase at `chase_speed` (10°/tick max turn);
untargeted hostiles wander like passives but don't panic when hit.
Zombies melee in reach (mob half-width + 0.3 + 0.5, |dy| < 1.5) with a
1 s cooldown → a `MobPlayerHit`, handled by `RepGame::player_hurt`
(knockback away from the hit + a hop off the ground + the `player_hurt`
sound pool, then `damage` half-hearts through the `player_damage` funnel
— docs/player.md). In daylight (`light_daylight_factor > 0.5`, head sky
light ≥ 0.95, not in water — see `mob_burns_in_daylight`) zombies burn at
1 hp/s with hurt flash/sound and no knockback. Creepers chase like
zombies, stop within 3 blocks, hiss for ~1.5 s (the `fuse` sfx +
`MOB_ANIM_SWELL` growing/white-flash render), and defuse if the player
gets > 7 away; at fuse end they push their position to
`out->explosions` and are removed — `RepGame::tick` turns each entry
into a `TntExplodeEvent` with a null `primed_entity` (null is a safe
no-op in `World::despawn_primed_tnt`/`PrimedTnt::despawn`). A
`fall_cap` per type limits downward speed (chickens and bees glide at 0.02).
`hover_h` > 0 swaps gravity for a spring easing the feet to `hover_h`
above the highest ground found in the mob's own column and a look-ahead
column scanned from above the head — a ledge or tree canopy reads as
raised ground so a flyer climbs over obstacles instead of auto-jumping.
The look-ahead (`hover_ground_ahead_at`) also checks the opening under the
solid run it finds: a gap tall enough for `hover_h` + the hitbox (a
2-high doorway) reports the floor below, so the bee flies *through*
doors instead of over the wall.
The water surface counts as ground (skim, don't dunk); with no ground in
range the mob falls normally. A dying hoverer drops while tipping.
`standing` stays false in flight, so `MOB_ANIM_WING_FLAP` holds the fast
airborne rate. The bee hovers at `hover_h = 1`.

Breeding (pigs, cows, sheep, chickens, bees, cats — `MobTypeDef::breed_items`
lists
each type's foods: pigs eat carrots/potatoes/beetroots, cows and sheep
wheat, chickens wheat or beetroot seeds, cats raw `COD`/`SALMON`; the bee
instead sets
`breeds_with_flowers`, which counts every `is_flower` block — members of
the `"flowers"` item tag in `data/item_tags.json` — as food, keeping a
single flower in `breed_items[0]` only as the `feed` debug command's
default): a passive mob within
`MOB_TEMPT_RANGE` (10 blocks, ±4 y) of a player holding its food drops the
wander AI and follows (`MobTickInput::held_item`; remote held items are
synced for display only, so only the local player tempts). Tempting ignores a
no-clipping player but not the hostiles toggle — peaceful worlds still
tempt. Right-clicking a breedable
mob while holding a valid food calls `MobManager::feed` — an adult
enters `love_ticks` (no expiry: love lasts until the mob mates), a baby
sheds ~10% of its
remaining growth. Feeding marks the mob `touched` so it persists like
penned livestock. Love only changes the AI when a partner exists: seeking
the nearest same-type mob also in love outranks tempting and wandering
(panic still wins), but a mob in love with no partner keeps following food
and wandering as usual. When both are in love they path to each other and
the pair check at the end of `tick` spawns a baby at their midpoint
(`age_ticks = -MOB_BABY_AGE_TICKS`, ~5 min at `MOB_BABY_SCALE` = 0.5 for
collision/raycast/eye/render scale) and starts both parents'
`breed_cooldown` (~2 min, blocks re-feeding). Babies count up to adult
and drop nothing when killed. Grow-up doubles the hitbox in one tick —
a baby pressed against a wall/fence would overlap the block and tunnel
through it (check_move only clamps face crossings), so reaching adult
flags `needs_unstick` and `tick_mob` pushes the mob back to free space
via `Collision::unstick` (also set by a growth-finishing `feed`). Right-click feeding a mirrored mob goes
through `ENTITY_ACTION`/`NET_ACTION_FEED` — the owner validates the item
against `breed_items` before applying (see `docs/entities.md`); baby
state rides `NET_MOB_FLAG_BABY` + `NetEntityRecord.age_ticks`, and
`MobFileRecord::grow_ticks` persists it (`docs/persistence.md`).
Breeding feedback is red-heart particles (`HeartParticles`): feeding
opens a `heart_ticks` window (~5 s, one heart per ~0.5 s; on mirrors the
`NET_MOB_FLAG_LOVE` rising edge opens the same window), a burst
over a newborn, and a smaller puff when a baby eats. The window is
bounded because love itself never expires — keying emission to
`love_ticks` would accumulate particles without bound. Emission runs
inside `MobManager::update_render` so hearts interpolate with the
render frame; capture mode skips it to stay deterministic. Mobs
also trample: landing on `FARMLAND` after >0.5 blocks of real fall pushes
the cell to `MobTickOutput::tramples` and `RepGame::tick` converts it to
dirt — same model as the player trample (`docs/player.md`).

Shearing (sheep — `MobTypeDef::shear_drop` is the per-type opt-in):
right-clicking an unshorn adult with `SHEARS` calls `MobManager::shear`,
which sets `sheared`, marks the mob `touched`, plays `sheep_shear` at the
feed distance falloff, and the caller spawns `shear_drop` (1–3
`WHITE_WOOL`) where the mob stood via `RepGame::spawn_mob_shear_drop`.
Shears are a stack-1 tool and aren't consumed. While `sheared` the mob
renders `MobTypeDef::sheared_model` (the sheared body under the wool
layer — `MOB_MODEL_SHEEP_SHEARED`, imported from Bedrock's
`geometry.sheep.sheared`); `spawn_any` allocates part instances for the
larger of the two cube sets so the swap never reallocates, and
`update_render` zero-fills the spares. The state rides
`NET_MOB_FLAG_SHEARED` through the wire — its rising edge plays the snip
on mirrors — and `MobFileRecord::flags` on disk; right-click shearing a
mirrored mob goes through `ENTITY_ACTION`/`NET_ACTION_SHEAR` like a feed
(the owner validates the claimed `item` against `SHEARS`).

Wool regrows by grazing: an idle sheep standing on `GRASS` starts a ~2 s
`graze_ticks` graze (eager ~1/(2.5 s·UPS) while shorn, ambient
~1/(50 s·UPS) woolly — `MobManager::tick_mob`) that pins the AI and dips
the head instead of the player look-at. Finishing over still-`GRASS`
ground pushes the cell to `MobTickOutput::grazes`, clears `sheared`, and
sheds ~10% of a baby's remaining growth; `RepGame::tick` converts the
cell to `DIRT` (no drop, no debris, same `repgame_convert_block`
hand-off as tramples). Panic or losing the ground aborts a started graze.

The night spawner runs inside `MobManager::tick` (off under
`REPGAME_MOB_TEST`, capture mode, fly mode and `REPGAME_MOB_SPAWNER=0`):
every 2 s, while fewer than 12 despawnable hostiles live, up to 4
attempts pick a random ring 24–44 blocks out, scan down from
player_y + 16 (40 cells) for a colliding top block with 2 air cells
above inside a `CHUNK_LIVE` chunk, and spawn where
`hostile_spawn_light_ok` (`max(sky * daylight, block) < 7/15`) holds —
60% zombie / 40% creeper, `persistent=0` + `despawnable=1`.
Despawnables are removed > 64 blocks out, randomly at
1/(8·UPS_RATE)/tick past 40, or when their feet chunk leaves
`CHUNK_LIVE`, and are never persisted.

`MAX_MOBS` (256) caps only emergent population sources — the night
spawner and fresh natural-roll products call `spawn` with the default
`capped=true` and check `under_mob_cap()` (owned non-mirror, non-avatar
count). Explicit spawns pass `capped=false` and always go through:
spawn eggs, the `n` key, `dbgcmd::mob`, `REPGAME_MOB_TEST` fixtures and
persisted-mob restores. It is not a storage or render limit:
`spawn_any` never refuses, so avatars, mirrored mobs and owned transfers
always fit, and the live count can exceed the cap.

- `REPGAME_MOBS=0` — disables local mobs (spawning, simulation,
  persistence) but NOT remote players: `spawn_remote` and remote
  rendering still work.
- `REPGAME_MOB_SPAWNER=0` — disables only the night spawner.
- `REPGAME_MOB_TEST="type,x,y,z,yaw[,pitch[,sneak[,item]]][;...]"` — exact
  deterministic spawns (non-persistent; also disables natural spawn and
  mobs-file loading for the run). `player` entries spawn a remote
  `MOB_PLAYER` at feet position through `set_remote_pose` (the optional
  pitch is the look pitch; the optional sneak is 0..1 and drops the eye
  height `sneak * PLAYER_SNEAK_EYE_DROP`). The optional trailing `item`
  is an item/block JSON name resolved by `item_id_from_name` — the remote
  player is drawn holding it (`set_remote_held_item`); unknown names log
  a warning and hold nothing.
- Capture mode (`REPGAME_CAPTURE`) freezes mob AI and physics — spawn
  poses are as authored, no falls or wandering mid-capture.
- `n` — debug spawn key: drops a mob three blocks in front of the
  player, cycling pig → cow → chicken → sheep → zombie → creeper → bee →
  cat on successive presses (`MobManager::spawn_in_front`; edge-triggered
  through `Input`, handled in `RepGame::tick`, persistent, uncapped).
  The ImGui overlay shows `Mobs: <n> (players <m>)` — remote players
  count separately.
- Spawn eggs — `data/items/<type>_spawn_egg.json` (one per non-remote
  type; `MobTypeDef::spawn_egg` binds item → type and
  `mob_type_for_spawn_egg` resolves it). Right-clicking a block face
  hatches the mob in the cell a placed block would go, facing the player
  (persistent, uncapped; survival consumes one — `player.cpp`, same
  pattern as bone meal). Block interactions still win: clicking a chest
  opens it unless sneaking.

Remote players: `MultiplayerAvatars` is a thin adapter over `MobManager`
(`player_id → uid` map) — `add`/`update_position`/`remove` translate to
`spawn_remote`/`set_remote_pose`/`remove_remote` on a `MOB_PLAYER` mob.
The packet position is the sender's eye position plus its eye height
(feet = eye − eye_height; eye height drops to `PLAYER_SNEAK_EYE_HEIGHT`
while sneaking, driving the `MOB_ANIM_HUMANOID_SNEAK` crouch pose).
The camera rotation matrix is inverted by
`mob_look_from_camera_rotation` (`mobs.hpp`). `MOB_AI_REMOTE` mobs skip
physics/AI, ease toward the latest network pose, walk-animate from the
horizontal delta (`MOB_ANIM_HUMANOID_WALK`), never persist, never
raycast, and keep ticking in capture mode.

`update_position` also carries the sender's selected hotbar item
(`Mob::held_item`); `set_remote_held_item` stores it and `update_render`
rebuilds `mob.held_parts` on change — a sprite instance via
`render_held_sprite` for flat items/non-isometric block icons, or one
`render_held_cube` `BlockObjectInstance` per `block_icon_box_count` box
for isometric icons (same `resolve_item_faces`/`block_item_shape` rules
as dropped items). The instances ride the `bone_arms[1]` bone world in
`update_render` — the arm cube at +X is the character's right hand, the
bone names being authored mirrored — so walk swing, sneak and the death
tip-over all carry the item. `MobManager::draw_held` draws both chains
after the parts pass (`world.cpp` calls it in the normal and reflection
passes). An invalid/`LAST_BLOCK_ID` item holds nothing.

Adding a mob:

1. Add a line to `tools/mobs_manifest.txt`:
   `name|geo_file|geometry_id|skin1,skin2,...[|bone=rx,ry,rz;...]` —
   `geo_file`/`skin` paths are under `BEDROCK_SAMPLES`
   (`bedrock-samples/resource_pack` in the repo by default — see
   `docs/blocks.md`), the
   trailing field overrides rest-pose bone rotations that Bedrock keeps in
   animation files (the pig's `body=-90,0,0` comes from
   `animation.pig.setup.v1.0`; with `+90` the body cube swings up and back
   instead of lying flat — keep the `-90` sign). A `bone.cubes=rx,ry,rz`
   entry instead stores the rotation on the bone's own cubes only
   (`MobCubeDef::rot`, applied about the bone pivot inside the bone
   transform — the children stay at their authored positions and each
   face keeps its painted texels; remapping origin/size to an AABB
   instead would scramble the box-uv face layout) — for models stored
   Java-style with a pre-rotated body cube under a transform hierarchy
   (the cat's `body.cubes=-90,0,0`). Plain `bone=` entries in the same
   field cover the rest-pose baseline a Bedrock animation holds (the
   cat's `tail1=-51.57` slope comes from `animation.cat.walk` — `sneak`'s
   zeroed tail hangs straight down the rump, coplanar with the body's
   rear face). A `bone.move=dx,dy,dz` entry translates the bone's
   authored cubes and pivot and reparents it to the root — for
   Java-ported geometry whose bind layout relies on runtime pivot glue
   the geo can't express (the cat's `tail2` is authored detached behind
   `tail1`, and its 8-px column reads as a second tail at any curl — so
   `tail2.move` parks its pivot inside the belly and `tail2=-90,0,0`
   lays the column flat fully inside the body volume, hidden) or to
   tuck a part inside the body (the cat's `frontLeg*.move` hides the
   authored 0.2 shoulder nub — the x component also keeps the leg's
   outer face off the body's side plane so they don't z-fight).
2. `make mobs-import` runs `tools/mob_import.py` →
   `include/common/mob_models_gen.hpp` + `bitmaps/mobs.png`. Output is
   deterministic (byte-identical reruns; the gen header is only rewritten
   when its content changes); the importer handles geo.json
   1.8 and 1.12, box UVs only (per-face UVs are rejected), bone
   mirror/pivot/rotation, and pads each skin into one 64x64 layer of the
   atlas. `"geometry.a:geometry.b"` inheritance merges bones by name:
   a same-named child bone's cubes layer on top of the parent's (that's
   how the sheep's wool sits over the sheared body — `reset: true` on a
   bone drops the inherited content instead).
3. Add a `MobTypeDef` row in `src/common/mob_types.cpp` (+ `MOB_*` enum in
   `mob_types.hpp`): model/skin bindings, hitbox, health,
   walk/panic/chase speeds, `fall_cap`, `hover_h`, `attack_damage`, drops, sound
   pool names, natural-spawn data, AI profile (`MOB_AI_PASSIVE`,
   `MOB_AI_HOSTILE_MELEE`, `MOB_AI_HOSTILE_CREEPER`, `MOB_AI_REMOTE`),
   animation mask (`MOB_ANIM_*` — quadruped/humanoid walk, head look,
   death tip, sneak, wing flap, creeper swell) and the head/leg/arm/
   body/wing bone names the animators bind to (NULL = unused).
   `gait_scale` multiplies the quadruped leg swing (0 = the full 80°;
   the cat uses 0.3 — its 10-px legs at full swing read as extra tails).
4. Optional sounds: append `sfx` entries to `tools/sounds_manifest.txt`
   (base + `_N` variants pool, e.g. `pig_say_1..3`; `pig_death`,
   `pig_step_1..5`).

Skin atlas convention: each skin is one 64x64 layer (`MOB_SKIN_*` is the
layer index into `mobs.png`, `MK_TEXTURE(mobs, 64, 64*MOB_SKIN_COUNT,
64, 64)`, NEAREST + no mipmaps via `Texture::init(..., mipmaps=false)`).
The loader maps skin pixel `(px, py)` to UV `(1 - px/64, 1 - py/64)` —
vertical flip from the texture load plus a per-tile X mirror; mirrored
cubes also swap left/right face rects (`mob_face_rect` in
`include/common/mob_uv.hpp`, also unit-tested in `test_mobs`). Skin alpha
is normalized on import: Bedrock renders entity skins opaque, so nonzero
alpha (e.g. sheep.tga's a=3 undercoat/face texels) is promoted to 255 —
only true a=0 texels stay cutout.

`MobManager::draw` disables `GL_CULL_FACE` for the instanced cube pass
(Java renders entities `entityCutoutNoCull`; Bedrock's `chicken_legs`
material is no-cull too): some skins leave cube faces untextured — the
chicken leg only paints its back + bottom rects — so the far side's
interior must show through the near face's alpha cutout. The cull-face
mode is left alone so the reflection pass's `GL_FRONT` survives. A
`GL_POLYGON_OFFSET_FILL` bias keeps feet — coplanar with the block top
face — from z-fighting the ground.

Persistence: each chunk's mobs live in `mobs_x_y_z` beside the
`chunk_x_y_z`/`chest_x_y_z` files — `u32 record_size`, `u32 count`,
packed `MobFileRecord`s (each carrying a `net_id`), then a tombstone
section `u32 removed_count, u8 removed[]` listing natural-roll indices
that must never respawn (readers copy the shared prefix and skip
`type >= MOB_TYPE_COUNT`; file formats: `docs/persistence.md`).

Natural-spawn products are *provenance-tracked* rather than saved
wholesale. The terrain worker's roll (`spawn_chance` ≈1/12 per type,
seeded `hash(chunk_pos, type, MAP_SEED)`) is deterministic, so a
*pristine* natural mob (`NET_MOB_FLAG_NATURAL` && !`NET_MOB_FLAG_TOUCHED`)
needs no record — it is recreated by re-rolling on every load, and a
chunk holding only pristine mobs leaves no `mobs_` file behind
(`persist_mobs` deletes the file when records and tombstones are both
empty). Each rolled record carries a `net_natural_mob_id` (home chunk +
roll index, `include/common/net/packet.hpp`). A mob becomes **touched**
(sticky `Mob::touched`, `MobManager::mark_touched`) on a player hit or by
the *pen rule* — a natural mob ticking inside a chunk with
`Chunk::sidecars.player_edited` (set by real player edits, never random
ticks or mob-driven writes like grazing/trampling)
counts as kept livestock. Touching tombstones the mob's roll index in
its *home* chunk's removed set; the mob itself then persists as an
ordinary record under its natural id, and the re-roll skips tombstoned
indices so it can never duplicate.

The worker loads records + tombstones and *always* re-rolls into
`Chunk::sidecars.mobs` (skipping tombstoned/already-loaded indices);
`World::render` hands just-finalized chunks to
`MobManager::adopt_chunk`, and a ~16-tick poll
persists+removes mobs whose owning chunk leaves the active window
(parked/reused — `CHUNK_LOADING` at the same position has not left).
`World::cleanup` persists every owned chunk. `despawnable` (night-spawn)
mobs are never written. No writes in capture or fly
mode — and no local reads/writes at all in multiplayer, where the server
holds `mobs_`/`items_` sidecars and the same pristine/touched rules (see
`docs/entities.md` "Multiplayer entity sync"). Natural spawn stands herds on `spawn_on`
column tops with hitbox headroom inside the chunk; in multiplayer the
chunk's owning client runs the roll on its CHUNK_ROLE grant and reports
`CHUNK_POPULATED`.

Hitting: left click raycasts mobs (`MobManager::raycast`, AABB slab test)
against `block_selection.ray_origin/ray_dir`; a mob nearer than the
selected block takes `HIT_DAMAGE` (4.0, creative kills in one hit), gets
knockback + a 0.3 s red flash + 5 s panic, and rolls `MobTypeDef::drops`
in survival when killed (zombies: rotten flesh 0-2 plus bones 0-2 — the
bone feeds the bone-meal recipe). Babies (`is_baby_at`) drop nothing.
Bow arrows reach the same `MobManager::raycast` during `arrows.tick` —
`ArrowTickOutput::mob_hits` are applied in `RepGame::tick_entities`
through the identical hurt path (survival `ARROW_DAMAGE` 9.0, `hurt()`
knockback/flash/panic, drops on kill) or forwarded as
ENTITY_ACTION/NET_ACTION_HIT for remote-owned mobs — see
`docs/entities.md` "Arrows".
