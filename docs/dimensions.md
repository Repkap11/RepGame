# Dimensions (overworld + nether)

RepGame has no separate per-dimension coordinate spaces. A dimension is a
64-chunk band of the signed-8-bit chunk-y field — the narrowest field in the
packed natural-mob id (`net/packet.hpp`), so two high bits of chunk y give
four dimension slots and the low six bits are chunk y inside the slot:

| slot | dimension   | encoded chunk y              | encoded block y |
|------|-------------|------------------------------|-----------------|
| 0    | overworld   | -32..31                      | -1984..1983     |
| 1    | nether      | -96..-33                     | -5952..-1985    |
| 2    | spare       | 32..95                       | 1984..5951      |
| 3    | spare       | 96..127 and -128..-97 (wrap) | ±5952..7936     |

The overworld band is centered on y=0 so existing saves stay in slot 0.
Everything keyed on `ivec3 chunk_pos` — chunk files, block updates, entity
sync, the server — works unchanged; band separation (≈4000 blocks) is far
wider than the vertical load radius, so a torus never straddles two bands.
There is deliberately **no 8:1 coordinate compression**: portal travel keeps
x/z.

All helpers live in `include/common/dimension.hpp`:
`dimension_of_chunk/block_y/pos`, `dim_local_*` (band-local y),
`dim_world_*` (inverse), `dim_name`/`dim_from_name`, plus the WGSL-safe
`DIM_IS_*_BY` predicates above the `REPGAME_WGSL_GEN` guard — the mapgen
dialect (`docs/mapgen.md`) can use only the `#define` half of the header.

## Per-dimension profile

`include/common/dimension_profile.hpp` is the policy table — skylight,
skybox, fog tint/range, ambient light floor, water evaporation, bed
explosions, natural spawns, music set. Gameplay/rendering code reads
`dimension_profile(dim)` rather than scattering `dim == DIM_NETHER`
checks. The spare slots are inert voids (no sky, no spawns).

## Terrain

`include/common/map_gen_nether.hpp` (`mgn_pick_block`) is the shared
nether generator in the multi-backend dialect — bedrock floor,
netherrack caverns, lava sea, quartz/gold ore, glowstone ceiling clusters,
soul sand/soil, basalt veins, and pillars that fuse the cavern floor to
the roof — a sparse 2D field converges `floor_h`/`roof_base` onto a
vertically-wandering mid-band (no fixed "mid line"), so pillars are
terrain deformed into mountains fused into the ceiling with hourglass
pinches, not stamped columns; a separate masked field lays floating
shelf ledges through the mid-cavern that merge into pillars where they
touch, and carving bores windows and tunnels through both. The roof mass
is a bounded
`MGN_ROOF_THICK` layer over `roof_base` with carving held a few blocks
below the jittered bedrock cap, so the ceiling always keeps a netherrack
skin; past the cap the rest of the band is solid bedrock filler, so
digging/flying up ends in ~50 blocks of rock, not kilometres of caves.
`map_gen.cpp` dispatches on
`DIM_IS_NETHER_BY(y)` so CPU, CUDA, HIP and the WGSL/WebGPU path all pick
it up; `StructureGen::place` stays overworld-only (nether features are
baked into the generator). Flat/block worlds bypass dimensions entirely.

## Lighting + environment

`has_sky = 0` in the nether gates the skylight model in `light.cpp`: no
column fills, no boundary sky seeds, no cascade sky contribution — the
band is lit only by lava/fire/glowstone/portal emitters. Rendering keys
off the profile too: `World::draw` swaps the panorama skybox for a
fog-colored void (`SkyBox` nether variant) with a short red-brown fog, and
the day/night factor is pinned dark. The profile's `min_ambient` is an
inherent light floor — `max()`'d with the player's Cave Brightness before
it reaches the shaders, so nether caverns stay dimly visible regardless of
that setting. `/pos` and the debug overlay show the
dimension name (`dim_name`, `dim_local_block_y`).

## Portals

`include/common/portal.hpp` — `portal.cpp` (world helpers) +
`portal_travel.cpp` (per-tick `PortalState` on `RepGameState`).

- **Frame**: an obsidian ring, interior 1x2..16x25
  (`PORTAL_MAX_INTERIOR_W/H`), detected by `portal_find_frame` from any
  AIR/FIRE/SOUL_FIRE/PORTAL interior cell.
- **Ignition**: `portal_ignite` validates the ring and fills the interior
  with PORTAL cells — each a `PlayerBlockPlacedEvent`, so ignition syncs
  in multiplayer. Flint & steel use calls it; a fire spread can too.
- **Survival**: `portal_cell_survives` (the `block_can_survive_at` rule)
  requires runs to obsidian through PORTAL cells along both in-plane
  horizontal directions and vertically — breaking any frame block pops
  the adjacent cell, cascading through the whole interior.
- **Travel**: `repgame_portal_tick` counts `PORTAL_TRAVEL_TICKS` (80)
  inside a portal, drops the player at the same x/z in the other band,
  then `portal_find_nearest`/`portal_build_frame` materialize a lit
  destination once its chunks load (`pending_build`). Landing inside the
  portal sets `must_exit` + `PORTAL_COOLDOWN_TICKS` so you don't bounce
  straight back.
- `/dimension [overworld|nether]` (`chat_commands.cpp`) is the portal-less
  debug path: same landing-column search, no frame built. Spare slots and
  same-dimension travel are refused.

## Rules

Per the profile: water placement evaporates in the nether
(`bucket.cpp`), beds explode on sleep attempts (`player.cpp`), natural
mob spawning is off (`chunk.cpp` spawn gate), and the music set switches
(`audio.cpp` `MUSIC_SET_*`).

## Multiplayer

`NET_PROTOCOL_VERSION` bumped to 18 — packet layouts are unchanged, but
the base-terrain contract differs: an old client would generate bedrock
where a new one generates the nether.

Remote avatars are dimension-filtered in `multiplayer_avatars.cpp`:
`update_position` despawns the mob when a position packet lands in a
different band and respawns it on re-entry — but keeps the streamed
`eye_pos_map`/`eye_height_map`, so `/tp <player>` works even for a hidden
player. `fill_remote_targets`/`fill_remote_feet` have `DimensionId`
overloads so mob targeting and cart shoving can't reach across bands.
Non-player entities need no filter: bands are ~4000 blocks apart, so
items/arrows/bodies/vehicles are always torus-local.

## Tests

- `test_dimension.cpp` — band classification (incl. the signed wrap),
  local/world round-trips, profiles, mob-id band separation, nether
  terrain properties (roof/floor/caverns/lava sea/no overworld blocks),
  portal detect/ignite/survive/cascade and travel.
- `tests/scenarios/nether_portal.rds` — scripted real-world walk:
  build + `ignite`, dwell-travel both ways, break-frame cascade.
- `mp_test.py` `dimension` scenario — two clients, avatar hiding and
  `/tp <player>` across bands.
