# Mobs

Client-local mobs — passive pig, cow, chicken and sheep plus hostile
zombie and creeper — and network-driven remote players (`MOB_PLAYER`),
driven by a type table and models imported from Bedrock
`geo.json`. Simulation (gravity, collision,
auto-jump, buoyancy, idle/wander/panic AI, hostile chase/fuse AI, hit
flash, death tip-over) runs
in `MobManager::tick` (`src/common/mobs.cpp`); rendering rebuilds one
instanced `RenderChain<ParticleVertex, MobPartInstance>` per frame from
CPU-evaluated bone transforms (`mob_vertex.glsl` + `object_fragment`;
negative `v_flash` is a red hurt tint, positive is the creeper's white
fuse flash). Mobs are drawn in both the main
and reflection passes and share the chunk light sampling.

Hostiles target the player only while targetable
(`MobTickInput.player_targetable` = survival + not no-clip; in-game that
means `GameMode_Survival && !no_clip` — `MobTickInput`/`MobTickOutput`
carry `player_hits` and `explosions` per tick). They acquire within 16
blocks, drop past 24, and chase at `chase_speed` (10°/tick max turn);
untargeted hostiles wander like passives but don't panic when hit.
Zombies melee in reach (mob half-width + 0.3 + 0.5, |dy| < 1.5) with a
1 s cooldown → a `MobPlayerHit`, handled by `RepGame::player_hurt`
(knockback away from the hit + a hop off the ground + the `player_hurt`
sound pool; there is no player health system yet, so `damage` is
otherwise unused). In daylight (`light_daylight_factor > 0.5`, head sky
light ≥ 0.95, not in water — see `mob_burns_in_daylight`) zombies burn at
1 hp/s with hurt flash/sound and no knockback. Creepers chase like
zombies, stop within 3 blocks, hiss for ~1.5 s (the `fuse` sfx +
`MOB_ANIM_SWELL` growing/white-flash render), and defuse if the player
gets > 7 away; at fuse end they push their position to
`out->explosions` and are removed — `RepGame::tick` turns each entry
into a `TntExplodeEvent` with a null `primed_entity` (null is a safe
no-op in `World::despawn_primed_tnt`/`PrimedTnt::despawn`). A
`fall_cap` per type limits downward speed (chickens glide at 0.02).

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

- `REPGAME_MOBS=0` — disables local mobs (spawning, simulation,
  persistence) but NOT remote players: `spawn_remote` and remote
  rendering still work.
- `REPGAME_MOB_SPAWNER=0` — disables only the night spawner.
- `REPGAME_MOB_TEST="type,x,y,z,yaw[,pitch[,sneak]][;...]"` — exact
  deterministic spawns (non-persistent; also disables natural spawn and
  mobs-file loading for the run). `player` entries spawn a remote
  `MOB_PLAYER` at feet position through `set_remote_pose` (the optional
  pitch is the look pitch; the optional sneak is 0..1 and drops the eye
  height `sneak * PLAYER_SNEAK_EYE_DROP`).
- Capture mode (`REPGAME_CAPTURE`) freezes mob AI and physics — spawn
  poses are as authored, no falls or wandering mid-capture.
- `n` — debug spawn key: drops a mob three blocks in front of the
  player, cycling pig → cow → chicken → sheep → zombie → creeper on
  successive presses (`MobManager::spawn_in_front`; edge-triggered
  through `Input`, handled in `RepGame::tick`, persistent). The ImGui
  overlay shows `Mobs: <n> (players <m>)` — remote players count
  separately.

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

Adding a mob:

1. Add a line to `tools/mobs_manifest.txt`:
   `name|geo_file|geometry_id|skin1,skin2,...[|bone=rx,ry,rz;...]` —
   `geo_file`/`skin` paths are under `BEDROCK_SAMPLES`
   (`bedrock-samples/resource_pack` in the repo by default — see
   `docs/blocks.md`), the
   trailing field overrides rest-pose bone rotations that Bedrock keeps in
   animation files (the pig's `body=-90,0,0` comes from
   `animation.pig.setup.v1.0`; with `+90` the body cube swings up and back
   instead of lying flat — keep the `-90` sign).
2. `make mobs-import` runs `tools/mob_import.py` →
   `include/common/mob_models_gen.hpp` + `bitmaps/mobs.png`. Output is
   deterministic (byte-identical reruns); the importer handles geo.json
   1.8 and 1.12, box UVs only (per-face UVs are rejected), bone
   mirror/pivot/rotation, and pads each skin into one 64x64 layer of the
   atlas. `"geometry.a:geometry.b"` inheritance merges bones by name:
   a same-named child bone's cubes layer on top of the parent's (that's
   how the sheep's wool sits over the sheared body — `reset: true` on a
   bone drops the inherited content instead).
3. Add a `MobTypeDef` row in `src/common/mob_types.cpp` (+ `MOB_*` enum in
   `mob_types.hpp`): model/skin bindings, hitbox, health,
   walk/panic/chase speeds, `fall_cap`, `attack_damage`, drops, sound
   pool names, natural-spawn data, AI profile (`MOB_AI_PASSIVE`,
   `MOB_AI_HOSTILE_MELEE`, `MOB_AI_HOSTILE_CREEPER`, `MOB_AI_REMOTE`),
   animation mask (`MOB_ANIM_*` — quadruped/humanoid walk, head look,
   death tip, sneak, wing flap, creeper swell) and the head/leg/arm/
   body/wing bone names the animators bind to (NULL = unused).
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
`chunk_x_y_z`/`chest_x_y_z` files — `u32 record_size`, `u32 count`, then
packed `MobFileRecord`s (readers copy the shared prefix and skip
`type >= MOB_TYPE_COUNT`; file formats: `docs/persistence.md`). An
existing-but-empty file is a *populated marker*: a chunk whose herd was
killed or removed never naturally
respawns. The terrain worker loads the file (or rolls the `spawn_chance`
≈1/12 natural spawn on fresh terrain, seeded `hash(chunk_pos, type,
MAP_SEED)`) into `Chunk::pending_mobs`; `World::render` hands
just-finalized chunks to `MobManager::adopt_chunk`, and a ~16-tick poll
persists+removes mobs whose owning chunk leaves the active window
(parked/reused — `CHUNK_LOADING` at the same position has not left).
`World::cleanup` persists every owned chunk. `despawnable` (night-spawn)
mobs are never written. No writes in capture or fly
mode — and no local reads/writes at all in multiplayer, where the server
holds `mobs_`/`items_` sidecars and the populated markers (see
`docs/entities.md` "Multiplayer entity sync"). Natural spawn stands herds on `spawn_on`
column tops with hitbox headroom inside the chunk; in multiplayer the
chunk's owning client runs the roll on its CHUNK_ROLE grant and reports
`CHUNK_POPULATED`.

Hitting: left click raycasts mobs (`MobManager::raycast`, AABB slab test)
against `block_selection.ray_origin/ray_dir`; a mob nearer than the
selected block takes `HIT_DAMAGE` (4.0, creative kills in one hit), gets
knockback + a 0.3 s red flash + 5 s panic, and rolls `MobTypeDef::drops`
in survival when killed.
