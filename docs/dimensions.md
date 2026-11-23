# Dimensions (overworld + nether + end)

RepGame has no separate per-dimension coordinate spaces. A dimension is a
64-chunk band of the signed-8-bit chunk-y field — the narrowest field in the
packed natural-mob id (`net/packet.hpp`), so two high bits of chunk y give
four dimension slots and the low six bits are chunk y inside the slot:

| slot | dimension   | encoded chunk y              | encoded block y |
|------|-------------|------------------------------|-----------------|
| 0    | overworld   | -32..31                      | -1984..1983     |
| 1    | nether      | -96..-33                     | -5952..-1985    |
| 2    | end         | 32..95                       | 1984..5951      |
| 3    | repheim     | 96..127 and -128..-97 (wrap) | ±5952..7936     |

The overworld band is centered on y=0 so existing saves stay in slot 0.
Everything keyed on `ivec3 chunk_pos` — chunk files, block updates, entity
sync, the server — works unchanged; band separation (≈4000 blocks) is far
wider than the vertical load radius, so a torus never straddles two bands.
Portal travel applies Minecraft's **8:1 coordinate compression** — one block
in the nether is eight in the overworld: a source portal's x/z maps to the
destination anchor by /8 when nether-bound and *8 when overworld-bound
(`PORTAL_COORD_SCALE`, `portal_scale_coord`).

All helpers live in `include/common/dimension.hpp`:
`dimension_of_chunk/block_y/pos`, `dim_local_*` (band-local y),
`dim_world_*` (inverse), `dim_name`/`dim_from_name`, plus the WGSL-safe
`DIM_IS_*_BY` predicates above the `REPGAME_WGSL_GEN` guard — the mapgen
dialect (`docs/mapgen.md`) can use only the `#define` half of the header.

The band encoding is hidden from the player: user-facing y runs in the
**display frame** — every band reads overworld-like coordinates (blocks
-1984..1983, chunks -32..31). `dim_display_block_y`/`dim_display_chunk_y`
convert encoded→display (repheim's display y is the rep frame), and
`dim_encode_block_y(dim, y)` converts a display y back into a band,
folding out-of-band input modulo the span. `/pos`, `/tp`, `/setblock`,
`/fill`, `/summon` and the ImGui overlay's Player Pos line all use the
display frame, so a `/tp` y can never cross dimensions; debug-only
consumers (F12's `REPGAME_CAPTURE` log line, rep-dbg, results.jsonl)
keep the real encoded y.

## Per-dimension profile

`include/common/dimension_profile.hpp` is the policy table — skylight,
skybox, fog tint/range, ambient light floor, water evaporation, bed
explosions, natural spawns, the despawnable-mob cap, music set.
Gameplay/rendering code reads
`dimension_profile(dim)` rather than scattering `dim == DIM_NETHER`
checks. Repheim's row mirrors the overworld's (sky, water, safe beds,
natural spawns, overworld music) — it is another overworld.

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
species follows the painted fungal-forest patches. The same nether
branch runs `place_nether_fortress`
(`src/common/structure_fortress.cpp`): deterministic nether-brick
fortresses — multi-section complexes: a two-storey keep linked by
stepped connectors to satellite halls, with corridors converging at
shared junction halls, plus bridge arms, platforms and towers —
anchored in the cavern band (deck at local y ~68–110, between the lava
sea and the roof mass) with loot chests and inert `MOB_SPAWNER` blocks;
layout, carving and loot are pure functions of world coords
(docs/mapgen.md "Structures"). Flat/block worlds
bypass dimensions entirely.

`include/common/map_gen_end.hpp` (`mge_pick_block`) is the shared End
generator — the band is void except an island layer around local y
~1990–2160: a fixed central end-stone dome at x/z 0 (flat top at
`MGE_ISLAND_Y`) holding ten obsidian pillars on a r=43 ring, the exit
fountain (a 3x3 `END_PORTAL` pool in a 5x5 bedrock ring) and the obsidian
arrival pad beside it; outer islands are noise-blob end-stone shelves
past the `MGE_GAP_R` moat, topped with `CHORUS_PLANT`/`CHORUS_FLOWER`
clumps. `map_logic.hpp` dispatches on `DIM_IS_END_BY(y)` in every
backend. The End's chorus JSONs use
`needs_place_on_solid_but_can_stack_on_self` plus the chorus-family
check in `block_supports_present`, so stacked stalks and their flower
cap survive — breaking a lower stem still cascades upward.
`chunk_load.cpp`'s empty-chunk early-out only applies to the overworld
and repheim bands — its "above max terrain" bound would wrongly mark
End island chunks all-air.

Repheim (band 3) is **another overworld**: it runs the reworked
generator — `mg_column_info`/`mg_pick_block` from
`include/common/map_gen_fields.hpp`, the code new-terrain builds used
to select via `REPGAME_MAP_GEN_LEGACY` (now removed) — while the
overworld band keeps the legacy generator; both are compiled into every
backend and `map_logic.hpp` dispatches on the band (`DIM_IS_REPHEIM_BY`,
`DIM_IS_NETHER_BY`, `DIM_IS_END_BY`, else legacy). The band wraps, so
generators evaluate in a **rep frame** (`repheim_gen_block_y` /
`repheim_encoded_block_y` in `dimension.hpp`): rep frame y = band-local
y − 1984, i.e. sea level at 0 just like the overworld — repheim terrain
and structures are identical to a new-gen overworld at the same x/z.
`StructureGen::place` repoints the structure passes' frame too, so
trees/decorations/ores land the same way. There is no portal to repheim
yet — `/repheim` lands there, and `portal_travel_no_build` still
refuses it.

## Lighting + environment

`has_sky = 0` in the nether gates the skylight model in `light_chunks.cpp`: no
column fills, no boundary sky seeds, no cascade sky contribution — the
band is lit only by lava/fire/glowstone/portal emitters. Rendering keys
off the profile too: `World::draw` swaps the panorama skybox for a
fog-colored void (`SkyBox` nether variant) with a short red-brown fog, and
the day/night factor is pinned dark. The profile's `min_ambient` is an
inherent light floor — `max()`'d with the player's Cave Brightness before
it reaches the shaders, so nether caverns stay dimly visible regardless of
that setting. `/pos` and the debug overlay report the
dimension name (`dim_name`) with y in the display frame.

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
  inside a portal, then `portal_travel` scales the source frame's
  interior centre x/z by the 8:1 ratio into the destination anchor
  (`dst_center`), drops the player on generated ground near it
  (`hold_feet`), and freezes them there — each
  pending tick re-snaps position and zeroes velocity until the
  destination resolves.
- **Readiness** (`portal_destination_ready`): the resolve waits for
  (1) the loader window to reach the hold chunk, (2) every chunk
  overlapping the search box (dst_center x/z ± `PORTAL_SEARCH_R`, full
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
  closest existing PORTAL cell (horizontal distance to dst_center, |dy|
  tie-break) and the player is stood inside its plate; otherwise the
  build site is picked from the *loaded* world (spiral from dst_center,
  not the hold point) and any candidate whose clearance volume —
  u in [-2, w+1], v in [-3, 3], y in [0, h+2] — holds PORTAL/OBSIDIAN/
  BEDROCK is vetoed, so a new frame never overlaps or carves into an
  existing one. A built frame copies the source's axis and interior
  size; landing inside any portal sets `must_exit` +
  `PORTAL_COOLDOWN_TICKS` so you don't bounce straight back —
  `must_exit` is only released on loaded non-portal ground.
- **Carve**: `portal_carve_pocket` still opens a pocket around embedded
  sites, but never writes over PORTAL, OBSIDIAN or BEDROCK.
- **End frames**: a horizontal 5x5 ring of `END_PORTAL_FRAME` (the
  generated fountain uses `BEDROCK` in the same role) around a 3x3
  interior, corners ignored — `end_portal_find_frame` /
  `end_portal_ignite` / `end_portal_cell_survives` in `portal.cpp`.
  Flint & steel on an interior cell lights it.
- **End travel**: `repgame_portal_tick`'s `END_PORTAL` branch — standing
  in a pool for `PORTAL_TRAVEL_TICKS` goes to the End's obsidian pad
  (fixed, beside the fountain — no x/z mapping, no destination build,
  no 8:1 scaling); standing in END_PORTAL inside the End drops the
  player on generated overworld ground at the same x/z (the fountain
  sits at x/z 0, i.e. world spawn). The pad wait (`pending_platform`)
  just holds the player until the pad's chunk is live.
- `/repheim` (`chat_commands.cpp`) is the portal-less debug path to the
  repheim band: the same landing-column search at unscaled x/z, no frame
  built. The scan window is the overworld's [-90, 300] shifted into the
  wrapped band's upper half, so below-sea floors fall back to the ledge.
- `/nether` and `/overworld` (`portal_travel_no_build`) run the real portal
  trip minus the frame: 8:1-scaled anchor, hold-and-resolve freeze,
  early/resolve adoption of an existing PORTAL cell, pocket carve on
  embedded sites — but the resolve lands the player on the site instead of
  building a frame (`PortalState::pending_no_build`).

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
overworld zombie/skeleton/creeper mix (docs/mobs.md). The End rolls
endermen on end stone — `MOB_ENDERMAN` gained the `DIM_END` bit and
`END_STONE` floor — and the night spawner applies the spawn_on
whitelist in the End too. Repheim shares the overworld's spawn sets:
every `DIM_OVERWORLD` spawn_dims bit is ORed with `DIM_REPHEIM`.

## Multiplayer

`NET_PROTOCOL_VERSION` is 24 — v18 introduced the dimension band and
v19 dropped the floor heightfield's mid-frequency octave (base-terrain
contract changes); v20 added `CHUNK_DIFF_DONE` so clients can tell when
a request box's edits have all been applied (portal travel's
readiness gate); v21 raised the nether stack and added basin lakebeds;
v22 added nether fortresses; v23 fills band 2 with the End generator;
v24 fills band 3 with the repheim band's reworked terrain and retires
the 0x80 generator-selection bit (all base-terrain contract changes).

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
  end terrain (island/fountain/pillars/moat/outer islands/chorus),
  portal detect/ignite/survive/cascade and travel incl. end rings and
  the repheim band edge.
- `tests/scenarios/nether_portal.rds` — scripted real-world walk:
  build + `ignite`, dwell-travel both ways, break-frame cascade.
- `mp_test.py` `dimension` scenario — two clients, avatar hiding and
  `/tp <player>` across bands.
