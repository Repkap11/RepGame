---
name: mp-test
description: Run, extend or debug the multiplayer end-to-end test harness (`make mp-test`, tools/mp_test.py) — real server + headless clients driven through the REPGAME_MP_CTL command file with JSONL dump assertions. WIP.
---

# mp-test (WIP)

End-to-end multiplayer tests: a real `RepGameServer` plus headless `RepGame`
clients on a free port, driven through ctl files and asserted on JSONL dumps.

**This harness is WIP.** `include/common/mp_test.hpp` is the source of truth
for the ctl command list and env seams; `tools/mp_test.py` is the source of
truth for the scenarios and conventions. Cross-check both before trusting
anything below.

## Run

```sh
make mp-test          # builds linux + server, then runs all scenarios
python3 tools/mp_test.py [-k substr] [--list] [--keep]
```

- `-k <substr>` — run only scenarios whose name matches.
- `--list` — print scenario names.
- `--keep` — keep every artifact dir (failure keeps them regardless).

Artifacts land in `/tmp/mptest_<scenario>_*` and contain `server.log` plus
`client_<X>/{stderr.log,dump.jsonl,ctl.txt}` per client.

## Architecture

- `tools/mp_test.py` boots `out/server/RepGameServer` on a free port with an
  isolated `HOME` (`server_home/` inside the artifact dir).
- Each client runs `out/linux/release/RepGame World1 localhost` headless
  (`SDL_VIDEODRIVER=offscreen`, `SDL_AUDIODRIVER=dummy`) with an isolated
  `HOME` and env: `REPGAME_PORT` (server port), `REPGAME_RENDER_DISTANCE=6`,
  `REPGAME_MP_CTL=<dir>/ctl.txt`, `REPGAME_MP_DUMP=<dir>/dump.jsonl`,
  `REPGAME_MP_DUMP_EVERY=6`, and a `REPGAME_FLY="x,y,z,0,0,0"` speed-0
  camera pin (`goto` commands just move the pin; `run.client(name, pos,
  fly=False)` skips the pin to exercise the real spawn path).
- Client → driver channel: the JSONL dump. Driver → client channel: the
  append-only ctl file (one command per line; new complete lines are
  executed each game tick, partial trailing lines retry next tick).
- `mp_test::tick` runs at the end of `RepGame::tick`
  (`src/common/mp_test.cpp`); `init()` opens the dump lazily — zero cost
  when `REPGAME_MP_DUMP` is unset.

## Ctl commands (see run_cmd in src/common/mp_test.cpp)

| Command | Effect |
|---|---|
| `goto x y z` | pin the camera (re-aims the REPGAME_FLY speed-0 pin) |
| `drop <item> <count> x y z [vx vy vz]` | spawn a dropped-item entity via the normal spawn path |
| `mob <type> x y z` | spawn a mob via `MobManager::spawn` |
| `hit <net_id>` | player melee hit on that mob (local damage on owned, feedback + ENTITY_ACTION on a mirror) |
| `feed x y z [item]` | feed the mob nearest the point (default: its first breed food); mirrors send NET_ACTION_FEED for the owner to validate |
| `shear x y z` | shear the shearable mob nearest the point (as if shears held); mirrors send NET_ACTION_SHEAR for the owner to validate |
| `fall <block> x y z` | spawn a falling-block body |
| `tnt x y z` | spawn a primed TNT body (normal ignite path, `TNT_FUSE_TICKS`) |
| `setblock x y z <block>` | normal player edit (BLOCK_UPDATE) |
| `probe x y z` | emit a `probe` event with the block at that cell |
| `role x y z` | emit a `role` event with the client's role for that chunk |
| `copen kind x y z` | subscribe to the container (CONTAINER_OPEN) |
| `cclose kind x y z` | unsubscribe (CONTAINER_CLOSE) |
| `cset kind x y z slot item n` | optimistic slot write + CONTAINER_SET_SLOT |
| `cprobe kind x y z` | emit a `cprobe` event with the world-owned contents `{kind,pos,is,rev,slots:[{slot,name,count}],aux}` — the sync result the server left |
| `mode survival\|creative` | pin game mode each tick (survival also pins walking — the FLY pin can't undo it) |
| `peaceful on\|off` | hostile-mob targeting (off = targetable; survival alone no longer does it) |
| `time <ticks>` | set world time and broadcast it |
| `dump [tag]` | force a state dump now |
| `quit` | clean exit through the normal path |

Unknown or malformed lines emit a `cmd_error` event.

## Dump stream

Two line types:

- `{"type":"event","tick":N,"name":"...", "detail":{...}}` — emitted
  immediately. Event names: `welcome`, `ready`, `view_sent`, `cmd_error`,
  `explosion_received`, `pickup_result`, `player_hit_received`,
  `entity_transfer`, `probe`, `role`, `cprobe`, `quit`.
- `{"type":"state",...}` — every `REPGAME_MP_DUMP_EVERY` ticks:
  - top level: `tick`, `session` (string), `connected` (0/1),
    `world_time`, `pos` (camera, `[x,y,z]` floats), `tag` (when dumped via
    `dump <tag>`), `roles` `{"owner":N,"viewer":N}`.
  - `inventory`: `{ITEM_NAME: total}` across hotbar + survival.
  - `items`: `{net_id, remote, pickup_pending, item, item_name, count,
    pos, vel, age, grounded, in_water, spin_seed, yaw, bob}`.
  - `mobs`: `{net_id, remote, uid, type, type_name, variant, pos, vel,
    yaw, health, ai_state, hurt_ticks, death_ticks, fuse_ticks, walk_dist,
    leg_amp, head_yaw, head_pitch, sneak, despawnable, targeting, sheared,
    net_flags}`.
  - `bodies`: `{net_id, remote, kind, block, pos, vel, grounded,
    fuse_left}`.
  - Net ids are **decimal strings** (64-bit, JSON numbers lose precision).
  - Mobs with `uid != 0` are remote-player avatars — they legitimately
    have no net_id; `mobs_of()` in the driver filters them out.

`ready` means: connected + WELCOME received + the camera's chunk is
`CHUNK_LIVE` + at least one CHUNK_ROLE grant applied.

## Scenario conventions

- Two clients `A`/`B`; `POS_A`/`POS_B` are 4 chunks apart
  (`NET_ENTITY_RADIUS_XZ` ±8 means both still see entities in between —
  chunks are 62x62). `POS_C` exists for late joiners.
- `settle(run, A, B)` builds deterministic 5x5 STONE pads at x=10 and
  x=260 (`platform()`, `PLATFORM_Y` 100 block layer; pad top =
  `PLATFORM_Y + 1`), pins the cameras just above them, and sleeps 80
  ticks — chunk ownership is then decided horizontally and each hop
  between pads is a decisive ownership flip (past the +2 hysteresis).
- Cross-client comparisons must be confined to the shared view band —
  the two pads' full views differ, so `snapshot()`/`mob_ids()` clip to
  the overlap (see `sc_ownership_flip_no_dup` / `sc_natural_mobs` for the
  band bounds).
- `wait_probe`/`wait_block` poll `probe` until the block reports the
  wanted name; `cprobe`/`cslot`/`wait_cslot` do the same for container
  contents; `find_ground_y`/`goto_ground` exist but the stone pads are
  preferred (heightmap probing was racy).
- Timeouts: `run.wait(desc, pred, timeout)`, `run.sleep_ticks(n)` (144
  UPS), `run.wait_ready(...)`. Raise `Fail(...)` to fail a scenario.

## Adding a scenario

1. Write `def sc_<name>(run, A, B):` using the helpers above; raise
   `Fail` on a bad assertion.
2. Append `("<name>", sc_<name>)` to `SCENARIOS` (clients A and B are
   booted for every scenario; spawn more with `run.client(name, pos)`).
3. If you need a new ctl command, add it in `run_cmd`
   (`src/common/mp_test.cpp`) **and** the command list in
   `include/common/mp_test.hpp`; new state fields go in `dump_state`.
   The world actions themselves live in `dbgcmd::`
   (`src/common/harness/debug_commands.cpp`), shared with the
   `REPGAME_SCRIPT` scenario runner — extend those when the action is
   generally useful.

## Debugging

- Re-run one scenario: `python3 tools/mp_test.py -k item_pickup --keep`.
- Read `server.log` (the server's `se_pr_debug` lines show role grants,
  transfers and persistence) and the failing client's `stderr.log`;
  correlate with its `dump.jsonl` events.
- `role`/`probe` commands double-check server beliefs mid-scenario.
