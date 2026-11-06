# Debug tooling (`tools/rep-dbg`)

`tools/rep-dbg` is the CLI for scripted debugging/profiling runs: the game
writes a structured JSONL event stream (`REPGAME_RESULTS`), every run
produces a schema-versioned `result.json`, and runs are indexed and
queryable. Nobody parses log text — results are structured at the source;
log parsing exists only as a compatibility shim for old revs.

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

`stats` aggregates every numeric leaf of every metric record automatically;
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
`rep-dbg runs --reindex`. `<root>` defaults to `/tmp/rep-dbg`;
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

## Commands

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
- `rep-dbg runs [--name N] [--status S] [--git SHA] [--since 2h] [--last N]
  [--reindex] [--json]`
- `rep-dbg show <id|dir|last> [--json] [--field stats.metric.loader.light_jobs.max]`
- `rep-dbg compare <id> <id> ...` — side-by-side stats table + status.
- `rep-dbg export <ids...> --csv|--json`
- `rep-dbg wt list|prune` — list/remove `--rev` worktrees (only ever
  touches `<root>/wt/`).

## Recipes

- Reproduce an F12 view: `rep-dbg run --env REPGAME_CAPTURE="<spec>" --flat`
- Bisect a regression: `rep-dbg fly --rev <sha> --ticks 400` (old revs get
  a `log-shim` result.json).
- Answer a one-off question: `rep-dbg show last --field
  stats.metric.loader.light_jobs.mean`, or `results.load("last")` in
  Python.
