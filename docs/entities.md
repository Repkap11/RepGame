# Entities: block bodies, arrows and multiplayer sync

The dedicated server is a thin C++ binary under `src/server/`
(`out/server/RepGameServer`, `make server` / `make server-run`): it holds
the world save, relays block updates, and arbitrates entity ownership
(`ServerEntities` in `src/server/server_entities.cpp`) but never simulates
gameplay — owner clients do that and stream the results. `RepGameServer
--selftest` (`make server-test`) exercises the sync rules end-to-end. The
wire framing, session handshake, chunk diffs, chest sync and world clock
that ride the same connection are in `docs/multiplayer.md`.

## Block bodies (falling blocks + primed TNT)

`BlockBodies` (`include/common/block_bodies.hpp`, `src/common/block_bodies.cpp`,
owned by `World` as `world.bodies`) is the shared CPU simulation for
axis-aligned block-sized bodies (0.98 box, `MAX_BLOCK_BODIES` = 1024, stable
nonzero ids). It integrates two kinds: `BODY_FALLING` (any block with
`falls_with_gravity` — sand, gravel, concrete powder, anvils) and
`BODY_PRIMED_TNT` (ignited TNT). Physics runs at `UPS_RATE` inside
`bodies.tick` (called right
after `mobs.tick`): gravity/drag/terminal velocity (`GRAVITY_STRENGTH`,
`TERMINAL_VELOCITY`, air drag ~0.9972), water medium (`WATER_GRAVITY_STRENGTH`,
`SWIM_TERMINAL_VELOCITY`, ~0.97 drag), ground friction 0.952, wall restitution
0.2, per-cell broadphase body-vs-body collision with inelastic exchange, and
voxel collision via `Collision::move_box` (same axis resolution/substepping as
the player path, Y→X→Z, no step-up, no edge guard; unloaded cells are solid).
Bodies freeze in capture mode and while their containing chunk is not
`CHUNK_LIVE` — a primed TNT frozen in a parked chunk still explodes at its
frozen position when the fuse event lands.

A `falls_with_gravity` block releases as a body when a neighbor update leaves
it over a loaded non-colliding cell — the trigger sits in `perform_checks`
(`BlockNextToChangeEvent.cpp`), queuing `FallingBlockEvent` two ticks out so a
quick support restore wins; the event re-checks live state and releases the
whole contiguous run of falling blocks above. A grounded, slow, grid-aligned
body settles after a few ticks and `settle_land` writes it back
(`set_loaded_block` + `multiplayer.set_block` + neighbor updates): air/water/
replaceable cells take the block (a replaced plant breaks into debris), a
non-colliding cell (torch) breaks the falling block into debris, a cell that
went solid meanwhile (another body landed beneath) stacks on top, and a rest
cell over unloaded space drops as debris. Bodies blocked by the player keep
waiting. `land_all` runs before `world.cleanup` so live bodies persist into
the save.

Primed TNT keeps its fuse as an event: `tnt_ignite`/`tnt_launch` (in
`TntExplodeEvent.cpp`) remove the block, spawn a `BODY_PRIMED_TNT` with a
small hop, and queue `TntExplodeEvent(tick + fuse, fallback_pos, body_id,
inverse)` — body id 0 is the non-body path (creepers). When the blast runs it
`take()`s the body so it explodes at the real position, and shoves every live
body (`bodies.impulse`, radius R+2 — still-fusing primed TNT and falling
blocks already in flight). Gravity blocks sitting in the world are destroyed
like everything else; a gravity block that loses support outside the sphere
still falls normally through `FallingBlockEvent`. TNT spawning at the body cap evicts the oldest falling body; a falling
spawn at the cap leaves the block in place.

Rendering is client-side instances rebuilt per frame in `update_render`
(interpolating `prev_min`→`min` by alpha, per-body light sampling): falling
blocks draw through `render_falling` + `block_object_shader`
(`BlockObjectInstance`: transform + light + the block's pixel-space
shape_scale/offset/tex_offset, folded into the unit cube by
`block_object_vertex.glsl` with cropped per-face UVs — the literal
`Block::offset`, so a non-full body matches the block it lands as), primed
TNT through `render_tnt` +
`primed_tnt_shader` (`PrimedTntInstance`: transform + light + CPU fuse-blink
`flash`, same shader file `primed_tnt_vertex.glsl` reworked to consume the
instance transform). Chains are only touched after `init()`/`render_inited`,
so headless tests simulate fine. Landed bodies keep an instance at their rest
cell until the target chunk's `needs_repopulation` drains (the remesh safety
net). In multiplayer each body is simulated by the client that caused it
(the existing "causer simulates" rule — remote block updates still don't
run these events) and mirrored to viewers; see "Multiplayer entity sync"
below for the spawn/state/remove/handover rules.

## Arrows

`ArrowEntities` (`include/common/arrows.hpp`, `src/common/arrows.cpp`,
owned by `World` as `world.arrows`) is the projectile sim for bow arrows —
a headless CPU tick in the same shape as `ItemEntities`/`BlockBodies`
(`MAX_ARROWS` = 256, oldest evicted at the cap; frozen in capture mode and
while the center's chunk isn't `CHUNK_LIVE`). `repgame_fire_arrow` (the
bow's right-click) spawns one at `ARROW_SPEED` (0.42 blocks/tick, ~60
blocks/s) biased a step along the look vector; each tick integrates
gravity 0.001/tick² + drag (air 0.9986, water/lava medium 0.93 — arrows
still sink underwater), then moves a point-sized collision box
(`Collision::move_box`, so plants/torches don't collide) along the
segment and cross-checks `MobManager::raycast` against the whole intended
path — the nearer of block-vs-mob wins, so a fast arrow can't tunnel
through either. move_box only clamps faces the leading edge crosses, so
a spawn already inside a solid (a point-blank release into a wall) is
settled as a block hit where it sits (`Collision::box_collides`) instead
of tunnelling through. Lava/fire contact destroys the arrow with a `fizz`.

A block hit parks the arrow: `stuck` freezes the sim with the tip ~0.1
inside the hit face (`stuck_cell` is the cell whose collision face stopped
the box — a grazing edge hit can leave the rendered tip over air, so it
can't be derived from the tip position; it is rechecked each tick —
breaking that block releases the arrow to fall again, while an unloaded
anchor counts as still holding since collision treats it as solid), the
`arrow_hit` pool plays, and
the shooter-side stuck arrow can be walked over to collect one `ARROW`
into the inventory once `ARROW_PICKUP_DELAY_TICKS` counts down (the same
cooldown a player-thrown item spawns with; item-pickup box semantics,
`pop` sound; full inventory leaves it in place) and despawns after
`ARROW_DESPAWN_TICKS` (5 minutes). A mob hit emits an
`ArrowEntities::ArrowMobHit` via `ArrowTickOutput` and removes the arrow;
`RepGame::tick_entities` applies it through the melee path —
`mobs.hurt` (survival `ARROW_DAMAGE` 9.0, creative bows carry the
one-shot 1000.0) + `spawn_mob_drops` on kill, or ENTITY_ACTION/
NET_ACTION_HIT to the owner of a mirrored mob — so knockback, hurt
flash, panic and baby-drop rules all match melee.

Rendering reuses the dropped-item extruded-sprite mesh/shader
(`ItemSpriteInstance` over the shared `item_sprite_vertices` buffers,
`item_sprite_shader`): the `arrow` icon's diagonal is rotated in-plane
onto the shaft (the sprite mesh's +y samples image-down, so the
in-plane rotation is 315° so the tip leads), the sprite's +y aligns to the flight
direction (`orient` — the last nonzero velocity direction, which
stays put while stuck). The sprite plane is fixed in world space:
its normal is `cross(world up, fwd)` (x-axis fallback for vertical
shots), so a stuck arrow never reorients as the camera moves —
viewed shaft-on it correctly reads as a thin edge. Arrows are
never persisted to disk.

Multiplayer follows the body's "causer simulates" model (`NetEntityKind::ARROW`):
the shooting client runs the sim and streams ENTITY_STATE (stuck plus the
shooter's survival flag ride `NetEntityStateEntry::flags`,
`NET_ARROW_FLAG_*`); viewers hold mirrors that ease toward the streamed
pose with no physics/pickup/despawn of their own; the shooter's
disconnect hands the sim to the arrow's chunk owner via ENTITY_TRANSFER
adopt=1, and arrows in uncovered chunks are dropped rather than
persisted. Remote mob hits still go through ENTITY_ACTION.

## Multiplayer entity sync

Protocol v10 (`include/common/net/packet.hpp`) — "server arbitrates, owner
client simulates". The server (`ServerEntities`, `server_entities.cpp`)
holds the authoritative entity table per chunk and never simulates;
clients (`NetEntities`, `net_entities.cpp`) announce what they own and
mirror the rest. A client declares its interest with VIEW (clamped to
`NET_ENTITY_RADIUS_XZ` ±8 / `NET_ENTITY_RADIUS_Y` ±3 chunks); per covered
chunk the server assigns roles — OWNER to the nearest covering player
(hysteresis: the incumbent keeps it while within best+2 chunks), VIEWER
to the rest — and pushes CHUNK_ROLE grants carrying the chunk's live
records plus the chunk's tombstoned natural-roll indices
(`removed_natural`). Net ids are `(session << 32) | counter`; session 0
is server-minted (chunk-load restores), and the `0xFF` top byte is the
deterministic natural-mob namespace — `net_natural_mob_id` packs the
home chunk + roll index so tombstones key off the index alone. Every
streamed entity carries a
`net.chunk` — the server's belief of its chunk — which drives all local
ownership checks, so an entity keeps streaming while it physically
crosses until the server's ENTITY_TRANSFER lands.

Messages (`NetMsgType`): WELCOME assigns the session id; VIEW declares
coverage; CHUNK_ROLE grants/revokes per-chunk roles with records;
ENTITY_SPAWN announces a new entity — relayed to *all* viewers including
the sender, whose echo doubles as the spawn's ack (a CHUNK_ROLE snapshot
generated before the spawn reached the server would otherwise wipe the
sender's predicted copy on apply and never restore it); ENTITY_STATE
streams pos/vel (batched
every 6 ticks, changed-epsilon + ~1 s keyframes; chunk crossings apply the
shared `net_entity_next_chunk` margin — an entity only changes chunks once
it sits NET_ENTITY_CHUNK_MARGIN=0.5 blocks inside the new one, so a
boundary-resting entity never flip-flops owners); ENTITY_REMOVE removes
with a reason; ENTITY_TRANSFER switches ownership (adopt=1) or demotes to
mirror (adopt=0); ENTITY_ACTION carries hits against remote entities
(NET_ACTION_HIT + damage), mob feeding (NET_ACTION_FEED + the claimed
`item` — the owning client's `feedable()` re-validates it
against the type's breed foods (`MobTypeDef::breed_items` plus
`breeds_with_flowers`) before applying), and mob shearing
(NET_ACTION_SHEAR — `shearable()` re-validates `item == SHEARS` plus the
adult/unshorn state before the owner's `shear()` applies it);
PICKUP_REQUEST/PICKUP_RESULT arbitrate item pickup races server-side
(exactly one grant, partial pickups reduce the count via a server state);
CHUNK_POPULATED marks a chunk's natural-mob roll done for this coverage
(the server's `rolled` flag is in-memory only — a re-covered chunk
reports `populated=0` and re-rolls, since pristine products were
dropped); PLAYER_HIT carries
a remote mob's melee hit to its target; EXPLOSION relays a blast.

Client-side, items, mobs and bodies share one bookkeeping struct:
`NetSync` (`include/common/net/net_sync.hpp`) carries the net id, the
mirror/owned flag, the server's-belief `chunk`, the easing target
`pos`/`vel`, and the sent-state snapshot + keyframe tick that drive the
6-tick batch cadence. The `netsync_*` helpers (`netsync_find`,
`netsync_apply_record`, `netsync_track_chunk`, `netsync_send_remove`,
`netsync_keyframe_due`/`netsync_moved`/`netsync_mark_sent`) implement the
parts all three entity types used to duplicate: find-by-net-id, spawn
from a record, chunk tracking via `net_entity_next_chunk`, removal, and
send-throttle. Entity-specific payload (item count/pickup, mob
yaw/health/variant/flags, body kind/fuse/transfer) stays per-type.

Items and mobs are chunk-owner simulated: the OWNER spawns, streams
states, and sends removals (LANDED/DIED/DESPAWN/PICKED_UP/EXPLODED);
everyone else holds easing mirrors. The server persists items and
non-despawnable mobs in `items_`/`mobs_` sidecar files on uncover —
client-side entity files are off while the session is or was connected
(`multiplayer_mode`; a never-connected session persists locally).
Natural mob spawning runs on the OWNER of an
unrolled chunk from `Chunk::spawn_mobs`'s seeded roll, announced with
`NET_MOB_FLAG_NATURAL` and completed by CHUNK_POPULATED; the roll skips
the grant's `removed_natural` indices and ids already live locally.
Natural-mob provenance follows the single-player rules
(`docs/mobs.md`): pristine naturals (NATURAL && !TOUCHED) are never
persisted — the server drops their records on uncover and the next
coverage re-rolls them; a mob gains `NET_MOB_FLAG_TOUCHED` on a player
hit or in a player-edited chunk, which tombstones its roll index in the
home chunk's `removed_natural` and turns it into an ordinary persisted
record. The server drops a natural ENTITY_SPAWN when the sender isn't
the owner, the chunk already rolled, the index is tombstoned, or the id
is already live — bouncing it back with ENTITY_REMOVE when the id isn't
live so the sender's copy dies. Hits on mirrored mobs go through
ENTITY_ACTION to the
owner; owned hostiles also target remote players via `remote_targets` and
reach them through PLAYER_HIT (`NetPlayerPayload.flags` bit0 =
targetable). Mob breeding state rides the same streams: `NetEntityRecord.age_ticks`
is negative for a baby's remaining growth and positive for an adult's
re-breed cooldown, `NET_MOB_FLAG_BABY` carries the half-scale flag through
state entries (a mirror keeps its own countdown while flagged), and
feeds reach the owner as ENTITY_ACTION/NET_ACTION_FEED — see
`docs/mobs.md` "Breeding". `NET_MOB_FLAG_LOVE` carries a mob's in-love
state the same way; its rising edge opens the feed-heart window on
every client, not just the owner. `NET_MOB_FLAG_SHEARED` likewise
carries the sheared-sheep state (model swap + wool-off flag through
spawns and state entries — see `docs/mobs.md` "Shearing"), its rising
edge playing `sheep_shear` on mirrors, and persists through the
provenance mask in the `mobs_` sidecars like TOUCHED.

Bodies (falling blocks + primed TNT) and arrows differ: the *causing*
client simulates — ENTITY_SPAWN fixes a sim fd
(`net_entity_causer_simulated`, the server's `body_sim_fd` table covers
both kinds), only that client's states and removals count, chunk
crossings never transfer, and crossing/spawning into uncovered space
just drops the server record (bodies and arrows are never persisted).
When the sim disconnects, the entity's chunk owner takes over via
ENTITY_TRANSFER adopt=1 (primed TNT reschedules its fuse event
locally); entities in uncovered chunks are erased. Mirrors interpolate
only — no physics, landing, impulses, or collisions — and die on
ENTITY_REMOVE, CHUNK_ROLE NONE, or disconnect. Blasts send EXPLOSION
after running locally; receivers play the sound and impulse their own
bodies/items (mirrors skip impulses — their sim applies it), while block
damage arrives separately through BLOCK_UPDATE.

`make server-test` covers ownership, hysteresis, relays, crossing,
pickup arbitration, persistence, natural-spawn gating, body sim rules,
handover and disconnect cleanup end-to-end against the real ServerLogic.

### WIP notes

Two behaviours currently in the tree are works-in-progress:

- **Server-side chunk-crossing hysteresis** (WIP): the server applies
  `net_entity_next_chunk` (`NET_ENTITY_CHUNK_MARGIN` = 0.5,
  `include/common/net/packet.hpp`) when it updates an entity's `net.chunk`
  from streamed states in `server_entities.cpp` — an entity only changes
  chunks once it sits at least 0.5 blocks inside the new one, so a
  boundary-resting entity never flip-flops owners.
- **Client stale-grant filtering** (WIP): `late_ids`/`gone_ids` in
  `include/common/net_entities.hpp` keep a pending CHUNK_ROLE grant's
  record snapshot honest — ENTITY_REMOVE lands ids in `gone_ids` so dead
  entities aren't resurrected by a late grant (or by a stale spawn relay,
  including our own echo), and ENTITY_SPAWN adds ids to `late_ids` so
  post-grant spawns survive the grant's resync wipe. The sender-included
  spawn echo covers the remaining gap — a spawn sent while the chunk's
  grant was still in flight registers into the pending grant's
  `late_ids` (or respawns after a wipe, since the snapshot can't know
  about it either way), and `late_ids` carry over when a newer grant
  supersedes a still-pending one. The resync wipe additionally keeps any
  entity whose id carries our session (`id >> 32 == session`): our in-view
  spawns are always live server-side, so absence from the snapshot only
  means it predates the spawn — that holds even against a server that
  pre-dates the spawn echo. The exception is marked by `stray_spawns`:
  spawns sent while their chunk sat outside our advertised VIEW are
  sidecar-persisted under no id and re-minted on cover, so the wipe still
  runs for them (the remapped record respawns in the same grant).
  Entities kept via `keep_ids` settle
  `net.mirror` to `!owned` at apply: a relay received before the role was
  known can't guess the sim side correctly.

### Multiplayer e2e harness

`make mp-test` runs `tools/mp_test.py`: it boots a real `RepGameServer`
plus headless clients (`SDL_VIDEODRIVER=offscreen`,
`SDL_AUDIODRIVER=dummy`) on a free port and drives them through 15
scenarios (item mirrors/pickup/race/transfer, ownership flips, natural
mobs, mob combat, remote targeting, falling blocks, TNT blast/handover,
persistence, disconnect cleanup, first-VIEW correctness), asserting
cross-client consistency
from per-tick JSON dumps. Full usage: the `mp-test` skill
(`.devin/skills/mp-test/SKILL.md`); the ctl command list and env seams
live in `include/common/mp_test.hpp`.
