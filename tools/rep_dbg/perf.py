"""rep-dbg perf: preflight, `perf report`/`perf stat`/`perf script` parsing,
perf diff, and post-processing of record/attach run dirs.

Preflight reads /proc/sys/kernel/perf_event_paranoid (>1 refuses, exit 4)
and kptr_restrict (warn only). REP_DBG_PROC_SYS can point at a fake
/proc/sys for tests. perf is never invoked via sudo.
"""

import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time

STAT_EVENTS = ("cycles,instructions,cache-references,cache-misses,branches,"
               "branch-misses,context-switches,cpu-migrations,task-clock")


def _proc_sys():
    return os.environ.get("REP_DBG_PROC_SYS") or "/proc/sys"


def _read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def preflight(log=sys.stderr):
    """True when perf can record. Refuses (with the sysctl hint) when
    perf_event_paranoid > 1; kptr_restrict only warns."""
    if shutil.which("perf") is None:
        print("rep-dbg: perf not found on PATH", file=log)
        return False
    val = _read(os.path.join(_proc_sys(), "kernel/perf_event_paranoid"))
    if val is not None and int(val) > 1:
        print("rep-dbg: kernel.perf_event_paranoid is %s; perf recording needs <=1" % val, file=log)
        print("  sudo sysctl kernel.perf_event_paranoid=1", file=log)
        print("  echo 'kernel.perf_event_paranoid=1' | sudo tee /etc/sysctl.d/99-perf.conf", file=log)
        return False
    kptr = _read(os.path.join(_proc_sys(), "kernel/kptr_restrict"))
    if kptr is not None and kptr != "0":
        print("rep-dbg: warning: kernel.kptr_restrict=%s — kernel symbols show as [unknown]" % kptr,
              file=log)
    return True


def record_wrap(run_freq, call_graph):
    """argv prefix for `perf record`; '{run_dir}' is filled by runner."""
    cg = "dwarf,16384" if call_graph == "dwarf" else call_graph
    return ["perf", "record", "-F", str(run_freq),
            "--call-graph", cg, "-o", "{run_dir}/perf.data", "--"]


def stat_wrap(events):
    return ["perf", "stat", "-x,", "-o", "{run_dir}/perf_stat.csv",
            "-e", events or STAT_EVENTS, "--"]


_REPORT_ROW = re.compile(r"^\s*([0-9.]+)%\s+(\S+)\s+(.*\S)\s*$")


def parse_report(text):
    """`perf report --stdio --sort comm,sym` -> [{symbol,self_pct,thread}]."""
    rows = []
    for line in text.splitlines():
        m = _REPORT_ROW.match(line)
        if not m:
            continue
        rows.append({"self_pct": float(m.group(1)), "thread": m.group(2),
                     "symbol": m.group(3)})
    return rows


def parse_stat_csv(text):
    """`perf stat -x,` output -> the perf.stat dict (nulls for uncounted)."""
    raw = {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) < 3:
            continue
        value, _unit, event = parts[0], parts[1], parts[2]
        if "not supported" in value or "not counted" in value:
            raw[event] = None
        else:
            try:
                raw[event] = float(value)
            except ValueError:
                raw[event] = None
    stat = dict(raw)
    cyc, ins = raw.get("cycles"), raw.get("instructions")
    stat["ipc"] = ins / cyc if cyc and ins else None
    refs, miss = raw.get("cache-references"), raw.get("cache-misses")
    stat["cache_miss_rate"] = miss / refs if refs and miss is not None else None
    if raw.get("task-clock") is not None:
        stat["task_clock_ms"] = raw["task-clock"]
    return stat


def top_diff(top_a, top_b, n=15):
    """Biggest self_pct deltas per symbol, both directions."""
    def key(r):
        return (r.get("symbol"), r.get("thread"))
    a = {key(r): r["self_pct"] for r in top_a}
    b = {key(r): r["self_pct"] for r in top_b}
    rows = []
    for k in set(a) | set(b):
        rows.append({"symbol": k[0], "thread": k[1],
                     "a_pct": a.get(k, 0.0), "b_pct": b.get(k, 0.0),
                     "delta": b.get(k, 0.0) - a.get(k, 0.0)})
    rows.sort(key=lambda r: abs(r["delta"]), reverse=True)
    return rows[:n]


def postprocess(run_dir, result, opts, log=sys.stderr):
    """After a record/stat run: top.txt, folded.txt, flame.svg, perf.stat."""
    from . import flame as flame_mod
    data = os.path.join(run_dir, "perf.data")
    stat_csv = os.path.join(run_dir, "perf_stat.csv")
    artifacts = {}
    if os.path.exists(stat_csv) and os.path.getsize(stat_csv):
        with open(stat_csv, errors="replace") as f:
            result.setdefault("perf", {})["stat"] = parse_stat_csv(f.read())
        artifacts["stat"] = "perf_stat.csv"
    if not os.path.exists(data) or not os.path.getsize(data):
        if "stat" not in result.get("perf", {}):
            print("rep-dbg: no perf.data produced", file=log)
        else:
            result.setdefault("artifacts", {}).setdefault("perf", artifacts)
        return

    size = os.path.getsize(data)
    result.setdefault("perf", {})["data_size_bytes"] = size
    print("rep-dbg: perf.data %.1f MB" % (size / 1e6), file=log)
    artifacts["data"] = "perf.data"

    rep = subprocess.run(["perf", "report", "--stdio", "--no-children",
                          "--sort", "sym", "-i", data],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    rep_thread = subprocess.run(["perf", "report", "--stdio", "--no-children",
                                 "--sort", "comm,sym", "-i", data],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    rows = parse_report(rep_thread.stdout)
    result["perf"]["top"] = rows[:40]
    top_lines = [l for l in rep.stdout.splitlines() if _REPORT_ROW.match(l)][:40]
    with open(os.path.join(run_dir, "top.txt"), "w") as f:
        f.write("# top 40 by self time\n" + "\n".join(top_lines) + "\n\n# per-thread\n")
        f.write(rep_thread.stdout)
    artifacts["top"] = "top.txt"

    scr = subprocess.run(["perf", "script", "-i", data],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    collapsed, by_thread = flame_mod.collapse(scr.stdout)
    with open(os.path.join(run_dir, "folded.txt"), "w") as f:
        for stack, w in sorted(collapsed.items()):
            f.write("%s %d\n" % (stack, w))
    artifacts["folded"] = "folded.txt"
    flame_mod.write_svg(collapsed, os.path.join(run_dir, "flame.svg"),
                        title=result.get("id", "flame"))
    artifacts["flame"] = "flame.svg"
    if getattr(opts, "threads", False):
        flames = {}
        for comm, coll in sorted(by_thread.items()):
            name = "flame-%s.svg" % re.sub(r"[^A-Za-z0-9_.-]", "_", comm)
            flame_mod.write_svg(coll, os.path.join(run_dir, name), title=comm)
            flames[comm] = name
        artifacts["flames_by_thread"] = flames
    result.setdefault("artifacts", {}).setdefault("perf", artifacts)


def attach(pid, duration, freq, call_graph, threads, run_dir, run_id, result, log):
    """perf record -p <pid> for `duration` seconds, stopped via SIGINT."""
    cg = "dwarf,16384" if call_graph == "dwarf" else call_graph
    argv = ["perf", "record", "-F", str(freq), "--call-graph", cg,
            "-p", str(pid), "-o", os.path.join(run_dir, "perf.data")]
    with open(os.path.join(run_dir, "stdout.log"), "w") as logf:
        proc = subprocess.Popen(argv, stdout=logf, stderr=subprocess.STDOUT)
        time.sleep(duration)
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    data = os.path.join(run_dir, "perf.data")
    result["status"] = "ok" if os.path.exists(data) and os.path.getsize(data) else "failed"
    return result


def resolve_pid(name):
    """'RepGame' or a number -> pid. Errors on 0 or >1 pgrep matches."""
    try:
        return int(name), name
    except ValueError:
        pass
    out = subprocess.run(["pgrep", "-x", name], stdout=subprocess.PIPE, text=True)
    pids = [int(p) for p in out.stdout.split()]
    if len(pids) == 1:
        return pids[0], name
    raise ValueError("pgrep -x %s matched %d pids: %s" %
                     (name, len(pids), " ".join(map(str, pids)) or "none"))
