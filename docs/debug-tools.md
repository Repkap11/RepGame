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
  the noise).

  Scenario shortcuts (`tools/rep_dbg/scenarios.py` — adding one is a
  single dict entry):

  - `cold-settle` — fly parked at `1000,80,1000`, settle
    `20/6000 ticks`, FLY_TICKS 6000, no flight. The interesting metric
    is `game.settle_tick`.
  - `stream-flight` — same settle, then `REPGAME_FLY_FLY_TICKS=600`
    inside `REPGAME_FLY_TICKS=900` (settle → stream → hover).

  Or pass preset args literally after `--`, e.g.
  `rep-dbg ab --cfg a=current --cfg b=current --reps 2 -- fly --ticks
  3000 --settle 20`. The scenario env is applied first, `--env` pairs
  next, and each cfg's own `K=V` last (it is the variable under test).

## Recipes

- Reproduce an F12 view: `rep-dbg run --env REPGAME_CAPTURE="<spec>" --flat`
- Bisect a regression: `rep-dbg fly --rev <sha> --ticks 400` (old revs get
  a `log-shim` result.json).
- Answer a one-off question: `rep-dbg show last --field
  stats.metric.loader.light_jobs.mean`, or `results.load("last")` in
  Python.
- A/B an env toggle vs master:
  `rep-dbg ab --cfg master=rev:master --cfg mine=current --reps 3 stream-flight`
- Chart a run: `rep-dbg metrics last --plot loader.light_jobs,loader.chunks_loading`
