"""rep-dbg CLI — see docs/debug-tools.md.

Conventions: every subcommand accepts --json and then prints exactly one
JSON object on stdout; progress goes to stderr. Exit codes: 0 ok,
1 asserts failed, 2 crashed/timeout, 3 build failed, 4 usage error. The
last stderr line of run-producing subcommands is
`rep-dbg: <status> <run-dir>`.
"""

import argparse
import csv
import datetime
import json
import os
import signal
import sys
import time

from . import build as build_mod
from . import index as index_mod
from . import results as results_mod
from . import runner as runner_mod
from . import stats as stats_mod
from . import worktree as worktree_mod


def _add_run_args(p):
    p.add_argument("--build", default="release", choices=["release", "debug", "asan", "tsan"])
    p.add_argument("--rev", help="git ref to build+run from a /tmp worktree")
    p.add_argument("--world", default="World1", help="world name under the isolated $HOME")
    p.add_argument("--flat", action="store_true", help="mark the world flat (world_type)")
    p.add_argument("--keep-world", help="copy this save dir into the isolated home")
    p.add_argument("--env", action="append", metavar="K=V", help="extra env vars")
    p.add_argument("--timeout", type=float, default=None, help="kill the game after N seconds")
    p.add_argument("--name", help="run name (run dir <root>/<name>/<ts>-<n>)")
    p.add_argument("--window", action="store_true", help="run with a real window (not headless)")
    p.add_argument("--json", action="store_true", help="print result.json on stdout")
    p.add_argument("--root", default=None, help="run root (default /tmp/rep-dbg or REP_DBG_ROOT)")
    p.add_argument("--every", type=int, default=None, help="REPGAME_RESULTS_EVERY cadence")


def _parser():
    ap = argparse.ArgumentParser(prog="rep-dbg")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("run", help="run the game with arbitrary env")
    _add_run_args(p)
    p.set_defaults(kind="run", name="run")

    p = sub.add_parser("fly", help="scripted no-clip flight (REPGAME_FLY preset)")
    _add_run_args(p)
    p.add_argument("--spec", default="0.5,80,0.5,0,0,8",
                   help="REPGAME_FLY 'x,y,z,yaw,pitch[,speed]'")
    p.add_argument("--ticks", type=int, default=400, help="REPGAME_FLY_TICKS")
    p.add_argument("--settle", type=int, default=None, help="REPGAME_FLY_SETTLE")
    p.add_argument("--settle-timeout", type=int, default=None, help="REPGAME_FLY_SETTLE_TIMEOUT")
    p.add_argument("--fly-ticks", type=int, default=None, help="REPGAME_FLY_FLY_TICKS")
    p.add_argument("--rd", default=None, help="REPGAME_FLY_RD 'tick:radius,...'")
    p.add_argument("--metrics", type=int, default=None, help="REPGAME_FLY_METRICS")
    p.set_defaults(kind="fly", name="fly")

    p = sub.add_parser("walk", help="scripted walk (REPGAME_WALKTEST preset)")
    _add_run_args(p)
    p.add_argument("--ticks", type=int, default=400, help="REPGAME_WALKTEST_TICKS")
    p.add_argument("--teleport", default=None, help="REPGAME_TELEPORT spec")
    p.set_defaults(kind="walk", name="walk")

    p = sub.add_parser("ab", help="interleaved A/B reps across cfgs")
    p.add_argument("scenario", nargs="?",
                   help="scenario shortcut (cold-settle|stream-flight); or pass preset args after --")
    p.add_argument("--cfg", action="append", required=True, metavar="NAME=SPEC",
                   help="SPEC: [rev:<ref>|worktree:<path>|current][:K=V[:K=V...]]")
    p.add_argument("--reps", type=int, default=3)
    p.add_argument("--metric", action="append",
                   help="result.json dotted field to aggregate (repeatable)")
    p.add_argument("--name", default="ab")
    p.add_argument("--build", default="release", choices=["release", "debug", "asan", "tsan"])
    p.add_argument("--world", default="World1")
    p.add_argument("--flat", action="store_true")
    p.add_argument("--keep-world")
    p.add_argument("--env", action="append", metavar="K=V")
    p.add_argument("--timeout", type=float)
    p.add_argument("--window", action="store_true")
    p.add_argument("--every", type=int, default=None)
    p.add_argument("--perf-stat", action="store_true",
                   help="run each rep under perf stat (perf.stat.* metrics)")
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("perf", help="perf record/attach/stat/diff")
    p.add_argument("action", choices=["record", "attach", "stat", "diff"])
    p.add_argument("args", nargs="*",
                   help="record/stat: [scenario]; attach: <pid|name>; diff: <runA> <runB>")
    p.add_argument("--freq", type=int, default=999)
    p.add_argument("--call-graph", default="dwarf", choices=["dwarf", "fp", "lbr"],
                   help="fp needs `make linux PERF=1`; dwarf bounds at 16KB stacks")
    p.add_argument("--duration", type=float,
                   help="record/stat: the run timeout; attach: seconds to record (default 15)")
    p.add_argument("--threads", action="store_true", help="per-thread flame-<comm>.svg")
    p.add_argument("--events", help="perf stat -e event list")
    p.add_argument("--top", type=int, default=15, help="diff: rows to print")
    p.add_argument("--build", default="release", choices=["release", "debug", "asan", "tsan"])
    p.add_argument("--rev")
    p.add_argument("--world", default="World1")
    p.add_argument("--flat", action="store_true")
    p.add_argument("--keep-world")
    p.add_argument("--env", action="append", metavar="K=V")
    p.add_argument("--timeout", type=float)
    p.add_argument("--name")
    p.add_argument("--window", action="store_true")
    p.add_argument("--every", type=int, default=None)
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("shot", help="deterministic capture (REPGAME_CAPTURE preset)")
    p.add_argument("spec", help="F12 camera spec 'x,y,z,yaw,pitch[,flags];...'")
    p.add_argument("--quality", type=int, help="REPGAME_CAPTURE_QUALITY 0..3")
    p.add_argument("--time", type=int, help="REPGAME_CAPTURE_TIME (world_time ticks)")
    p.add_argument("--flags", type=int, help="debug-flag bitmask applied to every view")
    p.add_argument("--settle", type=int, help="REPGAME_CAPTURE_SETTLE")
    p.add_argument("--timeout", type=int, help="REPGAME_CAPTURE_TIMEOUT (game ticks per view)")
    p.add_argument("--view-ticks", type=int, help="REPGAME_CAPTURE_VIEW_TICKS")
    p.add_argument("--flags-sweep", action="store_true",
                   help="one run: baseline + one view per debug-flag bit -> sweep.png")
    p.add_argument("--whip", type=int, metavar="N", help="N views at yaw+i*step -> whip.png")
    p.add_argument("--step", type=float, default=15, help="whip yaw step, degrees")
    p.add_argument("--revs", help="comma-separated revs (current|git refs) -> side.png + diffs")
    p.add_argument("--rerun", action=argparse.BooleanOptionalAction, default=True,
                   help="recheck a differing view-0 with one rerun (bistable caveat)")
    # run args, minus --timeout (it's the capture tick timeout here)
    p.add_argument("--build", default="release", choices=["release", "debug", "asan", "tsan"])
    p.add_argument("--rev")
    p.add_argument("--world", default="World1")
    p.add_argument("--flat", action="store_true")
    p.add_argument("--keep-world")
    p.add_argument("--env", action="append", metavar="K=V")
    p.add_argument("--name", default="shot")
    p.add_argument("--window", action="store_true")
    p.add_argument("--every", type=int, default=None)
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("script", help="run a .rds scenario file (REPGAME_SCRIPT)")
    p.add_argument("file", help="scenario file (one command per line)")
    _add_run_args(p)
    p.add_argument("--keep", action="store_true", help="REPGAME_SCRIPT_KEEP=1: stay running")

    p = sub.add_parser("diff", help="image diff: files or shot runs")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--threshold", type=int, default=8, help="max-channel delta per pixel")
    p.add_argument("--out", help="output dir (default: new diff run dir)")
    p.add_argument("--rerun", action="store_true",
                   help="on a view-0 run diff, re-execute both runs once")
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("metrics", help="per-field stats / CSV / PNG plot for one run")
    p.add_argument("ref", help="run id | run dir | results.jsonl | stdout.log | last")
    p.add_argument("--fields", help="comma-separated dotted fields for the stats table")
    p.add_argument("--plot", help="comma-separated dotted fields to chart")
    p.add_argument("--csv", action="store_true", help="dump per-tick metric rows")
    p.add_argument("--after-settle", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("runs", help="list runs from the index")
    p.add_argument("--name")
    p.add_argument("--status")
    p.add_argument("--git")
    p.add_argument("--since", help="e.g. 30m, 2h, 3d")
    p.add_argument("--last", type=int)
    p.add_argument("--reindex", action="store_true", help="rebuild index.jsonl from result.json files")
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("show", help="print a run's result.json")
    p.add_argument("ref", nargs="?", default="last", help="run id | run dir | last")
    p.add_argument("--field", help="dotted path, e.g. stats.metric.loader.light_jobs.max")
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("compare", help="side-by-side stats table for runs")
    p.add_argument("refs", nargs="+")
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("export", help="flat table of runs")
    p.add_argument("refs", nargs="+")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--csv", action="store_true")
    g.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)

    p = sub.add_parser("wt", help="manage --rev worktrees")
    p.add_argument("action", choices=["list", "prune"])
    p.add_argument("--json", action="store_true")
    p.add_argument("--root", default=None)
    return ap


def _preset_env(opts):
    env = dict(opts.env_pairs)
    if opts.cmd == "fly":
        env["REPGAME_FLY"] = opts.spec
        env["REPGAME_FLY_TICKS"] = str(opts.ticks)
        for flag, var in ((opts.settle, "REPGAME_FLY_SETTLE"),
                          (opts.settle_timeout, "REPGAME_FLY_SETTLE_TIMEOUT"),
                          (opts.fly_ticks, "REPGAME_FLY_FLY_TICKS"),
                          (opts.rd, "REPGAME_FLY_RD"),
                          (opts.metrics, "REPGAME_FLY_METRICS")):
            if flag is not None:
                env[var] = str(flag)
    elif opts.cmd == "walk":
        env["REPGAME_WALKTEST"] = "1"
        env["REPGAME_WALKTEST_TICKS"] = str(opts.ticks)
        if opts.teleport:
            env["REPGAME_TELEPORT"] = opts.teleport
    if opts.every is not None:
        env["REPGAME_RESULTS_EVERY"] = str(opts.every)
    return env


def _env_pairs(kv_list, what="--env"):
    pairs = {}
    for kv in kv_list or []:
        if "=" not in kv:
            raise ValueError("%s wants K=V, got %r" % (what, kv))
        k, _, v = kv.partition("=")
        pairs[k] = v
    return pairs


def _scenario_env(opts):
    """ab's scenario: preset args after `--`, a shortcut name, or empty."""
    from . import scenarios
    if opts.extra:
        try:
            o = _parser().parse_args(opts.extra)
        except SystemExit:
            raise ValueError("bad scenario args after --")
        if o.cmd not in ("run", "fly", "walk"):
            raise ValueError("scenario args after -- must be run|fly|walk")
        o.env_pairs = _env_pairs(o.env)
        return _preset_env(o), o.cmd
    if opts.scenario:
        if opts.scenario not in scenarios.SCENARIOS:
            raise ValueError("unknown scenario %r (have: %s)" %
                             (opts.scenario, ", ".join(sorted(scenarios.SCENARIOS))))
        return dict(scenarios.SCENARIOS[opts.scenario]), "fly"
    return {}, "run"


def _do_ab(opts):
    from . import ab as ab_mod

    try:
        scenario_env, scenario_kind = _scenario_env(opts)
        user_env = _env_pairs(opts.env)
        if opts.perf_stat:
            from . import perf as perf_mod
            if not perf_mod.preflight(sys.stderr):
                return 4
        cfgs = []
        for spec in opts.cfg:
            name, _, body = spec.partition("=")
            if not name or not body:
                raise ValueError("--cfg wants NAME=SPEC, got %r" % spec)
            tree_spec, cfg_env = ab_mod.parse_spec(body)
            cfgs.append({"name": name, "spec": body, "tree_spec": tree_spec, "env": cfg_env})
    except ValueError as e:
        print("rep-dbg: %s" % e, file=sys.stderr)
        return 4

    # Build each distinct tree once, up front.
    trees = {}
    for cfg in cfgs:
        if cfg["tree_spec"] in trees:
            continue
        try:
            tree = ab_mod.resolve_tree(cfg["tree_spec"], opts.root, sys.stderr)
        except (ValueError, RuntimeError) as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        if build_mod.ensure_built(tree, opts.build, sys.stderr) is None:
            print("rep-dbg: build failed in %s" % tree, file=sys.stderr)
            return 3
        trees[cfg["tree_spec"]] = tree

    run_dir, ab_id = runner_mod.new_run_dir(opts.root, opts.name)
    started = time.time()
    rep_runs = {cfg["name"]: [] for cfg in cfgs}
    rep_results = {cfg["name"]: [] for cfg in cfgs}
    cfg_by_name = {c["name"]: c for c in cfgs}
    for rep, cfg_name in ab_mod.interleave_order([c["name"] for c in cfgs], opts.reps):
        cfg = cfg_by_name[cfg_name]
        env = dict(scenario_env)
        env.update(user_env)
        env.update(cfg["env"])  # cfg env is the variable under test
        if opts.every is not None:
            env["REPGAME_RESULTS_EVERY"] = str(opts.every)
        if opts.perf_stat:
            from . import perf as perf_mod
            wrap_ns = {"wrap": perf_mod.stat_wrap(None), "perf": True,
                       "stop_signal": signal.SIGINT}
        else:
            wrap_ns = {"wrap": None, "perf": None, "stop_signal": None}
        ns = argparse.Namespace(
            root=opts.root, env=["%s=%s" % kv for kv in env.items()],
            rev=None, rev_arg=(cfg["tree_spec"][4:]
                               if cfg["tree_spec"].startswith("rev:") else None),
            build=opts.build, world=opts.world, flat=opts.flat,
            keep_world=opts.keep_world, timeout=opts.timeout,
            name="%s-%s" % (opts.name, cfg["name"]), kind="ab-rep",
            window=opts.window, extra=[], tree=trees[cfg["tree_spec"]],
            ab_id=ab_id, cfg=cfg["name"], **wrap_ns)
        result, rep_dir = runner_mod.run(ns, sys.stderr)
        rep_runs[cfg["name"]].append({"id": result["id"], "status": result["status"],
                                      "dir": os.path.relpath(rep_dir, opts.root)})
        rep_results[cfg["name"]].append(result)

    fields = opts.metric or ab_mod.DEFAULT_METRICS
    per_metric = {}
    for field in fields:
        per_cfg = {}
        for cfg in cfgs:
            vals = []
            for result in rep_results[cfg["name"]]:
                try:
                    v = results_mod.field_of(result, field)
                except (KeyError, IndexError, ValueError):
                    continue
                if isinstance(v, (int, float)) and not isinstance(v, bool):
                    vals.append(v)
            if vals:
                per_cfg[cfg["name"]] = ab_mod.aggregate(vals)
        if per_cfg:
            per_metric[field] = per_cfg
    metrics = ab_mod.metric_table(per_metric)

    any_bad = any(r["status"] != "ok"
                  for reps in rep_results.values() for r in reps)
    result = {
        "schema": runner_mod.SCHEMA,
        "id": ab_id,
        "name": opts.name,
        "kind": "ab",
        "status": "failed" if any_bad else "ok",
        "git": build_mod.git_info(build_mod.REPO),
        "build": opts.build,
        "scenario": opts.scenario or (" ".join(opts.extra) if opts.extra else None),
        "cfg_order": [c["name"] for c in cfgs],
        "cfgs": {c["name"]: {"spec": c["spec"], "tree": trees[c["tree_spec"]], "env": c["env"]}
                 for c in cfgs},
        "reps": opts.reps,
        "metrics": metrics,
        "rep_runs": rep_runs,
        "started": datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
        "wall_s": round(time.time() - started, 2),
        "artifacts": {"summary": "summary.md"},
    }
    md = ab_mod.summary_md(result)
    with open(os.path.join(run_dir, "summary.md"), "w") as f:
        f.write(md)
    with open(os.path.join(run_dir, "result.json"), "w") as f:
        json.dump(result, f, indent=2, sort_keys=True)
    index_mod.append(opts.root, index_mod.index_entry(result, run_dir, opts.root))

    print("rep-dbg: %s %s" % (result["status"], run_dir), file=sys.stderr)
    if opts.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(md)
    return runner_mod.exit_code(result)


def _do_perf(opts):
    from . import perf as perf_mod

    if opts.action == "diff":
        if len(opts.args) != 2:
            print("rep-dbg: perf diff wants <runA> <runB>", file=sys.stderr)
            return 4
        try:
            tops = []
            for ref in opts.args:
                r = results_mod.load(ref, opts.root)
                top = (r.result.get("perf") or {}).get("top")
                if top is None:
                    raise ValueError("%s has no perf.top (not a perf record run)" % ref)
                tops.append(top)
        except (LookupError, OSError, ValueError) as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        rows = perf_mod.top_diff(tops[0], tops[1], opts.top)
        if opts.json:
            print(json.dumps(rows, indent=2, sort_keys=True))
        else:
            for r in rows:
                print("%6.2f -> %6.2f  %+6.2f  %-16s %s" % (
                    r["a_pct"], r["b_pct"], r["delta"], r["thread"] or "-", r["symbol"]))
        return 0

    if not perf_mod.preflight(sys.stderr):
        return 4

    if opts.action == "attach":
        if not opts.args:
            print("rep-dbg: perf attach wants <pid|name>", file=sys.stderr)
            return 4
        try:
            pid, name = perf_mod.resolve_pid(opts.args[0])
        except ValueError as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        run_dir, run_id = runner_mod.new_run_dir(opts.root, "perf-attach")
        duration = opts.duration or 15
        result = {"schema": runner_mod.SCHEMA, "id": run_id, "name": "perf-attach",
                  "kind": "perf", "status": None, "source": "perf-attach",
                  "attach": {"pid": pid, "target": name,
                             "duration_s": duration, "freq": opts.freq,
                             "call_graph": opts.call_graph},
                  "started": datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")}
        perf_mod.attach(pid, duration, opts.freq, opts.call_graph, opts.threads,
                        run_dir, run_id, result, sys.stderr)
        perf_mod.postprocess(run_dir, result, opts, sys.stderr)
        with open(os.path.join(run_dir, "result.json"), "w") as f:
            json.dump(result, f, indent=2, sort_keys=True)
        index_mod.append(opts.root, index_mod.index_entry(result, run_dir, opts.root))
        print("rep-dbg: %s %s" % (result["status"], run_dir), file=sys.stderr)
        if opts.json:
            print(json.dumps(result, indent=2, sort_keys=True))
        return runner_mod.exit_code(result)

    # record / stat: a normal preset run with a wrapped argv.
    opts.scenario = opts.args[0] if opts.args else None
    try:
        env, _kind = _scenario_env(opts)
        env.update(_env_pairs(opts.env))
    except ValueError as e:
        print("rep-dbg: %s" % e, file=sys.stderr)
        return 4
    if opts.action == "record":
        if opts.call_graph == "fp":
            print("rep-dbg: note: fp call graphs want a frame-pointer build"
                  " (`make linux PERF=1`); unoptimized-out frames show as"
                  " truncated stacks", file=sys.stderr)
        wrap = perf_mod.record_wrap(opts.freq, opts.call_graph)
        name = opts.name or "perf"
    else:
        wrap = perf_mod.stat_wrap(opts.events)
        name = opts.name or "perf-stat"
    if opts.every is not None:
        env["REPGAME_RESULTS_EVERY"] = str(opts.every)
    ns = argparse.Namespace(
        root=opts.root, env=["%s=%s" % kv for kv in env.items()],
        rev=opts.rev, rev_arg=None, build=opts.build, world=opts.world,
        flat=opts.flat, keep_world=opts.keep_world,
        timeout=opts.duration or opts.timeout, name=name, kind="perf",
        window=opts.window, extra=[], wrap=wrap, perf=True,
        stop_signal=signal.SIGINT, ab_id=None, cfg=None, threads=opts.threads)
    result, run_dir = runner_mod.run(ns, sys.stderr)
    print("rep-dbg: %s %s" % (result["status"], run_dir), file=sys.stderr)
    if opts.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    return runner_mod.exit_code(result)


def _shot_run(opts, tree, rev_arg, name, extra_env, mode):
    from . import capture as capture_mod
    ns = argparse.Namespace(
        root=opts.root, env=["%s=%s" % kv for kv in extra_env.items()],
        rev=None, rev_arg=rev_arg, build=opts.build, world=opts.world,
        flat=opts.flat, keep_world=opts.keep_world, timeout=None,
        name=name, kind="shot", window=opts.window, extra=[], tree=tree,
        ab_id=None, cfg=None,
        postprocess_fn=(lambda d, r, l, m=mode: capture_mod.postprocess(d, r, m, l)))
    result, run_dir = runner_mod.run(ns, sys.stderr)
    print("rep-dbg: %s %s" % (result["status"], run_dir), file=sys.stderr)
    return result, run_dir


def _shot_env(opts):
    from . import capture as capture_mod
    try:
        views = capture_mod.parse_views(opts.spec)
    except ValueError as e:
        raise ValueError(str(e))
    mode = None
    if opts.flags is not None:
        for v in views:
            v[5] = opts.flags
    if opts.flags_sweep:
        views = capture_mod.sweep_views(opts.spec)
        mode = "sweep"
    elif opts.whip:
        views = capture_mod.whip_views(opts.spec, opts.whip, opts.step)
        mode = "whip"
    env = {"REPGAME_CAPTURE": capture_mod.format_views(views)}
    env.update(_env_pairs(opts.env))
    for flag, var in ((opts.quality, "REPGAME_CAPTURE_QUALITY"),
                      (opts.time, "REPGAME_CAPTURE_TIME"),
                      (opts.settle, "REPGAME_CAPTURE_SETTLE"),
                      (opts.timeout, "REPGAME_CAPTURE_TIMEOUT"),
                      (opts.view_ticks, "REPGAME_CAPTURE_VIEW_TICKS")):
        if flag is not None:
            env[var] = str(flag)
    if mode == "whip" and opts.view_ticks is None:
        env["REPGAME_CAPTURE_VIEW_TICKS"] = "2"
    return env, mode


def _do_shot(opts):
    from . import ab as ab_mod
    from . import capture as capture_mod
    from . import imgdiff

    try:
        env, mode = _shot_env(opts)
    except ValueError as e:
        print("rep-dbg: %s" % e, file=sys.stderr)
        return 4

    if opts.revs:
        revs = [r.strip() for r in opts.revs.split(",") if r.strip()]
        if len(revs) < 2:
            print("rep-dbg: --revs wants at least two", file=sys.stderr)
            return 4
        parent, parent_id = runner_mod.new_run_dir(opts.root, "shot-revs")
        runs = []
        for rev in revs:
            tree_spec = "current" if rev == "current" else "rev:" + rev
            try:
                tree = ab_mod.resolve_tree(tree_spec, opts.root, sys.stderr)
            except (ValueError, RuntimeError) as e:
                print("rep-dbg: %s" % e, file=sys.stderr)
                return 4
            rev_arg = tree_spec[4:] if tree_spec.startswith("rev:") else None
            result, run_dir = _shot_run(opts, tree, rev_arg,
                                        "shot-%s" % rev.replace("/", "_"), env, mode)
            runs.append({"rev": rev, "id": result["id"], "status": result["status"],
                         "dir": os.path.relpath(run_dir, opts.root), "result": result})

        shots = {}
        for r in runs:
            entries = (r["result"].get("artifacts") or {}).get("screenshots") or []
            if entries:
                shots[r["rev"]] = os.path.join(opts.root, r["dir"], entries[0]["file"])
        diffs = []
        base_rev = revs[0]
        for r in runs[1:]:
            if base_rev in shots and r["rev"] in shots:
                try:
                    d = imgdiff.compare(shots[base_rev], shots[r["rev"]])
                except ValueError as e:
                    print("rep-dbg: diff %s vs %s: %s" % (base_rev, r["rev"], e), file=sys.stderr)
                    continue
                if d["changed_px"] and opts.rerun:
                    stable = _diff_rerun(runs[0]["result"], r["result"], opts.root,
                                         first_changed=True)
                    d["stable"] = stable
                    if stable is False:
                        print("rep-dbg: bistable capture_0 for %s vs %s — not a regression"
                              % (base_rev, r["rev"]), file=sys.stderr)
                diffs.append({"a": base_rev, "b": r["rev"], **d})
        if len(shots) >= 2:
            from PIL import Image
            tiles = [(Image.open(shots[rev]), rev) for rev in revs if rev in shots]
            capture_mod.contact_sheet(tiles, os.path.join(parent, "side.png"))
        result = {"schema": runner_mod.SCHEMA, "id": parent_id, "name": "shot-revs",
                  "kind": "shot-revs",
                  "status": "ok" if all(r["status"] == "ok" for r in runs) else "failed",
                  "spec": opts.spec, "revs": revs,
                  "runs": [{k: v for k, v in r.items() if k != "result"} for r in runs],
                  "diffs": diffs,
                  "started": datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
                  "artifacts": {"side": "side.png" if len(shots) >= 2 else None}}
        with open(os.path.join(parent, "result.json"), "w") as f:
            json.dump(result, f, indent=2, sort_keys=True)
        index_mod.append(opts.root, index_mod.index_entry(result, parent, opts.root))
        print("rep-dbg: %s %s" % (result["status"], parent), file=sys.stderr)
        if opts.json:
            print(json.dumps(result, indent=2, sort_keys=True))
        else:
            for d in diffs:
                print("%s vs %s: changed_px=%d max_delta=%d stable=%s" % (
                    d["a"], d["b"], d["changed_px"], d["max_delta"], d["stable"]))
        return runner_mod.exit_code(result)

    tree = build_mod.REPO
    if opts.rev:
        sha = build_mod.git(tree, "rev-parse", opts.rev)
        from . import worktree as worktree_mod
        tree = worktree_mod.ensure(opts.root, sha, sys.stderr)
    result, run_dir = _shot_run(opts, tree, opts.rev, opts.name, env, mode)
    if opts.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    return runner_mod.exit_code(result)


def _diff_rerun(res_a, res_b, root, first_changed):
    """Re-execute both runs once, re-diff view 0. stable=False when the
    second diff disagrees with the first (the bistable signature)."""
    from . import capture as capture_mod
    from . import imgdiff
    _dir_a, pngs_a, _c = capture_mod.rerun(res_a, root, sys.stderr)
    _dir_b, pngs_b, _c = capture_mod.rerun(res_b, root, sys.stderr)
    if 0 not in pngs_a or 0 not in pngs_b:
        return None
    try:
        second = imgdiff.compare(pngs_a[0], pngs_b[0])
    except ValueError:
        return None
    return (second["changed_px"] > 0) == first_changed


def _do_diff(opts):
    from . import imgdiff

    def as_images(ref):
        if os.path.isfile(ref):
            return {0: ref}, None
        try:
            r = results_mod.load(ref, opts.root)
        except (LookupError, OSError) as e:
            raise ValueError(str(e))
        entries = (r.result.get("artifacts") or {}).get("screenshots")
        if not entries:
            raise ValueError("%s has no artifacts.screenshots" % ref)
        return {e["view"]: os.path.join(r.dir, e["file"]) for e in entries}, r.result

    try:
        imgs_a, res_a = as_images(opts.a)
        imgs_b, res_b = as_images(opts.b)
    except ValueError as e:
        print("rep-dbg: %s" % e, file=sys.stderr)
        return 4

    out_dir = opts.out
    if not out_dir:
        out_dir, _id = runner_mod.new_run_dir(opts.root, "diff")
    os.makedirs(out_dir, exist_ok=True)
    pairs = sorted(set(imgs_a) & set(imgs_b))
    if not pairs:
        print("rep-dbg: no shared view indices", file=sys.stderr)
        return 4
    rows, any_changed = [], False
    for view in pairs:
        pa, pb = imgs_a[view], imgs_b[view]
        try:
            d = imgdiff.compare(pa, pb, opts.threshold)
        except ValueError as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        heat = os.path.join(out_dir, "heat%s.png" % ("_%d" % view if len(pairs) > 1 else ""))
        imgdiff.heat_png(pa, pb, heat)
        imgdiff.side_png(pa, pb, heat, os.path.join(
            out_dir, "side%s.png" % ("_%d" % view if len(pairs) > 1 else "")))
        if view == 0 and d["changed_px"] and opts.rerun and res_a and res_b:
            stable = _diff_rerun(res_a, res_b, opts.root, first_changed=True)
            d["stable"] = stable
            if stable is False:
                print("rep-dbg: bistable capture_0 — not a regression", file=sys.stderr)
        d["view"] = view
        d["heat"] = os.path.relpath(heat, out_dir)
        rows.append(d)
        any_changed = any_changed or d["changed_px"] > 0
    if opts.json:
        print(json.dumps(rows, indent=2, sort_keys=True))
    else:
        for d in rows:
            print("view %d: changed_px=%d (%.4f%%) max_delta=%d mean_delta=%.3f bbox=%s stable=%s" % (
                d["view"], d["changed_px"], d["changed_frac"] * 100,
                d["max_delta"], d["mean_delta"], d["bbox"], d["stable"]))
        print("rep-dbg: diff artifacts in %s" % out_dir, file=sys.stderr)
    return 1 if any_changed else 0


def _script_headers(path):
    """#! world <name> | #! keep-world <path> | #! terrain | #! env K=V |
    #! timeout S."""
    hdr = {"env": []}
    try:
        with open(path, errors="replace") as f:
            for line in f:
                if not line.startswith("#!"):
                    continue
                parts = line[2:].split()
                if len(parts) >= 2 and parts[0] == "world":
                    hdr["world"] = parts[1]
                elif len(parts) >= 2 and parts[0] == "keep-world":
                    hdr["keep_world"] = parts[1]
                elif parts and parts[0] == "terrain":
                    hdr["terrain"] = True
                elif len(parts) >= 3 and parts[0] == "env":
                    hdr["env"].append("=".join(parts[1:]))
                elif len(parts) >= 2 and parts[0] == "timeout":
                    hdr["timeout"] = float(parts[1])
    except OSError as e:
        raise ValueError("script file: %s" % e)
    return hdr


def _do_script(opts):
    from . import capture as capture_mod
    try:
        hdr = _script_headers(opts.file)
    except ValueError as e:
        print("rep-dbg: %s" % e, file=sys.stderr)
        return 4
    env = _env_pairs(hdr["env"], "#! env")
    env.update(_env_pairs(opts.env))  # CLI --env wins over headers
    env["REPGAME_SCRIPT"] = os.path.abspath(opts.file)
    if opts.keep:
        env["REPGAME_SCRIPT_KEEP"] = "1"
    if opts.every is not None:
        env["REPGAME_RESULTS_EVERY"] = str(opts.every)
    # Default to a flat world; #! terrain/world/keep-world says otherwise.
    flat = opts.flat or not (hdr.get("terrain") or hdr.get("world") or hdr.get("keep_world"))
    ns = argparse.Namespace(
        root=opts.root, env=["%s=%s" % kv for kv in env.items()],
        rev=opts.rev, rev_arg=None, build=opts.build,
        world=hdr.get("world") or opts.world, flat=flat,
        keep_world=hdr.get("keep_world") or opts.keep_world,
        timeout=opts.timeout or hdr.get("timeout"),
        name=opts.name or os.path.splitext(os.path.basename(opts.file))[0],
        kind="script", window=opts.window, extra=[],
        ab_id=None, cfg=None,
        postprocess_fn=lambda d, r, l: capture_mod.postprocess_script(d, r, log=l))
    result, run_dir = runner_mod.run(ns, sys.stderr)
    print("rep-dbg: %s %s" % (result["status"], run_dir), file=sys.stderr)
    for f_ in (result.get("asserts") or {}).get("failures", []):
        print("%s:%s: %s — expected %s, actual %s" % (
            opts.file, f_.get("line"), f_.get("cmd"), f_.get("expected"), f_.get("actual")),
            file=sys.stderr)
    if opts.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    return runner_mod.exit_code(result)


def _do_run(opts):
    env = _preset_env(opts)
    # Preset pairs first so a user's --env overrides a preset default.
    opts.env = ["%s=%s" % kv for kv in env.items()] + list(opts.env or [])
    result, run_dir = runner_mod.run(opts, sys.stderr)
    print("rep-dbg: %s %s" % (result["status"], run_dir), file=sys.stderr)
    if opts.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        s = result.get("game") or {}
        print("%s: %s (%.1fs, exit_reason=%s)" % (result["id"], result["status"],
                                                result.get("wall_s") or 0,
                                                s.get("exit_reason")))
        print("  %s" % run_dir)
    return runner_mod.exit_code(result)


def _flatten_stats(result):
    flat = {}
    for key, agg in (result.get("stats") or {}).items():
        if not agg:
            continue
        for sub in ("mean", "max", "p50", "p95", "last"):
            flat["stats.%s.%s" % (key, sub)] = agg[sub]
    return flat


def main(argv):
    # Everything after a literal `--` is extra args for the game binary.
    extra = []
    if "--" in argv:
        i = argv.index("--")
        extra = argv[i + 1:]
        argv = argv[:i]
    opts = _parser().parse_args(argv)
    opts.extra = extra
    opts.root = opts.root or results_mod.default_root()

    if opts.cmd in ("run", "fly", "walk"):
        opts.env_pairs = {}
        for kv in opts.env or []:
            if "=" not in kv:
                print("rep-dbg: --env wants K=V, got %r" % kv, file=sys.stderr)
                return 4
            k, _, v = kv.partition("=")
            opts.env_pairs[k] = v
        return _do_run(opts)

    if opts.cmd == "ab":
        return _do_ab(opts)

    if opts.cmd == "perf":
        return _do_perf(opts)

    if opts.cmd == "shot":
        return _do_shot(opts)

    if opts.cmd == "script":
        return _do_script(opts)

    if opts.cmd == "diff":
        return _do_diff(opts)

    if opts.cmd == "metrics":
        from . import metrics as metrics_mod
        try:
            records, run_dir = metrics_mod.load_records(opts.ref, opts.root)
        except (LookupError, OSError, ValueError, json.JSONDecodeError) as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        metrics = metrics_mod.metric_records(records, opts.after_settle)
        if not metrics:
            print("rep-dbg: no metric records", file=sys.stderr)
            return 4
        fields = opts.fields.split(",") if opts.fields else None
        if opts.csv:
            header, rows = metrics_mod.csv_rows(metrics)
            w = csv.writer(sys.stdout)
            w.writerow(header)
            w.writerows(rows)
        else:
            stats = stats_mod.compute_stats(metrics)
            wanted = None
            if fields:
                wanted = set(f if f.startswith("metric.") else "metric." + f for f in fields)
            metrics_mod.print_table(stats, wanted)
        if opts.plot:
            try:
                plot_fields = [f[len("metric."):] if f.startswith("metric.") else f
                               for f in opts.plot.split(",")]
                base = run_dir or os.path.dirname(os.path.abspath(opts.ref))
                out = os.path.join(base, "metrics.png")
                metrics_mod.plot(metrics, plot_fields, out)
            except (ValueError, ImportError) as e:
                print("rep-dbg: plot: %s" % e, file=sys.stderr)
                return 4
            print(out)
            if run_dir:
                result_path = os.path.join(run_dir, "result.json")
                if os.path.exists(result_path):
                    with open(result_path) as f:
                        result = json.load(f)
                    result.setdefault("artifacts", {}).setdefault("plots", []).append(
                        os.path.relpath(out, run_dir))
                    with open(result_path, "w") as f:
                        json.dump(result, f, indent=2, sort_keys=True)
        return 0

    if opts.cmd == "runs":
        if opts.reindex:
            entries = index_mod.reindex(opts.root)
            print("rep-dbg: reindexed %d runs" % len(entries), file=sys.stderr)
        runs = results_mod.query(opts.root, name=opts.name, status=opts.status,
                                 git=opts.git, since=opts.since, last=opts.last)
        if opts.json:
            print(json.dumps([r.result for r in runs], indent=2, sort_keys=True))
        else:
            for r in runs:
                g = r.result.get("game") or {}
                print("%-40s %-8s %-7s %6.1fs  %s" % (
                    r.id, r.status, r.result.get("build"),
                    r.result.get("wall_s") or 0, g.get("exit_reason") or "-"))
        return 0

    if opts.cmd == "show":
        try:
            r = results_mod.load(opts.ref, opts.root)
        except (LookupError, OSError) as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        if opts.field:
            try:
                val = r.field(opts.field)
            except (KeyError, IndexError, ValueError) as e:
                print("rep-dbg: field %s: %s" % (opts.field, e), file=sys.stderr)
                return 4
            print(json.dumps(val) if opts.json else
                  (json.dumps(val) if isinstance(val, (dict, list)) else str(val)))
            return 0
        if opts.json:
            print(json.dumps(r.result, indent=2, sort_keys=True))
        else:
            print("%s  status=%s  build=%s  wall=%.1fs  dir=%s" % (
                r.id, r.status, r.result.get("build"),
                r.result.get("wall_s") or 0, r.dir))
            for key in sorted(r.stats):
                agg = r.stats[key]
                if agg:
                    print("  %-44s mean=%-12.3g max=%-12.3g p95=%-12.3g last=%s" % (
                        key, agg["mean"], agg["max"], agg["p95"], agg["last"]))
        return 0

    if opts.cmd == "compare":
        try:
            runs = [results_mod.load(ref, opts.root) for ref in opts.refs]
        except (LookupError, OSError) as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        fields = sorted({f for r in runs for f in _flatten_stats(r.result)})
        rows = [{"id": r.id, "status": r.status,
                 "asserts": r.asserts,
                 **_flatten_stats(r.result)} for r in runs]
        if opts.json:
            print(json.dumps(rows, indent=2, sort_keys=True))
            return 0
        cols = ["field"] + [r.id for r in runs]
        print("%-50s %s" % (cols[0], "  ".join("%15s" % c for c in cols[1:])))
        print("%-50s %s" % ("status", "  ".join("%15s" % r.status for r in runs)))
        for f in fields:
            print("%-50s %s" % (f, "  ".join(
                "%15.4g" % row[f] if f in row else "%15s" % "-"
                for row in rows)))
        return 0

    if opts.cmd == "export":
        try:
            runs = [results_mod.load(ref, opts.root) for ref in opts.refs]
        except (LookupError, OSError) as e:
            print("rep-dbg: %s" % e, file=sys.stderr)
            return 4
        rows = [{"id": r.id, "status": r.status, "wall_s": r.result.get("wall_s"),
                 **_flatten_stats(r.result)} for r in runs]
        if opts.json:
            print(json.dumps(rows, indent=2, sort_keys=True))
        else:
            fields = sorted({f for row in rows for f in row})
            header = ["id", "status", "wall_s"] + [f for f in fields if f not in ("id", "status", "wall_s")]
            print(",".join(header))
            for row in rows:
                print(",".join(str(row.get(f, "")) for f in header))
        return 0

    if opts.cmd == "wt":
        if opts.action == "list":
            shas = worktree_mod.list(opts.root)
            if opts.json:
                print(json.dumps(shas))
            else:
                for sha in shas:
                    print(os.path.join(worktree_mod.wt_root(opts.root), sha))
            return 0
        removed = worktree_mod.prune(opts.root, sys.stderr)
        if opts.json:
            print(json.dumps(removed))
        else:
            print("rep-dbg: pruned %d worktree(s)" % len(removed))
        return 0

    return 4
