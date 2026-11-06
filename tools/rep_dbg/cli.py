"""rep-dbg CLI — see docs/debug-tools.md.

Conventions: every subcommand accepts --json and then prints exactly one
JSON object on stdout; progress goes to stderr. Exit codes: 0 ok,
1 asserts failed, 2 crashed/timeout, 3 build failed, 4 usage error. The
last stderr line of run-producing subcommands is
`rep-dbg: <status> <run-dir>`.
"""

import argparse
import json
import os
import sys

from . import build as build_mod
from . import index as index_mod
from . import results as results_mod
from . import runner as runner_mod
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
