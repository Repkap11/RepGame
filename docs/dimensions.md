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
explosions, natural spawns, the despawnable-mob cap, music set.
Gameplay/rendering code reads
`dimension_profile(dim)` rather than scattering `dim == DIM_NETHER`
checks. The spare slots are inert voids (no sky, no spawns).

## Terrain

`include/common/map_gen_nether.hpp` (`mgn_pick_block`) is the shared
nether generator in the multi-backend dialect — bedrock floor, a ~90-block
netherrack underworld (cave worms and big hollows fade out with depth
rather than clipping, so lava-filled voids get rounded beds),
basin-sagged lava lakes whose beds bowl toward their centres,
quartz/gold veins, glowstone veins plus
crystal clusters hanging from the cavern ceiling,
soul sand/soil, basalt veins, magma veins clustered in the lava-sea
band (patchy crust on the submerged seafloor and shorelines, flecks
shot through the basalt deltas — it also burns players standing on
it, `PLAYER_MAGMA_DAMAGE`), sparse natural `FIRE` on netherrack
surfaces and `SOUL_FIRE` on soul-valley tops (both emit light via their
block definitions), and pillars that fuse the cavern floor to
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
it up; `StructureGen::place` also stamps huge fungi (crimson/warped
stems, wart caps with embedded shroomlights, weeping vines on crimson,
twisting vines on warped)
onto the nylium crust — `place_nether_trees` in `structure_gen.cpp`
finds the up-facing surface by probing `gen_block_id` down the column
and roots a `SHAPE_FUNGUS` tree wherever the top cell is nylium, so
species follows the painted fungal-forest patches. Flat/block worlds
bypass dimensions entirely.

## Lighting + environment

`has_sky = 0` in the nether gates the skylight model in `light_chunks.cpp`: no
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
  inside a portal, then `portal_travel` records the source frame's
  interior centre (`src_center`), drops the player on generated ground
  in the other band (`hold_feet`), and freezes them there — each
  pending tick re-snaps position and zeroes velocity until the
  destination resolves.
- **Readiness** (`portal_destination_ready`): the resolve waits for
  (1) the loader window to reach the hold chunk, (2) every chunk
  overlapping the search box (src_center x/z ± `PORTAL_SEARCH_R`, full
  destination y band) inside the *active* window to be `CHUNK_LIVE`, and
  (3) in multiplayer, `CHUNK_DIFF_DONE` confirmation of every diff box
  sent when the gate first passed (a snapshot — new requests can't
  extend the wait; if a reconnect resets the counters so sent drops
  below the target, `portal_destination_ready` re-snapshots). The gate
  never gives up early anymore: `PORTAL_RESOLVE_TIMEOUT_TICKS` is a
  soft timeout that only logs "still waiting" once, and the hard cap
  `PORTAL_RESOLVE_GIVEUP_TICKS` (60 s) resolves without building —
  `portal_find_nearest` may still adopt an already-loaded portal, but
  if none is found the player is simply left at `hold_feet`. Building
  on chunks whose diffs never landed is what planted a new frame
  beside the real portal, so the give-up path clears `pending_build`/
  `pending_carve` and writes nothing.
- **Early adopt**: while the gate is unmet, every
  `PORTAL_ADOPT_SCAN_TICKS` (~0.5 s) `portal_try_adopt` re-scans the
  box's live chunks; a PORTAL cell can only sit in confirmed chunks, so
  the first one found moves `hold_feet` inside its plate and the player
  waits *there* — the eventual release is a no-op teleport instead of a
  visible snap from the predicted site to the real portal. A nearer
  portal in a still-unsettled chunk can still win at resolve.
- **Resolve**: `portal_find_nearest` scans the box's live chunks for the
  closest existing PORTAL cell (horizontal distance to src_center, |dy|
  tie-break) and the player is stood inside its plate; otherwise the
  build site is picked from the *loaded* world (spiral from src_center,
  not the hold point) and any candidate whose clearance volume —
  u in [-2, w+1], v in [-3, 3], y in [0, h+2] — holds PORTAL/OBSIDIAN/
  BEDROCK is vetoed, so a new frame never overlaps or carves into an
  existing one. A built frame copies the source's axis and interior
  size; landing inside any portal sets `must_exit` +
  `PORTAL_COOLDOWN_TICKS` so you don't bounce straight back —
  `must_exit` is only released on loaded non-portal ground.
- **Carve**: `portal_carve_pocket` still opens a pocket around embedded
  sites, but never writes over PORTAL, OBSIDIAN or BEDROCK.
- `/dimension [overworld|nether]` (`chat_commands.cpp`) is the portal-less
  debug path: same landing-column search, no frame built. Spare slots and
  same-dimension travel are refused.

## Rules

Per the profile: water placement evaporates in the nether
(`bucket.cpp`), beds explode on sleep attempts (`player.cpp`), and the
music set switches (`audio.cpp` `MUSIC_SET_*`). The nether runs the same
two natural-spawn paths as the overworld but on its own table: the
fresh-chunk roll and the despawnable spawner are both gated per type by
`MobTypeDef::spawn_dims`, so nether chunks roll zombified piglin herds
(on netherrack/crimson nylium) and wither skeletons (soul sand,
netherrack, blackstone), and the
2 s spawner picks zombified piglin .7 / wither skeleton .3 instead of the
overworld zombie/skeleton/creeper mix (docs/mobs.md).

## Multiplayer

`NET_PROTOCOL_VERSION` is 21 — v18 introduced the dimension band and
v19 dropped the floor heightfield's mid-frequency octave (base-terrain
contract changes); v20 added `CHUNK_DIFF_DONE` so clients can tell when
a request box's edits have all been applied (portal travel's
readiness gate); v21 raised the nether stack and added basin lakebeds
(another base-terrain contract change).

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
