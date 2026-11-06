"""Run execution: run dir layout, env assembly, process lifecycle, and
result.json assembly.

Run dir <root>/<name>/<YYYYmmdd-HHMMSS>-<n>/:
    result.json, results.jsonl, stdout.log, home/ (isolated $HOME),
    screenshots/ (the game's cwd).
"""

import datetime
import json
import os
import re
import shutil
import subprocess
import time

from . import build as build_mod
from . import index as index_mod
from . import logflags
from . import logparse
from . import stats as stats_mod
from . import worktree as worktree_mod

SCHEMA = 1


def new_run_dir(root, name):
    base = os.path.join(root, name)
    os.makedirs(base, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    n = 1
    while os.path.exists(os.path.join(base, "%s-%d" % (stamp, n))):
        n += 1
    run_dir = os.path.join(base, "%s-%d" % (stamp, n))
    os.makedirs(run_dir)
    return run_dir, "%s/%s-%d" % (name, stamp, n)


def _parse_env(pairs):
    env = {}
    for kv in pairs or []:
        if "=" not in kv:
            raise ValueError("--env wants K=V, got %r" % kv)
        k, _, v = kv.partition("=")
        env[k] = v
    return env


def _jsonl(path):
    recs = []
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        recs.append(json.loads(line))
                    except json.JSONDecodeError:
                        pass
    except OSError:
        pass
    return recs


def run(opts, log):
    """opts: the argparse namespace from cli.py. Returns (result, run_dir)."""
    root = opts.root
    repgame_env = _parse_env(opts.env)

    # ab reps pass an already-resolved tree via opts.tree; --rev resolves
    # into the cached worktree instead.
    tree = getattr(opts, "tree", None) or build_mod.REPO
    rev_arg = getattr(opts, "rev_arg", None) or opts.rev
    # Refs (HEAD~3, master, ...) resolve against the main repo — inside a
    # detached worktree HEAD is already the target commit.
    info = build_mod.git_info(build_mod.REPO if rev_arg else tree, rev_arg)
    if opts.rev:
        tree = worktree_mod.ensure(root, info["sha"], log)

    binary = build_mod.ensure_built(tree, opts.build, log)
    if binary is None:
        run_dir, run_id = new_run_dir(root, opts.name)
        result = _result_base(run_id, opts, info, run_dir, build=opts.build)
        result["status"] = "build_failed"
        _finish(run_dir, result)
        return result, run_dir

    run_dir, run_id = new_run_dir(root, opts.name)
    home = os.path.join(run_dir, "home")
    os.makedirs(os.path.join(home, ".repgame"), exist_ok=True)

    world = opts.world
    if opts.flat:
        wdir = os.path.join(home, ".repgame", world)
        os.makedirs(wdir, exist_ok=True)
        with open(os.path.join(wdir, "world_type"), "w") as f:
            f.write("flat")
    if opts.keep_world:
        dst = os.path.join(home, ".repgame", world)
        if os.path.isdir(opts.keep_world):
            shutil.copytree(opts.keep_world, dst, dirs_exist_ok=True)
        else:
            raise ValueError("--keep-world: %s is not a directory" % opts.keep_world)

    results_path = os.path.join(run_dir, "results.jsonl")
    env = dict(os.environ)
    env["HOME"] = home
    env["REPGAME_RESULTS"] = results_path
    env["REPGAME_GIT"] = info["sha"]
    if not opts.window:
        env["SDL_VIDEODRIVER"] = "offscreen"
        env["SDL_AUDIODRIVER"] = "dummy"
    env.update(repgame_env)
    repgame_env = {k: v for k, v in env.items() if k.startswith("REPGAME_")}

    argv = [binary, world] + list(opts.extra or [])
    print("rep-dbg: run %s" % " ".join(argv), file=log)
    for k in sorted(repgame_env):
        print("rep-dbg:   %s=%s" % (k, repgame_env[k]), file=log)

    result = _result_base(run_id, opts, info, run_dir, build=opts.build)
    result["cmd"] = argv
    result["env"] = repgame_env
    result["binary"] = binary

    started = time.time()
    result["started"] = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
    timed_out = False
    with open(os.path.join(run_dir, "stdout.log"), "w") as logf:
        proc = subprocess.Popen(argv, cwd=run_dir, env=env,
                                stdout=logf, stderr=subprocess.STDOUT)
        try:
            code = proc.wait(timeout=opts.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            proc.terminate()
            try:
                code = proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                code = proc.wait()
    result["wall_s"] = round(time.time() - started, 2)
    result["exit"] = {"code": code if code >= 0 else None,
                      "signal": -code if code < 0 else None}

    assemble_result(result, run_dir, timed_out=timed_out)
    _finish(run_dir, result)
    return result, run_dir


def _result_base(run_id, opts, info, run_dir, build):
    result = {
        "schema": SCHEMA,
        "id": run_id,
        "name": opts.name,
        "kind": opts.kind,
        "status": None,
        "git": info,
        "build": build,
        "source": "results",
    }
    for key in ("ab_id", "cfg"):
        val = getattr(opts, key, None)
        if val is not None:
            result[key] = val
    return result


def assemble_result(result, run_dir, timed_out=False):
    """Fill status/game/stats/asserts/artifacts/log_flags from the run dir."""
    results_path = os.path.join(run_dir, "results.jsonl")
    records = _jsonl(results_path)
    if not any(r.get("type") == "header" for r in records):
        # Old binary without the results writer (a --rev run): rebuild the
        # stream from the log text.
        n = logparse.convert(os.path.join(run_dir, "stdout.log"), results_path,
                             git=result.get("git", {}).get("sha", ""))
        if n > 1:
            records = _jsonl(results_path)
            result["source"] = "log-shim"

    summary = next((r for r in reversed(records) if r.get("type") == "summary"), None)
    if summary is None and result.get("exit", {}).get("code") == 0 and not timed_out:
        # Clean exit without a summary record (log-shim runs): synthesize it.
        ticks = max((r.get("tick", 0) for r in records), default=0)
        summary = {"type": "summary", "tick": ticks, "ticks": ticks,
                   "exit_reason": "unknown", "asserts": {"total": 0, "failed": 0},
                   "shots": [], "settle_tick": 0}
    result["game"] = summary

    metrics = [r for r in records if r.get("type") == "metric"]
    result["stats"] = stats_mod.compute_stats(metrics, summary.get("settle_tick") if summary else None)

    assert_recs = [r for r in records if r.get("type") == "assert"]
    result["asserts"] = {
        "total": len(assert_recs),
        "failed": sum(1 for r in assert_recs if not r.get("ok")),
        "failures": [{"line": r.get("line"), "cmd": r.get("cmd"),
                      "expected": r.get("expected"), "actual": r.get("actual")}
                     for r in assert_recs if not r.get("ok")],
    }

    shots = sorted("screenshots/" + f
                   for f in os.listdir(os.path.join(run_dir, "screenshots"))
                   if f.endswith((".tga", ".png"))) \
        if os.path.isdir(os.path.join(run_dir, "screenshots")) else []
    result["artifacts"] = {"log": "stdout.log", "results": "results.jsonl",
                           "screenshots": shots}

    flags = logflags.scan_file(os.path.join(run_dir, "stdout.log"))
    result["log_flags"] = flags

    if timed_out:
        result["status"] = "timeout"
    elif summary is None:
        result["status"] = "crashed"
    elif result["asserts"]["failed"]:
        result["status"] = "failed"
    elif flags.get("asan_errors") or flags.get("ubsan_errors"):
        result["status"] = "failed"
    else:
        result["status"] = "ok"


def _finish(run_dir, result):
    with open(os.path.join(run_dir, "result.json"), "w") as f:
        json.dump(result, f, indent=2, sort_keys=True)
    root = os.path.dirname(os.path.dirname(run_dir))
    index_mod.append(root, index_mod.index_entry(result, run_dir, root))


def exit_code(result):
    status = result.get("status")
    if status == "ok":
        return 0
    if status == "failed":
        return 1
    if status in ("crashed", "timeout"):
        return 2
    if status == "build_failed":
        return 3
    return 4
