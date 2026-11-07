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
import signal
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

    if opts.build in ("asan", "tsan"):
        from . import sanitizer as sanitizer_mod
        env.update(sanitizer_mod.sanitizer_env(opts.build, run_dir))

    # perf record/stat reps wrap the game argv; '{run_dir}' placeholders in
    # the wrap argv get the just-created run dir. --gdb wraps in batch gdb
    # whose own output goes to crash.txt while the inferior's output is
    # redirected (gdb `run >`) back into stdout.log.
    gdb_mode = bool(getattr(opts, "gdb", False))
    wrap = getattr(opts, "wrap", None)
    if gdb_mode and wrap is None:
        from . import crash as crash_mod
        wrap = crash_mod.gdb_wrap()
    wrap = [w.replace("{run_dir}", run_dir)
            for w in (wrap or [])]
    argv = wrap + [binary, world] + list(opts.extra or [])
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
    stdout_path = os.path.join(run_dir, "stdout.log")
    # gdb mode: gdb's own stdout lands in crash.txt; the inferior's output is
    # redirected by `run > stdout.log` inside gdb. Inferior stderr still
    # arrives via the parent pipe, so append it to stdout.log.
    out_path = os.path.join(run_dir, "crash.txt") if gdb_mode else stdout_path
    with open(out_path, "w") as logf, \
            open(stdout_path, "a") as errf:
        proc = subprocess.Popen(argv, cwd=run_dir, env=env,
                                stdout=logf,
                                stderr=errf if gdb_mode else subprocess.STDOUT)
        window_info = None
        if isinstance(getattr(opts, "timed_window", None), dict):
            timed_out, window_info = _window_loop(
                proc, opts.timed_window, run_dir, opts.timeout, started, log)
            if proc.poll() is None:
                # The window completed: rep-dbg stops the game itself (clean
                # SIGTERM exit — not a timeout).
                proc.send_signal(signal.SIGTERM)
                try:
                    code = proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    code = proc.wait()
            else:
                code = proc.wait()
        else:
            try:
                code = proc.wait(timeout=opts.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                # perf reps want SIGINT so perf.data is finalized; gdb mode
                # uses SIGINT so batch gdb interrupts the inferior and still
                # runs the queued backtrace before quitting.
                proc.send_signal(getattr(opts, "stop_signal", None) or
                                 (signal.SIGINT if gdb_mode else signal.SIGTERM))
                try:
                    code = proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    code = proc.wait()
    result["wall_s"] = round(time.time() - started, 2)
    result["exit"] = {"code": code if code >= 0 else None,
                      "signal": -code if code < 0 else None}

    assemble_result(result, run_dir, timed_out=timed_out)
    if isinstance(getattr(opts, "timed_window", None), dict):
        _window_result(result, run_dir, window_info, timed_out)
    if gdb_mode:
        from . import crash as crash_mod
        crash_mod.postprocess(run_dir, result, log)
    if opts.build in ("asan", "tsan"):
        sanitizer_mod.postprocess(run_dir, result, log)
    if getattr(opts, "perf", None):
        from . import perf as perf_mod
        perf_mod.postprocess(run_dir, result, opts, log)
        if result["status"] == "ok" and \
                (result.get("perf") or {}).get("stat") is None and \
                not (result.get("perf") or {}).get("data_size_bytes"):
            result["status"] = "failed"
    post_fn = getattr(opts, "postprocess_fn", None)
    if post_fn:
        post_fn(run_dir, result, log)
    _finish(run_dir, result)
    return result, run_dir


def _max_tick(records):
    return max((r.get("tick", 0) for r in records), default=0)


def _window_loop(proc, win, run_dir, timeout, started, log):
    """Drive a fixed-duration window: wait for win['after_event'] in
    results.jsonl, warm up, run win['window_s'] seconds between on_start/
    on_end. Returns (timed_out, info)."""
    results_path = os.path.join(run_dir, "results.jsonl")
    event = win["after_event"]
    warmup_s = win.get("warmup_s", 0)
    window_s = win["window_s"]
    deadline = (started + timeout) if timeout else None
    info = {"event": event, "warmup_s": warmup_s, "error": None,
            "start_tick": None, "end_tick": None,
            "start_wall": None, "end_wall": None}

    # Wait for the event record (the game fflushes per record).
    seen = False
    while not seen:
        if proc.poll() is not None:
            return False, {**info, "error": "game exited before '%s'" % event}
        if deadline is not None and time.time() > deadline:
            return True, {**info, "error": "'%s' never arrived" % event}
        for r in _jsonl(results_path):
            if r.get("type") == "event" and r.get("name") == event:
                seen = True
                break
        if not seen:
            time.sleep(0.2)

    if _sleep_checking(proc, warmup_s):
        return False, {**info, "error": "game exited during warmup"}
    info["start_tick"] = _max_tick(_jsonl(results_path))
    info["start_wall"] = time.time()

    handle = None
    try:
        if win.get("on_start"):
            handle = win["on_start"](proc.pid, run_dir)
        if _sleep_checking(proc, window_s):
            return False, {**info, "error": "game exited mid-window"}
    finally:
        info["end_wall"] = time.time()
        info["end_tick"] = _max_tick(_jsonl(results_path))
        if win.get("on_end"):
            win["on_end"](handle)
    return False, info


def _sleep_checking(proc, seconds):
    """Sleep up to `seconds`; True when proc exited during the sleep."""
    end = time.time() + seconds
    while time.time() < end:
        if proc.poll() is not None:
            return True
        time.sleep(min(0.1, end - time.time()))
    return proc.poll() is not None


def _window_result(result, run_dir, info, timed_out):
    """Fill result['window'] from the metric records inside the window."""
    info = info or {}
    win = {"after_event": info.get("event"), "warmup_s": info.get("warmup_s"),
           "start_tick": info.get("start_tick"), "end_tick": info.get("end_tick"),
           "seconds": None, "frames": None, "fps": None, "prof": {}}
    if info.get("start_tick") is not None and info.get("end_tick") is not None:
        seconds = (info["end_wall"] - info["start_wall"]) if info.get("end_wall") else None
        sums = {}
        n_frames = 0
        for r in _jsonl(os.path.join(run_dir, "results.jsonl")):
            if r.get("type") != "metric":
                continue
            if not (info["start_tick"] < r.get("tick", 0) <= info["end_tick"]):
                continue
            for scope, pv in (r.get("prof") or {}).items():
                if not isinstance(pv, dict) or not pv.get("n"):
                    continue
                acc = sums.setdefault(scope, {"us": 0.0, "n": 0})
                acc["us"] += pv.get("us", 0)
                acc["n"] += pv["n"]
                if scope == "draw_frame":
                    n_frames += pv["n"]
        win["prof"] = {scope: {"us_per_call": acc["us"] / acc["n"] if acc["n"] else None,
                               "n": acc["n"]}
                       for scope, acc in sums.items()}
        win["seconds"] = round(seconds, 3) if seconds is not None else None
        win["frames"] = n_frames if sums else None
        win["fps"] = round(n_frames / seconds, 2) if n_frames and seconds else None
    if info.get("error"):
        win["error"] = info["error"]
        if not timed_out and result.get("status") in (None, "ok"):
            result["status"] = "failed"
    result["window"] = win


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
