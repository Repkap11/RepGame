# Debug tooling (`tools/rep-dbg`)

`tools/rep-dbg` is the CLI for scripted debugging/profiling runs: the game
writes a structured JSONL event stream (`REPGAME_RESULTS`), every run
produces a schema-versioned `result.json`, and runs are indexed and
queryable. Nobody parses log text — results are structured at the source;
log parsing exists only as a compatibility shim for old revs.

## Quick start

The five commands you will use most:

```sh
rep-dbg fly --flat --ticks 400          # a normal headless flight, settled
rep-dbg show last                        # result.json, pretty-printed
rep-dbg show last --field stats.metric.loader.light_jobs.mean
rep-dbg ab --cfg a=rev:master --cfg b=current --reps 3 stream-flight
rep-dbg metrics last --plot loader.light_jobs,loader.chunks_loading
```

Then read *Results model* once (that is where every answer lives), skim
*Commands by area* for the rest, and keep *Recipes* for worked examples.

## Results model

### `results.jsonl` — the game-side event stream

`REPGAME_RESULTS=<path>` enables a single JSONL writer
(`src/common/harness/results.cpp`). First line is a header; every line has
`type` and (except the header) `tick`.

- `header`: `{"type":"header","schema":1,"git":"<sha>","build":"release|debug",
  "world":<name>,"policy":{...parsed RunPolicy...},"start_unix":<epoch>}`.
  `git` comes from `REPGAME_GIT` (rep-dbg sets it).
- `metric`: every `REPGAME_RESULTS_EVERY` ticks (default 20), sharing the
  same loader/light probe as the FLYMET line:
  `{"type":"metric","tick":N,"fly_phase":0-2,"rd":N,"pos":[x,y,z],
  "loader":{...all LoaderDebugStats fields...},"light":{k:v,...}}`.
  With `REPGAME_PROFILE=1` the record also carries
  `"prof":{<slot>:{n,us,max_us},...}` — see *Profile timers*.
  `light` carries the `light_dbg_stats` k=v counters as numbers where
  numeric; the `| us:` section's keys are prefixed `us_`. Those counters
  are interval counters reset by every `light_dbg_stats` call: in fly mode
  they cover one FLYMET interval (`REPGAME_FLY_METRICS`), otherwise one
  results interval (the periodic `LIGHT probe` log is suppressed while
  `REPGAME_RESULTS` is set so it doesn't steal the window).
- `event`: `{"type":"event","tick":N,"name":...,"detail":{...}}`. Names so
  far: `fly_settled`, `capture_shot` `{view,file,ticks,timeout}`, `quit`.
- `probe`: `{"type":"probe","tick":N,"pos":[x,y,z],"block":id,"name":str,
  "rotation":n,"sky":n,"blocklight":n}` (`results::probe`, used by the
  Phase-5 script runner).
- `assert`: `{"type":"assert","tick":N,"line":n,"cmd":str,"ok":bool,
  "expected":str,"actual":str}` (`results::assert_`, Phase 5).
- `summary`: exactly once at clean exit (`RepGame::cleanup`):
  `{"type":"summary","tick":N,"ticks":N,"exit_reason":"fly_ticks|
  capture_done|walktest_ticks|quit|signal","asserts":{"total","failed"},
  "shots":[...],"settle_tick":N}`. A missing summary means the process died
  abnormally; the runner then reports `crashed`/`timeout`.

### `result.json` — one per run

Assembled by the runner in the run dir (`<root>/<name>/<ts>-<n>/`):

```json
{"schema":1, "id":"<name>/<ts>-<n>", "name":..., "kind":"run|fly|walk",
 "status":"ok|failed|crashed|timeout|build_failed",
 "source":"results|log-shim",
 "git":{"sha":"...","dirty":false,"rev_arg":"master"}, "build":"release",
 "binary":"...", "cmd":[...], "env":{"REPGAME_FLY":"..."},
 "started":"...", "wall_s":41.2, "exit":{"code":0,"signal":null},
 "game":{...the summary record...},
 "stats":{"metric.loader.light_jobs":{"mean":..,"max":..,"p50":..,"p95":..,"last":..}, ...},
 "asserts":{"total":0,"failed":0,"failures":[...]},
 "artifacts":{"log":"stdout.log","results":"results.jsonl","screenshots":[...]},
 "log_flags":{"asan_errors":0,"ubsan_errors":0,"gl_errors":0,"capture_timeouts":0,"aborts":0}}
```

Windowed runs (the `idle` scenario, `perf idle`) add a `"window"` object:
`{after_event, warmup_s, seconds, start_tick, end_tick, frames, fps,
prof:{<scope>:{us_per_call, n}}}` — a fixed wall-clock measurement window
that starts `warmup_s` after the named `results.jsonl` event, ends
`window_s` later (the game is then SIGTERMed, which does not count as a
timeout), and aggregates `prof` over metric records in `(start_tick,
end_tick]`. `frames`/`fps`/`prof` are null/empty without
`REPGAME_PROFILE`. A window that can't complete fails the run with
`window.error`.

`stats` aggregates every numeric leaf of every metric record automatically
(including a derived `prof.<scope>.us_per_call = us/n` per record, skipped
when `n` is 0);
when a `fly_settled` happened, `<key>_after_settle` variants cover the
post-settle metrics only. `log_flags` is the only log scraping — patterns
live in `tools/rep_dbg/logflags.py`. Artifact paths are relative to the run
dir. `"source":"log-shim"` means `results.jsonl` was reconstructed from
`stdout.log` by `tools/rep_dbg/logparse.py` (a `--rev` run of a pre-results
commit) — same pipeline downstream either way.

Schema versioning: new fields are additive; bump `schema` only on breaking
changes.

### The index

`<root>/index.jsonl` gets one line per finished run. Rebuild with
`rep-dbg runs --reindex`. `<root>` defaults to `~/.repgame/rep-dbg`;
`--root`/`REP_DBG_ROOT` override.

### Python API

```python
from rep_dbg import results
r = results.load("last")            # or a run id / run dir
r.status, r.stats["metric.loader.light_jobs"]["max"], r.asserts["failures"]
for m in r.metrics(after_settle=True): m["loader"]["light_jobs"]
r.events("capture_shot"); r.probes(); r.artifact("log"); r.field("stats.metric.rd.mean")
results.query(name="fly-popin", status="ok")   # -> list of Run
```

### CLI conventions

Every subcommand accepts `--json` and then prints exactly one JSON object
on stdout; progress goes to stderr. Exit codes: `0` ok, `1` asserts failed,
`2` crashed/timeout, `3` build failed, `4` usage/preflight error. The last
stderr line of a run is `rep-dbg: <status> <run-dir>`.

## Commands by area

### Runs and presets

- `rep-dbg run [--build release|debug] [--rev <git-ref>] [--world NAME]
  [--flat] [--keep-world <dir>] [--env K=V ...] [--timeout S] [--name N]
  [--window] [--json] [-- <extra game args>]`
  — headless run with an isolated `$HOME`; the world is created (or copied
  via `--keep-world`) under `home/.repgame` in the run dir, so parallel
  runs never collide and `player.dat` writes stay local. `--flat` writes
  `world_type=flat`; `--rev` builds a cached worktree at
  `<root>/wt/<sha>` (`make linux` inside it) so old commits run through the
  same pipeline.
- `rep-dbg fly` — the run presets for `REPGAME_FLY`:
  `--spec "x,y,z,yaw,pitch[,speed]"` (default `0.5,80,0.5,0,0,8`),
  `--ticks` (FLY_TICKS, default 400), `--settle`, `--settle-timeout`,
  `--fly-ticks`, `--rd "tick:radius,..."`, `--metrics`, `--every`
  (RESULTS_EVERY).
- `rep-dbg walk` — `REPGAME_WALKTEST` preset: `--ticks`, `--teleport`.

All presets accept `--build release|debug|asan|tsan`, `--profile`
(REPGAME_PROFILE=1), `--gdb` and `--every`; `rep-dbg tests` is the odd
one out — see *Sanitizers & crashes*.

### Runs, stats and metrics

- `rep-dbg runs [--name N] [--status S] [--git SHA] [--since 2h] [--last N]
  [--reindex] [--json]`
- `rep-dbg show <id|dir|last> [--json] [--field stats.metric.loader.light_jobs.max]`
- `rep-dbg compare <id> <id> ...` — side-by-side stats table + status.
- `rep-dbg export <ids...> --csv|--json`
- `rep-dbg wt list [--json]` — managed worktrees: git-registered
  worktrees inside `<root>` plus unregistered dirs under `<root>/wt/`
  (orphans). Shows path, head sha, size, last-used, registered, dirty.
- `rep-dbg wt prune [--older-than DAYS (3)] [--all] [--dry-run]` —
  registered → `git worktree remove --force`; orphan dirs → symlink-safe
  rmtree; then `git worktree prune` drops stale registrations. Last-use
  is tracked by a `<root>/wt/.used/<sha>` stamp touched on every
  `--rev`/`worktree:` reuse (dir mtime as fallback). Age-based prune
  keeps *dirty* worktrees that live outside `<root>/wt/` (hand-made ones
  may hold uncommitted work — `rep-dbg: keeping dirty worktree …`);
  `--all` removes them.
- `rep-dbg gc [--dry-run] [--keep-days 7] [--max-gb 10]
  [--perf-data-hours 24] [--wt-days 3] [--json]` — housekeeping for the
  root: deletes kept `perf.data` older than `--perf-data-hours` (marks
  `perf.data_deleted` in result.json), run dirs older than `--keep-days`,
  oldest run dirs over the `--max-gb` total cap (never younger than 1 h),
  and worktrees idle `--wt-days` — then rebuilds the index. Safety: only
  deletes realpaths inside the root, refuses `/`, `$HOME` and the repo,
  and never descends into symlinks. Run-creating commands first call the
  same gc automatically at most once/hour (stamp `<root>/.gc-stamp`);
  defaults overridable via `REP_DBG_KEEP_DAYS`, `REP_DBG_MAX_GB`,
  `REP_DBG_PERF_DATA_HOURS`, `REP_DBG_WT_DAYS`, and `REP_DBG_AUTO_GC=0`
  disables it (one stderr line, only when it removed something).
- `rep-dbg metrics <run|results.jsonl|stdout.log> [--fields a,b] [--csv]
  [--plot a,b] [--after-settle]` — per-field stats table for one run;
  `--csv` dumps flattened per-tick metric rows, `--plot` writes a PNG line
  chart (tick on x, one line per field, legend) into the run dir and
  registers it under `artifacts.plots` in result.json. A bare
  `stdout.log` goes through the log shim. `--after-settle` restricts to
  records after `fly_settled`.
- `rep-dbg ab --cfg NAME=SPEC ... --reps N [scenario|-- <preset args>]
  [--metric FIELD ...] [--name N] [--json]` — interleaved A/B/C reps.
  `SPEC` is `[rev:<ref>|worktree:<path>|current][:K=V[:K=V...]]` — env
  pairs are colon-separated since values (e.g. `REPGAME_FLY`) contain
  commas. E.g. `base=current`, `smooth=current:REPGAME_LIGHT_SMOOTH=1`,
  `old=rev:HEAD~3`. Each distinct tree is built once up front, then reps
  interleave (rep1 A,B,C; rep2 A,B,C …) so drift affects all cfgs
  equally. Each rep is a normal indexed run (`kind:"ab-rep"`, plus
  `ab_id`/`cfg` in result.json). The ab run dir `<root>/ab/<ts>-<n>/`
  holds its own indexed `result.json` (`kind:"ab"` with the cfg
  definitions, rep run ids, and aggregates) and a `summary.md` table
  (rows = metrics, cols = cfgs as `median±stddev`, delta-vs-first-cfg %,
  noisy marker). Status is `failed` if any rep isn't `ok` (the run keeps
  going; see `rep_runs` for which). `--metric` names a result.json
  dotted path; the default set is `wall_s`, `game.settle_tick`,
  `stats.metric.loader.light_jobs.mean`,
  `stats.metric.loader.light_pending.max`,
  `stats.metric.loader.chunks_loading.mean`.

  **Noise rule**: a metric is flagged `"noisy"` when any cfg's
  `stddev/|mean|` exceeds 10 %, or when the spread between the most
  separated cfg medians is smaller than the larger of those two cfgs'
  `max-min` range (i.e. the ranges overlap and the medians don't clear
  the noise). Each metric gets a `verdict`: `"noisy"` (any cfg
  `stddev/|mean|` > 10 %), `"within_noise"` (median gap inside the larger
  max-min spread), else `"clear"`; `summary.md` marks `**noisy**` and
  `(within noise)` accordingly. `--profile` also appends
  `stats.metric.prof.draw_frame.us_per_call.mean` and
  `...tick.us_per_call.mean` to the default metrics.

  Scenario shortcuts (`tools/rep_dbg/scenarios.py` — adding one is a
  single dict entry):

  - `cold-settle` — fly parked at `1000,80,1000`, settle
    `20/6000 ticks`, FLY_TICKS 6000, no flight. The interesting metric
    is `game.settle_tick`.
  - `stream-flight` — same settle, then `REPGAME_FLY_FLY_TICKS=600`
    inside `REPGAME_FLY_TICKS=900` (settle → stream → hover).
  - `idle` — parked flat-world run with `REPGAME_PROFILE=1` and a
    `fly_settled` window (2 s warmup + `window_s` measurement; default
    10 s, override with `ab --duration S`). Default metrics are the
    window fields: `window.fps`, `window.prof.draw_frame.us_per_call`,
    `window.prof.tick.us_per_call`.

  Or pass preset args literally after `--`, e.g.
  `rep-dbg ab --cfg a=current --cfg b=current --reps 2 -- fly --ticks
  3000 --settle 20`. The scenario env is applied first, `--env` pairs
  next, and each cfg's own `K=V` last (it is the variable under test).

## Profile timers (`REPGAME_PROFILE`)

In-game coarse timers complement `perf`: **perf shows where CPU time goes
inside call stacks; profile timers give per-tick subsystem budgets** that
aggregate cleanly and A/B well.

- Add `PROFILE_SCOPE("name")` at the top of a coarse function (tick,
  world draw, chunk mesh, mob tick, block-update drain, light job…).
  It registers a fixed slot once (64 slots, mutex, overflow logs once and
  lands in a shared dummy) and runs an RAII `profile::Scope`.
- When profiling is off, the Scope is one relaxed check of
  `profile::g_enabled` — no clock reads, effectively free. When on
  (`REPGAME_PROFILE=1`, or `--profile` on any rep-dbg preset / all `ab`
  cfgs) it times with `steady_clock` and accumulates relaxed
  `n`/`total_ns`/`max_ns` atomics per slot. Names aggregate across
  threads, so light workers sum into one slot.
- On each metric record `snapshot_and_reset()` swaps the interval
  counters into `"prof":{name:{n,us,max_us}}` — the interval is one
  `REPGAME_RESULTS_EVERY` window (metric to metric), and the stats
  pipeline automatically produces `stats.metric.prof.<name>.us.mean`,
  `...max_us.max`, etc.
- `rep-dbg metrics <run> --fields 'prof.*'` tabulates them;
  `--plot prof.tick.us,prof.world_draw.us` charts a run;
  `rep-dbg ab --profile --metric stats.metric.prof.tick.us.mean ...`
  compares budgets across cfgs.
- Instrumented sites: `profile.hpp`/`game_tick.cpp` ("tick"),
  `game_render.cpp` ("draw_frame"), `world.cpp` ("world_draw"),
  `light.cpp` ("light_bfs" — per-column BFS drain on the light workers, "light_finalize"),
  `terrain_loading_thread.cpp` ("terrain_job"), `chunk.cpp`
  ("chunk_mesh"), `mobs.cpp` ("mobs_tick"), `player.cpp`
  ("player_movement", "block_updates"). Implementation:
  `src/common/utils/profile.cpp`.

Cost model: off is a single bool load per scope; on is two steady_clock
reads + three relaxed atomics per scope — negligible at call-site
granularity, which is why scopes belong on coarse functions, never on
per-block inner loops.

## Profiling (`perf`)

Preflight (`tools/rep_dbg/perf.py`) refuses with exit 4 when
`kernel.perf_event_paranoid > 1` — record/stat need:

```sh
sudo sysctl kernel.perf_event_paranoid=1                                   # until reboot
echo 'kernel.perf_event_paranoid=1' | sudo tee /etc/sysctl.d/99-perf.conf  # persistent
```

`kernel.kptr_restrict != 0` only warns (kernel symbols resolve as
`[unknown]`; game code is unaffected). `rep-dbg` never invokes sudo.

Commands:

- `rep-dbg perf record [--freq 499] [--call-graph dwarf|fp|lbr]
  [--stack 8192] [--threads] [--duration S]
  [cold-settle|stream-flight|-- <preset args>]` — a normal preset run
  (kind `"perf"`) wrapped in `perf record`; `--duration` is the run
  timeout, and a timeout SIGINTs perf so `perf.data` is finalized.
- `rep-dbg perf idle [--duration 8] [--warmup 2] [--spec
  0.5,80,0.5,0,0] [--settle 20] [--settle-timeout 6000] [--no-profile]`
  — one command for "where does a settled, stationary game spend CPU":
  a parked flat/`-flat` run (fly speed forced to 0, never exits),
  `REPGAME_PROFILE=1`, `perf record -p` attached for `--duration`
  seconds once `fly_settled` + `--warmup` have passed, then SIGTERM.
  `result.json.window` holds fps + per-scope `us_per_call` for the
  measured window.
- `rep-dbg perf attach <pid|name> [--duration 15]` — `perf record -p` a
  running game (name resolved via `pgrep -x`; errors on 0 or >1 match).
- `rep-dbg perf stat <scenario args> [--events ...]` — `perf stat -x,` with
  the default event set (cycles, instructions, caches, branches,
  context-switches, task-clock); parsed into `result.json.perf.stat` with
  `ipc`, `cache_miss_rate` derived, `<not supported>` rows as null.
- `rep-dbg ab --perf-stat` — runs every rep under `perf stat`, so
  `perf.stat.*` works as `--metric` fields.
- `rep-dbg perf diff <runA> <runB> [--top N]` — biggest self% deltas per
  symbol between two record runs. When both runs carry
  `perf.samples`/`freq`/`duration_s` (and `window.frames`) the table
  adds a normalised column — `us/frame` preferred, else `ms CPU/s` —
  and sorts by it; symbols whose share rose but whose normalised cost
  moved < 5 % are tagged `share-only` (share inflation, not real
  growth). Old runs without those fields fall back to self% only.

Outputs in the run dir (registered under `artifacts.perf`): `top.txt`
(top 40 by self time plus a per-thread `comm,sym` section; parsed to
`perf.top`), `folded.txt` and `flame.svg` (collapsed `perf script`
stacks → icicle SVG with hover titles; `--threads` adds
`flame-<comm>.svg`). `perf.data` is **deleted after postprocess**
(`perf.data_size_bytes` + `perf.data_deleted` recorded in result.json,
`artifacts.perf.data` removed) unless `--keep-data`; `perf diff` only
needs `perf.top`. `perf.samples` (total folded-stack weight), `freq`,
`duration_s` and `frames` are kept for diff normalisation.

Call-graph modes: `dwarf` works on any binary but copies `--stack` bytes
(default 8 KB) of stack per sample (big perf.data — lower `--freq` or
bound `--duration`); `fp`
needs a frame-pointer build (`make linux PERF=1`, which adds
`-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer` via
`REPGAME_FRAME_POINTERS`; default builds are unaffected and `record
--call-graph fp` prints a reminder); `lbr` is cheap but only on Intel
with a shallow stack depth.

Recipes:

- Interactive attach: `make linux-run` in one terminal,
  `rep-dbg perf attach RepGame --duration 20` in another.
- Compare two records: `rep-dbg perf diff perf/<idA> perf/<idB> --top 20`.

## Capturing and diffing

`rep-dbg shot "<F12 spec>"` wraps `REPGAME_CAPTURE` (press F12 in-game to
get the spec string; format `x,y,z,yaw,pitch[,flags];...` — see the capture
skill for the per-view debug-flag bit table and the settle/view-ticks env
vars):

- `rep-dbg shot "<spec>" [--quality 0..3] [--time T] [--flags F]
  [--settle N] [--timeout T] [--view-ticks N] [--flat] [--rev R]` — kind
  `"shot"`; every `capture_<i>_display.tga` is converted to PNG (TGAs
  kept), and `artifacts.screenshots` lists `{view,file,tga,flags,timeout}`
  joined from the `capture_shot` events. Display PNG paths print to
  stderr.
- `rep-dbg shot <spec> --flags-sweep` — one run, baseline + one view per
  debug-flag bit via the per-view flag field; `sweep.png` is a labelled
  contact sheet (`artifacts.sweep`) for bisecting which subsystem changed
  a frame.
- `rep-dbg shot <spec> --whip N --step DEG [--view-ticks T]` — N views at
  yaw+i·step with `REPGAME_CAPTURE_VIEW_TICKS` (default 2) so transients
  show; contact sheet `whip.png`.
- `rep-dbg shot <spec> --revs a,b[,c]` — the same view per rev (`current`
  = working tree, others via `--rev` worktrees); the parent run dir
  `<root>/shot-revs/<ts>-<n>/` (kind `"shot-revs"`, indexed) holds
  `side.png` (labelled side-by-side) and `diffs` per rev vs the first —
  with the view-0 rerun recheck on by default (`--no-rerun` to disable).

`rep-dbg diff <a> <b> [--threshold 8] [--out DIR] [--json] [--rerun]` —
a/b are image files (png/tga) or shot run ids/dirs (display shots paired
by view index). Per pair: `changed_px`, `changed_frac`, `max_delta`,
`mean_delta`, `bbox`, `stable`, plus `heat.png` (amplified delta over
dimmed grayscale of a) and `side.png` (a | b | heat) in the out dir
(default: an indexed `diff/` run dir). Exit 1 when any pair differs above
threshold, 0 when identical, 4 on usage error or size mismatch.

**Bistable caveat**: `capture_0` is bistable run-to-run, so a run-vs-run
view-0 diff should be rechecked once before being called a regression.
`--rerun` re-executes both runs with their recorded `cmd`/`env` in fresh
run dirs and re-diffs view 0; `stable:false` means the recheck disagreed
with the first diff (one zero, one non-zero) and a "bistable capture_0"
note prints. `stable` is null when no rerun happened. Diffs are always
run-vs-run or rev-vs-rev — no checked-in baseline images.

Recipes:

- "User reports a visual bug with an F12 string": `rep-dbg shot "<f12>"`
  → look at the PNG, then `--flags-sweep` to bisect the subsystem.
- "Did my change alter rendering?": `rep-dbg shot "<f12>" --revs master,current`.

## Scripted scenarios

`rep-dbg script <file.rds> [--world W] [--keep-world D] [--rev R] [--window]
[--keep] [--timeout S] [--json]` runs a scripted scenario — a plain-text
file of debug commands, one per line — as a normal indexed run with
`kind:"script"`. Scenarios assert **state** (blocks, light, mob counts),
never images.

Each tick the game executes lines in order until one blocks; blocking
commands resume on later ticks. `#` starts a comment. `#!` lines are
headers read by rep-dbg, not the game:

- `#! world <name>` — world name (default is a generated flat world)
- `#! keep-world <path>` — reuse an existing world directory
- `#! terrain` — normal terrain instead of the flat world
- `#! env K=V` — extra env var (repeatable)
- `#! timeout S` — run timeout

Commands (coordinates are whitespace-separated ints unless noted):

| Command | Effect |
| --- | --- |
| `wait <ticks>` | block for n ticks |
| `wait_idle [streak=20] [timeout=6000]` | block until the chunk loader is idle `streak` ticks in a row; on timeout emits `wait_timeout` + a failed assert, then continues |
| `camera x y z yaw pitch` / `release` | pin / unpin the camera (floats; same path as a capture view) |
| `screenshot <name>` | block until the frame is drawn; writes `screenshots/script_<name>_display.tga` (→ PNG + `artifacts.screenshots`) and emits a `screenshot` event |
| `quality low|medium|high|x_high`, `flags <u_DebugFlags int>`, `rd <n>` | same mechanisms as the capture views |
| `time <ticks>`, `mode survival|creative`, `peaceful on|off`, `fly on|off` | world time / game mode / hostile-mob targeting / `input.player_flying` (flat worlds spawn flying) |
| `setblock x y z <block> [rotation]`, `fill x1 y1 z1 x2 y2 z2 <block>` | normal player edit; setblock takes an optional BlockState rotation; fill is inclusive, max 65536 cells |
| `give <item> [count]`, `slot N` | hotbar add + slot select, refreshing the held-item overlay |
| `damage <hp>`, `eat`, `respawn` | player vitals (docs/player.md); each emits a `*_result` event with `applied`/`health`/`dead` |
| `sleep x y z` | `repgame_try_sleep` on the bed cell; emits `sleep_result {slept,time}` |
| `inventory <0|1>` | toggle `input.inventory_open` (shows the survival grid + 2x2 crafting UI in survival) |
| `menu <0|1>`, `search <0|1>` | toggle `input.menu_open` / `input.search_focused` (pause menu, creative search bar) |
| `container chest\|furnace\|table x y z`, `container none` | open the container UI at the block as a right-click would (offline path; the block must be placed already — `setblock` lands on the next tick), or close any open container screen |
| `uistate` | emit a `uistate` event: the pass's `overflow` mask (`UI_OVERFLOW_*` — screens exceeding the viewport), layout rects + each container panel's applied items-slab position (centered y-up UI coords) |
| `goto x y z`, `drop`, `mob`, `fall`, `tnt` | same args as the mp-test commands (shared `dbgcmd::` implementations) |
| `cart x y z [vx vz]` | spawn a minecart at the position (snaps onto a rail if the cell has one); optional initial velocity in blocks/tick |
| `carts` | emit a `cart_state` event per cart: `id`, `pos`, `vel`, `on_rail`, `rider` |
| `ride` | mount the nearest cart within 4 blocks of the player (error when none) |
| `dismount` | leave the ridden cart (no-op when not riding) |
| `ride_state` | emit a `ride_state` event: `riding_cart` (0 = not riding) and the camera `pos` |
| `fake_rider` | spawn a remote-player avatar (`MobManager::spawn_any` — the multiplayer path) and seat it in the cart nearest the camera through the same `set_remote_seated` code remote riders use; emits a `fake_rider` event `{uid,cart_id}` |
| `feed x y z [item]` | feeds the mob nearest the point (default: the type's first breed food); mirrors route ENTITY_ACTION like a right-click |
| `shear x y z` | shears the shearable mob nearest the point (as if holding shears); mirrors route ENTITY_ACTION like a right-click |
| `probe x y z`, `probe_light x y z` | emit a results `probe` record (name + sky/block light) |
| `assert_block x y z <name|id>` | assert the loaded block |
| `assert_light x y z sky|block <op> <n>` | op is `== != < <= > >=` |
| `assert_mob_count <op> <n> [type]` | count mobs, optionally filtered by type |
| `assert_health <op> <hp>`, `assert_dead <op> <0|1>` | player vitals |
| `echo <text...>` | `echo` event |
| `dump` | force a metric record now |
| `quit` | exit now (else the script exits at EOF) |

Assert records carry readable `expected`/`actual` (e.g. `">= 12"`/`"9"`).
Unknown commands and bad args emit `cmd_error` **and** count as failed
asserts — scripts fail loudly. Asserts on unloaded chunks fail with actual
`"unloaded"`: always `wait_idle` after edits before asserting. At EOF the
run exits with `exit_reason:"script_done"` unless `REPGAME_SCRIPT_KEEP=1`
(`--keep`), which stays up with the camera released — useful with
`--window`. Script mode does not write `player.dat`.

Pacing caveat: scripted runs draw far fewer frames per second than live
play (waits are tick-driven and each screenshot stalls the loop), so
per-frame visuals — particles, animations — can behave or appear
differently than at a live ~240 fps. A passing scripted screenshot does
not prove the effect shows in normal play; when a repro is visual, keep a
live run or a denser `wait`/`screenshot` cadence as the control.

rep-dbg prints each failed assert as `file:line: cmd — expected X, actual
Y` and exits 1. `make scenarios` runs every `tests/scenarios/*.rds`
(`SCENARIOS="substr"` filters) — it's part of `make test-all`.

Bug report → scenario recipe: reproduce the setup with `setblock`/`fill`/
`mob` lines, `wait_idle`, then `assert_*` what the user observed; commit
the `.rds` under `tests/scenarios/` so `make scenarios` keeps it green.

## Inspecting saved worlds (`inspect`)

`rep-dbg inspect <target> <sub> ...` runs the offline world inspector —
the game binary's `inspect` subcommand (dispatched before SDL, no GL),
which reads the same save files `MapStorage` writes, strictly read-only.
`<target>` is a world name (under `$HOME/.repgame`), a path to a world
dir, a run id, a run dir, or `last`; runs resolve to
`<run>/home/.repgame/<world>` via the world in `result.json.cmd`. It
creates no run and touches no index.

Subcommands (default human text, `--json` for one JSON object):

- `block x y z` — `{pos, chunk, local, id, name, rotation, redstone_power,
  saved, edited}`. Values come from the real pipeline (natural generation
  plus saved edits — `Chunk::inspect_load`), so `saved:false` means no
  chunk file but `id`/`name` still show generated terrain; `edited` is
  true when the cell is one of the chunk file's saved edit cells.
- `slice y x1 z1 x2 z2 [--png out.png]` — an ASCII grid of one layer +
  char→name legend, max 256×256. With `--png`, rep-dbg renders it with
  PIL (one stable colour per block name, 8×, legend strip).
- `column x z [--y0 a --y1 b]` — vertical run-length-encoded column.
- `chunk cx cy cz` — file size, non-air count, edited-cell count, top-20
  block counts.
- `entities [--near x y z --radius r]` — chests, mobs and dropped items
  from `chest_*`/`mobs_*`/`items_*` files via the real loaders.
- `player` — `player.dat` (pos/angles/mode/inventories), `world.dat`,
  `world_type`.

Light is not persisted (it derives from blocks) — use `probe_light` in a
rep-dbg script to inspect light in a live world.

## Sanitizers & crashes

`--build asan` and `--build tsan` select dedicated build variants
(`make linux_asan` / `make linux_tsan`, binaries at
`out/linux/asan/RepGame` and `out/linux/tsan/RepGame`). ASan builds also
carry UBSan (`-fsanitize=address,undefined`, recovering —
`halt_on_error=0`, one report per source location); both use `-O1 -g
-fno-omit-frame-pointer`, no LTO, and drop the static
libstdc++/libgcc link so the sanitizer runtimes work.

The runner sets the right env per variant:

- asan: `ASAN_OPTIONS=log_path=<run>/asan:detect_leaks=1:abort_on_error=0:halt_on_error=0`,
  `LSAN_OPTIONS=suppressions=tools/sanitizers/lsan.supp`,
  `UBSAN_OPTIONS=print_stacktrace=1:log_path=<run>/ubsan`
- tsan: `TSAN_OPTIONS=log_path=<run>/tsan:suppressions=tools/sanitizers/tsan.supp:second_deadlock_stack=1`

Reports land as `<run>/asan.<pid>` / `tsan.<pid>` / `ubsan.<pid>`, are
listed in `result.json.artifacts.sanitizer`, and are summarized in
`result.json.sanitizer = {asan_errors, ubsan_errors, tsan_warnings,
leaks, top: [{kind, summary_line, first_repgame_frame}]}` — the first
frame pointing into `src/`/`include/` is what to paste in a bug. Any
unsuppressed report marks the run `failed`.

**Suppression policy:** `tools/sanitizers/*.supp` exists for third-party
noise only (Mesa/llvmpipe, SDL, imgui, libasound), one comment per entry.
Never suppress RepGame frames — report them instead. TSan on GL drivers
is noisy: races inside the driver threads are third-party and
suppressible; races between `loader`/`light_worker` and the main thread
are real and should be reported verbatim.

**`rep-dbg tests --build asan|tsan|debug [filters]`** runs `RepGame
--tests` as an indexed `tests` run under the same sanitizer env. The test
binary has no results stream, so `result.json.tests = {passed, failed,
failed_names}` is parsed from its stdout — the only log parse rep-dbg
performs on output. Status is `failed` on test failures or reports.

**`--gdb`** on `run`/`fly`/`walk` wraps the game in batch gdb:
`gdb -q -batch -nx -ex "run > stdout.log" -ex "thread apply all bt full"`.
The inferior's output still lands in `stdout.log`; gdb's own output —
the signal line and every thread's full backtrace — lands in
`<run>/crash.txt`. On a crash `result.json.crash = {signal, thread,
top_frames}` (first ~8 frames of the crashing thread as
`"func (file:line)"`) and status is `crashed`; a normal exit records
`{signal: null, note: "no_crash"}` instead of misleading noise. On a
timeout the runner sends SIGINT, which batch gdb treats as an interrupt:
the queued `bt full` still runs, so even a hung run leaves an
all-thread backtrace before gdb quits and kills the inferior.

**Self-test hook:** `REPGAME_DEBUG_CRASH=segv|abort[@tick]` makes
`RepGame::tick` crash deliberately (`segv` writes through a null
pointer, `abort` calls `abort()`) at the given tick (default 0). Use it
to verify `--gdb` and sanitizer report collection.

## Recipes

- Headless crash triage: `rep-dbg fly --gdb --env REPGAME_DEBUG_CRASH=segv@60`,
  then read `result.json.crash` and `<run>/crash.txt`.
- Suspected light/loader race: `rep-dbg tests --build tsan light` or a
  full `rep-dbg fly --build tsan`; findings land in `result.json.sanitizer`.
- "User says block X at x,y,z is wrong": `rep-dbg inspect World1 block x y z`;
  surrounding layout via `rep-dbg inspect World1 slice y x-10 z-10 x+10 z+10 --png out.png`.
- Inspect a scenario's world afterwards: `rep-dbg inspect last block 10 1 10`
  (run dirs keep their isolated `home/`).
- Reproduce an F12 view: `rep-dbg run --env REPGAME_CAPTURE="<spec>" --flat`
- Bisect a regression: `rep-dbg fly --rev <sha> --ticks 400` (old revs get
  a `log-shim` result.json).
- Answer a one-off question: `rep-dbg show last --field
  stats.metric.loader.light_jobs.mean`, or `results.load("last")` in
  Python.
- A/B an env toggle vs master:
  `rep-dbg ab --cfg master=rev:master --cfg mine=current --reps 3 stream-flight`
- Chart a run: `rep-dbg metrics last --plot loader.light_jobs,loader.chunks_loading`
- Per-subsystem tick budgets: `rep-dbg fly --profile --json` →
  `stats.metric.prof.*`; A/B them with `rep-dbg ab --profile --metric
  stats.metric.prof.tick.us.mean`.
- Measure profiler overhead itself: `rep-dbg ab --cfg off=current --cfg
  on=current:REPGAME_PROFILE=1 --reps 3 stream-flight`.

## Don't

- Don't write one-off `/tmp` shell scripts that set `SDL_VIDEODRIVER` and
  hand-delete worlds — `rep-dbg run` already does isolation, timeouts and
  interleaving.
- Don't regex `stdout.log` — the field you want is already in
  `result.json`; if it isn't, add a record to `results.cpp` (the log
  shim exists only for old revs).
- Don't use checked-in image baselines — diffs are always run-vs-run or
  rev-vs-rev (`rep-dbg diff`, `shot --revs`).
- Don't drop ad-hoc debug scratch (screenshots, `.rds` scripts, patches,
  logs, temp homes) loose in `/tmp` — use `/tmp/repgame/`; it's tmpfs
  and clears on reboot. Persistent run output lives under
  `~/.repgame/rep-dbg`.
- Don't `sudo` anything — the only root knob is the perf sysctl, and the
  preflight tells you exactly what to ask the user to run.
- Don't `rm` worlds under `~/.repgame` — rep-dbg runs are isolated
  copies; reuse a save via `--keep-world`, never mutate the real one.
