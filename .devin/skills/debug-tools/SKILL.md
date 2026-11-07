---
name: debug-tools
description: Structured debug/profiling runs for RepGame via tools/rep-dbg — indexed runs with a result.json + results.jsonl event stream. Use for profiling (perf or REPGAME_PROFILE timers), screenshots and visual-bug diffs, reproducing bug reports, block/light debugging, crashes and hangs, sanitizer runs (asan/tsan), A/B comparisons across revs or env toggles, and stress-testing loading/lighting — without regexing logs.
---

# rep-dbg

`tools/rep-dbg` runs the game headless with an isolated `$HOME`, records a
structured `results.jsonl` (`REPGAME_RESULTS`), and assembles a
schema-versioned `result.json` per run under `/tmp/rep-dbg` (`--root` /
`REP_DBG_ROOT` override). Full reference: `docs/debug-tools.md`.

**Rule: never regex logs.** Read `result.json` via `rep-dbg show --field`
or `rep_dbg.results` — the fields you want already exist. Log text stays
for humans (`stdout.log`); the only sanctioned scraping is
`log_flags` (sanitizer/GL/timeout counters).

## Symptom → command

| Symptom / question | Command |
|---|---|
| Did the flight/light-queues behave? | `rep-dbg fly --flat --ticks 400` then `rep-dbg show last` |
| Chunks still loading late in flight? | `rep-dbg show last --field stats.metric.loader.chunks_loading_after_settle.mean` |
| When did it settle? | `rep-dbg show last --field game.settle_tick` |
| Which tick did the capture shoot? | `rep-dbg show last --field game.shots` / `r.events("capture_shot")` |
| Regression vs another rev | `rep-dbg fly --rev <sha> ...` + `rep-dbg compare <idA> <idB>` |
| Crash / hang | `rep-dbg fly --gdb ...` → `crash.txt` all-thread bt + `result.json.crash` (`REPGAME_DEBUG_CRASH=segv@60` self-test hook); `--timeout S` alone gives `status=timeout` |
| ASan/UBSan findings | `rep-dbg tests --build asan` or `rep-dbg fly --build asan` → `result.json.sanitizer`, reports as `asan.*`/`ubsan.*` in the run dir |
| Data races | `rep-dbg tests --build tsan light` / `rep-dbg fly --build tsan` → `tsan.*` reports; suppressions in `tools/sanitizers/` (third-party noise only, never RepGame frames) |
| rep_tests as a run | `rep-dbg tests --build debug|asan|tsan [filters]` → `result.json.tests {passed,failed,failed_names}` |
| A/B an env toggle or branch | `rep-dbg ab --cfg a=rev:master --cfg b=current:REPGAME_LIGHT_SMOOTH=1 --reps 3 stream-flight` (scenarios: `cold-settle`, `stream-flight`, or preset args after `--`) |
| Metric table / CSV / chart | `rep-dbg metrics <run> [--csv] [--plot loader.light_jobs,...] [--after-settle]` |
| Where is CPU time going? | `rep-dbg perf record stream-flight` → `top.txt`, `flame.svg` in the run dir (needs `kernel.perf_event_paranoid<=1`; `--call-graph fp` wants `make linux PERF=1`) |
| Where does an idle game spend CPU? | `rep-dbg perf idle --flat --duration 8` — parked settled run, perf attach + `result.json.window` (fps, `prof.<scope>.us_per_call`); `perf diff` two idle runs for normalised `us/frame` deltas (`share-only` flags share inflation) |
| Per-tick subsystem budgets | `rep-dbg fly --profile` (`REPGAME_PROFILE=1`) → `stats.metric.prof.<name>.{n,us,max_us}` + derived `us_per_call`; `metrics <run> --fields 'prof.*'` / `--plot prof.tick.us`; `ab --profile` to A/B budgets |
| Profile a running game | `rep-dbg perf attach RepGame --duration 20` |
| A/B frame cost | `rep-dbg ab --cfg base=rev:A --cfg fix=rev:B --reps 3 --flat idle` — windowed: `window.fps`, `window.prof.*.us_per_call` (`--duration S` resizes) |
| Reproduce an F12 screenshot | `rep-dbg shot "<f12 spec>"` → PNGs in `artifacts.screenshots` |
| Bisect which subsystem changed a frame | `rep-dbg shot "<f12>" --flags-sweep` → `sweep.png` |
| Did my change alter rendering | `rep-dbg shot "<f12>" --revs master,current`; `rep-dbg diff <a> <b>` (view-0 diffs are bistable — `--rerun` rechecks) |
| Assert world state from a script | `rep-dbg script <file.rds>` (one cmd/line: wait_idle, setblock, assert_block/assert_light/...); seed scenarios in `tests/scenarios/`, run all with `make scenarios` |
| What's actually saved on disk | `rep-dbg inspect <world|run|last> block|slice|column|chunk|entities|player` — read-only; `slice --png` renders with PIL; `saved:false` = generated terrain, not stored |
| IPC / cache misses | `rep-dbg perf stat stream-flight`, or `rep-dbg ab --perf-stat --metric perf.stat.ipc ...` |
| List / export runs | `rep-dbg runs [--status failed] [--last N]`, `rep-dbg export <ids...> --csv` |
| Old worktrees piling up | `rep-dbg wt list` / `rep-dbg wt prune [--older-than D] [--all] [--dry-run]` |
| /tmp filling up | `rep-dbg gc [--dry-run]` — old run dirs, stale perf.data, size cap, idle worktrees; auto-runs hourly (REP_DBG_AUTO_GC=0 off) |

## Conventions

- `--json` on every subcommand prints exactly one JSON object; progress is
  on stderr, last line `rep-dbg: <status> <run-dir>`.
- Exit codes: 0 ok, 1 asserts failed, 2 crashed/timeout, 3 build failed,
  4 usage error.
- Run dir: `<root>/<name>/<YYYYmmdd-HHMMSS>-<n>/` with `result.json`,
  `results.jsonl`, `stdout.log`, `home/` (isolated `$HOME` — safe to run
  copies in parallel, even same world name), `screenshots/`.
- `--rev <ref>` builds `<root>/wt/<sha>` once (`imgui_build`/`entt_build`
  symlinked in); pre-results revs get `"source":"log-shim"` results built
  from FLYMET log lines — the stats/compare pipeline still works.

## Python API

```python
from rep_dbg import results
r = results.load("last")                  # or run id / dir
r.stats["metric.loader.light_jobs"]["max"]
r.metrics(after_settle=True); r.events("capture_shot"); r.probes()
results.query(name="fly", status="ok")
```

## Preflight & caveats

- perf: `kernel.perf_event_paranoid > 1` refuses with exit 4 — agents
  can't `sudo`; ask the user to run the sysctl line printed by
  `docs/debug-tools.md`. `perf.data` is deleted after postprocess unless
  `--keep-data` (diff only needs `perf.top`); defaults `--freq 499`,
  dwarf stack 8 KB (`--stack`).
- A/B metrics carry a `verdict`: `noisy` / `within_noise` / `clear` —
  don't read small deltas on `noisy`/`within_noise` rows as regressions.
- `capture_0` flat captures are bistable — rerun a view-0 diff once
  (`rep-dbg diff --rerun`) before calling it a regression.
- Sanitizer known findings as of commit ab63d8c (verify before
  assuming): UBSan misaligned `unsigned int` load
  (`map_storage.cpp:490` in `load_blocks_decode`), UBSan invalid
  `TranslucentBlend` sentinel (`chunk_loader.cpp:44`), exit-time
  chunk/light leaks (~951 MB: `calculate_populated_blocks`,
  `load_terrain`, `light_fill_chunk`), TSan `light_dbg_*` counter races
  in `light.cpp`. RepGame findings are reported, never suppressed.

## Statuses

`ok` (summary record present, asserts clean), `failed` (asserts failed or
sanitizer/GL log flags), `crashed` (no summary — died abnormally),
`timeout` (rep-dbg killed it; a SIGTERM clean exit still yields
`exit_reason=signal`), `build_failed`.
