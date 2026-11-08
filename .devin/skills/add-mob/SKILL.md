---
name: add-mob
description: Add a new mob type — mobs_manifest.txt entry, make mobs-import, MobTypeDef row, sounds, and how to eyeball/test it. Use when creating or editing mob models, skins or behaviour definitions.
---

# Add a mob

Full behaviour/rendering reference: `docs/mobs.md`.

## Steps

1. Add a line to `tools/mobs_manifest.txt`:
   `name|geo_file|geometry_id|skin1,skin2,...[|bone=rx,ry,rz;...]` —
   `geo_file`/`skin` paths are under `BEDROCK_SAMPLES`
   (`bedrock-samples/resource_pack` in the repo by default), the
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
   walk/panic/chase speeds, `fall_cap`, `hover_h`, `attack_damage`, drops, sound
   pool names, natural-spawn data, AI profile (`MOB_AI_PASSIVE`,
   `MOB_AI_HOSTILE_MELEE`, `MOB_AI_HOSTILE_CREEPER`, `MOB_AI_REMOTE`),
   animation mask (`MOB_ANIM_*` — quadruped/humanoid walk, head look,
   death tip, sneak, wing flap, creeper swell) and the head/leg/arm/
   body/wing bone names the animators bind to (NULL = unused).
4. Optional sounds: append `sfx` entries to `tools/sounds_manifest.txt`
   (base + `_N` variants pool, e.g. `pig_say_1..3`; `pig_death`,
   `pig_step_1..5`).

Skin atlas convention: each skin is one 64x64 layer of `mobs.png`
(`MOB_SKIN_*` is the layer index, NEAREST + no mipmaps). UV mapping and
the left/right swap for mirrored cubes: `mob_face_rect` in
`include/common/mob_uv.hpp`. The importer promotes any nonzero skin alpha
to opaque (Bedrock entity materials ignore alpha; e.g. sheep.tga uses a=3
as an undercoat marker) — a=0 stays cutout.

## Testing it

- `REPGAME_MOB_TEST="type,x,y,z,yaw[,pitch[,sneak]][;...]"` spawns exact
  poses at init (non-persistent; disables natural spawn + mob-file
  loading). `player` entries spawn a remote-player avatar.
- Combine with `REPGAME_CAPTURE` (see the `capture` skill) to screenshot
  the spawn — capture mode freezes AI/physics so poses are as authored.
- In-game: `n` drops a mob three blocks ahead, cycling pig → cow →
  chicken → sheep → zombie → creeper on successive presses.
