# Multiplayer & wire protocol

Multiplayer is a client/server session over one TCP stream (WebSocket on
WASM). The client runs the full game locally — terrain, redstone,
physics — and the server arbitrates the shared state: block edits, chest
contents, entity ownership, and the world clock. Entity synchronization
(mobs, item drops, block bodies) has its own protocol layer documented
in `docs/entities.md`; this doc covers the transport, the session
handshake, and the non-entity message types.

## Framing

Every frame on the wire (`include/common/net/packet.hpp`,
`src/common/net/framed_socket.cpp`):

```
[ u32 magic = NET_MAGIC 'RGPT' ]
[ u32 crc32 of everything after this field ]
[ u32 payload_len ]
[ payload: protocol_version(1) | msg_type(1) | player_id(4) | body ]
```

All multi-byte fields are little-endian, written/read explicitly via
`PacketWriter`/`PacketReader` so the format is host-endianness
independent. Payloads are bounded by `NET_MAX_PAYLOAD_LEN` (1 MiB —
sized for a worst-case chunk diff) and request box dimensions by
`NET_MAX_BOX_DIM`. `FramedSocket` owns the partial-frame buffers for
both directions; receivers validate magic/length/CRC before dispatch.
`NET_PROTOCOL_VERSION` is checked on every frame's payload header — a
mismatch disconnects (`Multiplayer::mismatched_server_version`, surfaced
by the ImGui overlay). Bump it whenever a message layout changes; the
`NetMsgType` values are stable on the wire and must never be renumbered.
Protocol 6 added `removed_natural` (tombstoned natural-roll indices) to
`CHUNK_ROLE`; protocol 7 added the `FURNACE_*` messages (since folded
into `CONTAINER_*`); protocol 8 added `item` to
`NetEntityActionPayload` plus `NET_ACTION_FEED` — see `docs/entities.md`;
protocol 9 retired the `CHEST_*`/`FURNACE_*` wire families for the unified
`CONTAINER_*` family (see "Containers"); protocol 10 added
`NetEntityKind::ARROW` (bow projectiles — see `docs/entities.md`).

## Transports

`Multiplayer` (`include/common/multiplayer.hpp`) splits into a shared
dispatch/sender core (`src/common/multiplayer_common.cpp`) and
per-platform transport files:

- **Linux/POSIX** (`src/linux/multiplayer.cpp`): `init` spawns
  `connect_async` — DNS + nonblocking `connect` + `poll` on a background
  thread with `MULTIPLAYER_CONNECT_TIMEOUT_MS`, so an unreachable host
  can't stall startup. `process_events` (called each tick) drains
  inbound frames through `handle_frame`; senders write to
  `framed_socket`'s outbound queue and `flush_outbound` pushes it to the
  socket. Any send/recv error calls `disconnect`.
- **WASM** (`src/wasm/multiplayer.cpp`): the same core over a browser
  WebSocket (`ws_handle`), with Emscripten callbacks wired through
  `WasmMultiplayerHooks`.

`Multiplayer::active` flips true only after the connect completes;
`connect_pending` distinguishes "connecting" from "disconnected" for the
overlay, and `ever_connected` latches on the first success.
`multiplayer_mode()` — hostname + (`connect_pending` or
`ever_connected`) — disables local entity-file persistence while the
session participates in server entity state; a connect that fails
outright leaves the session purely local (see `docs/persistence.md`).

## Session handshake

There is no client hello. On accept the server
(`src/server/server_logic.cpp`, `src/server/server_entities.cpp`) sends
the new client:

- `WELCOME` — the client's `session_id`, the high 32 bits of every
  `NetEntityId` it will mint (`docs/entities.md`).
- `WORLD_TIME` — the server's clock value (`ServerLogic::worldTimeNow`;
  the clock only runs while at least one client is connected).
- `CLIENT_INIT` — one per already-connected player, carrying that
  player's last `NetPlayerPayload` so the new client's avatar list
  starts populated.
- `PLAYER_CONNECTED` — broadcast to the *other* clients.

The client's side after that is driven by data it already produces:

- `PLAYER_LOCATION` — `update_players_position` is called each render
  pass (`src/common/game/game_render.cpp`) but only sends when
  position/rotation/eye_height/flags changed. On receipt, other clients
  update the avatar (`world.multiplayer_avatars`,
  `src/common/multiplayer_avatars.cpp`). The `flags` byte carries
  `NET_PLAYER_FLAG_TARGETABLE` (survival && !no_clip) for mob targeting.
- `VIEW` — sent by `NetEntities::send_view` once welcomed: the view
  center chunk plus radius (clamped to `NET_ENTITY_RADIUS_XZ`/`_Y`).
  The server answers with `CHUNK_ROLE` grants — see `docs/entities.md`.
- `PLAYER_DISCONNECTED` — server broadcast when a client leaves; the
  remaining clients drop the avatar.

## Block updates

`Multiplayer::set_block` sends `BLOCK_UPDATE` for every local write
(called from `World::set_loaded_block` paths, e.g.
`PlayerBlockPlacedEvent`). The server records the edit
(`ServerLogic::record_block` — including mirroring boundary cells into
the neighbouring chunk's diff set) and relays the message to every other
client. Receivers apply it via `world.set_loaded_block` — a plain cell
write, *not* a redstone event, so remote edits don't re-run neighbour
checks locally (`docs/redstone.md`).

## Chunk diffs

The server never sends terrain — clients generate it themselves. What
travels is the per-chunk edit set:

- `CHUNK_DIFF_REQUEST` asks for a box of chunks
  (`request_chunks_box`). `ChunkLoader::render_chunks` fires one box for
  the whole visible window on connect
  (`initial_chunk_request_pending`, retried until it sends) and one box
  covering all newly-entered positions after every window shift.
- `CHUNK_DIFF_RESULT` is one frame per chunk: a list of
  `(blocks_index, BlockState)` pairs covering the chunk's *internal*
  (halo-inclusive) index space. `apply_diff_block` writes each cell and
  mirrors boundary cells into loaded neighbours so seams remesh
  together.

Diffs routinely beat terrain gen to the client, so a result for a
non-live chunk goes into `Multiplayer::pending_diffs` and lands during
`finalize_loaded_chunk` via `apply_pending_diffs` — before light
finalize, so lighting sees the final blocks. The same queue absorbs
local `BLOCK_UPDATE` writes to parked slots and a sender's own writes to
unloaded chunks (`World::set_loaded_block`). Server side, recorded edits
idle for `CHUNK_IDLE_SAVE_SECONDS` before being written to the world's
`chunk_*` files (`scheduleChunkSave`; the deadline lives on the cache
entry's `wake_deadline`, `wake_schedule` is just the ordered hint) — the
file format is the same `docs/persistence.md` describes.

## Containers

Synced containers (chests, furnaces, and any future kind) share one wire
family and one sync model — the `CONTAINER_*` messages all carry a
`NetContainerKind` byte plus a block position, and the per-kind
differences are data in a `NetContainerKindDef` row
(`include/common/container.hpp`): slot/aux arity, the block predicate,
sidecar load/persist hooks, and an optional authoritative tick +
lit-swap pair. Adding a kind is a new row plus client World storage,
not another copy of the subscribe/tick/broadcast machinery.

- `CONTAINER_OPEN` subscribes the client (a client holds at most one
  open container — a new open drops the old subscription server-side);
  the server answers with `CONTAINER_CONTENTS`, a full snapshot: the
  kind's slot array plus its aux ints (furnaces carry `cook_progress`
  and `burn_ticks`).
- `CONTAINER_SET_SLOT` is an optimistic single-slot write in *both*
  directions — the client applies locally then sends; the server
  sanitizes (`container_clean_slot`), bumps the entry's revision, and
  relays to the *other* subscribers stamped with the new revision.
- A write from a client that isn't subscribed to that container is
  rejected — and answered with the authoritative `CONTAINER_CONTENTS`
  for the position so the optimistic copy self-heals instead of
  diverging until the next snapshot.
- `CONTAINER_CLOSE` unsubscribes; disconnect drops all subscriptions.
- Overwriting a container block triggers a `CONTAINER_CONTENTS`
  snapshot to the breaker, consumed via
  `RepGameState::pending_container_loot` so its contents drop as loot.

Client side, `NetContainers` (`state.multiplayer.containers`,
`src/common/net_containers.cpp`) sends and applies the family —
inbound frames only update World-owned container state via the generic
`World::get/set_container_contents` / `set_container_slot` accessors;
the open UI panel repaints itself by pulling from World each tick
(`docs/ui.md`). Server side, `ServerContainers`
(`src/server/server_containers.cpp`) owns the per-chunk container cache,
subscriber maps, lazy sidecar IO (`chest_*`/`furnace_*` files,
`docs/persistence.md`), ticking, and the correction path — `ServerLogic`
just delegates to it.

Ticking kinds (furnaces) run server-side at `UPS_RATE` driven by the
epoll wake schedule: contents mutations mark the chunk dirty and get a
throttled `CONTAINER_CONTENTS` rebroadcast to subscribers; a lit-state
edge additionally broadcasts `BLOCK_UPDATE` (`FURNACE`↔`LIT_FURNACE`,
rotation preserved from the last-seen block state) recorded as a chunk
edit. Clients never tick furnaces while connected — the server is
authoritative.

## Sender / codec layer

The hand-written `net_serialize_*`/`net_deserialize_*` pairs stay the
source of truth for each wire format, but they're registered once in
the codec table (`net_codec_table`, `src/common/net/packet.cpp`): one
row per `NetMsgType` carrying the message name and make/destroy/ser/de
hooks. The table backs:

- `Multiplayer::send<P>(type, payload)` / `Server::send<P>` — the typed
  sender every outbound message uses (serialize → queue → flush →
  error-check, in one place);
- `Server::broadcast` / `broadcast_to` / `broadcast_empty` — the
  fan-out helpers replacing hand-rolled per-fd loops;
- `net_msg_type_name` — readable names for `REPGAME_NET_TRACE` logs and
  debugger output;
- the registry-driven round-trip tests (`test_net_codec_registry`),
  which exercise *every* payload row — a new message type without a row
  fails the contiguity check, so coverage can't silently lag.

`REPGAME_NET_TRACE=1` logs `net_in`/`net_out <name> … n=<bytes>` at the
four transport funnels (client `handle_frame`/`send_payload`, server
`handle_client_ready_for_read`/`queue_message`) — the cheap way to
watch a sync conversation end to end.

## World time

`WORLD_TIME` flows both ways: the server broadcasts it on connect, after
every client `WORLD_TIME` set request (PageUp/PageDown sends
`send_world_time`), and periodically (`WORLD_TIME_SYNC_SECONDS`) to
correct drift. Clients snap `state.world_time` to the value and keep
advancing locally between syncs. The server persists the clock to
`world.dat` (`ServerLogic::init` reads it back; a fresh world starts at
`DAY_LENGTH_TICKS / 4`, noon).

## Testing

- The `net_*` tests (`src/common/tests/test_net.cpp`, `make tests
  TESTS=net`) round-trip every payload serializer — the codec registry
  test enumerates the whole table — plus the container kind defs and
  the entity protocol.
- `RepGameServer --selftest` (`src/server/server_selftest.cpp`,
  `make server-test`) simulates clients against `ServerLogic` directly:
  WELCOME sessions, VIEW/CHUNK_ROLE coverage, transfers, and the
  container conversation (subscribe, relayed writes with revisions,
  rejected-write correction, loot-on-break, furnace ticking +
  lit-swap, sidecar persistence).
- `make mp-test` drives real server + headless clients through JSONL
  assertions — see the `mp-test` skill and `docs/entities.md`; the
  `containers` scenario drives `copen`/`cset`/`cprobe` end to end.
