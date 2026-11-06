---
name: debug-tools
description: Structured debug/profiling runs for RepGame via tools/rep-dbg — indexed runs with a result.json + results.jsonl event stream (metrics, events, asserts). Use when stress-testing loading/lighting, bisecting with --rev, or answering "what did the loader/light queues do" questions without regexing logs.
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
| Crash / hang | run with `--timeout S` → `status` is `crashed`/`timeout`, see `stdout.log` |
| A/B an env toggle or branch | `rep-dbg ab --cfg a=rev:master --cfg b=current:REPGAME_LIGHT_SMOOTH=1 --reps 3 stream-flight` (scenarios: `cold-settle`, `stream-flight`, or preset args after `--`) |
| Metric table / CSV / chart | `rep-dbg metrics <run> [--csv] [--plot loader.light_jobs,...] [--after-settle]` |
| Where is CPU time going? | `rep-dbg perf record stream-flight` → `top.txt`, `flame.svg` in the run dir (needs `kernel.perf_event_paranoid<=1`; `--call-graph fp` wants `make linux PERF=1`) |
| Profile a running game | `rep-dbg perf attach RepGame --duration 20` |
| IPC / cache misses | `rep-dbg perf stat stream-flight`, or `rep-dbg ab --perf-stat --metric perf.stat.ipc ...` |
| List / export runs | `rep-dbg runs [--status failed] [--last N]`, `rep-dbg export <ids...> --csv` |
| Old worktrees piling up | `rep-dbg wt list` / `rep-dbg wt prune` |

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

## Statuses

`ok` (summary record present, asserts clean), `failed` (asserts failed or
sanitizer/GL log flags), `crashed` (no summary — died abnormally),
`timeout` (rep-dbg killed it; a SIGTERM clean exit still yields
`exit_reason=signal`), `build_failed`.
