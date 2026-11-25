"""rep-dbg gc: reclaim the run root and mp-test artifact scratch.

Rules (each printed as it happens):
  (a) kept perf.data older than --perf-data-hours: delete the file, mark
      result.json perf.data_deleted and drop artifacts.perf.data.
  (b) run dirs older than --keep-days: rmtree.
  (c) total run-dir size over --max-gb: delete oldest dirs first (never
      younger than 1 h) until under the cap.
  (d) worktrees older than --wt-days by last use (worktree.py).
  (e) mp-test artifact dirs under <TMPDIR>/repgame/mptest_* older than
      --mptest-keep-days or over --mptest-max-gb (active dirs excluded;
      newest completed dir preserved from the cap).

Safety: only deletes paths whose realpath is inside the realpath of root;
refuses root = "/", $HOME or the repo; rmtree never descends into
symlinks (worktrees symlink imgui_build/entt_build into the repo).
"""

import datetime
import json
import os
import re
import shutil
import sys
import tempfile
import time

from . import build as build_mod
from . import index as index_mod
from . import worktree as worktree_mod

STAMP = ".gc-stamp"
RUN_DIR_RE = re.compile(r"^\d{8}-\d{6}-\d+$")
MPTEST_PREFIX = "mptest_"
MPTEST_ACTIVE = ".active"
MPTEST_AUTO_ENV = {"mptest_keep_days": "REP_DBG_MPTEST_KEEP_DAYS",
                   "mptest_max_gb": "REP_DBG_MPTEST_MAX_GB"}
AUTO_ENV = {"keep_days": "REP_DBG_KEEP_DAYS", "max_gb": "REP_DBG_MAX_GB",
            "perf_data_hours": "REP_DBG_PERF_DATA_HOURS",
            "wt_days": "REP_DBG_WT_DAYS"}
AUTO_ENV.update(MPTEST_AUTO_ENV)
DEFAULTS = {"keep_days": 7.0, "max_gb": 10.0, "perf_data_hours": 24.0,
            "wt_days": 3.0, "mptest_keep_days": 2.0,
            "mptest_max_gb": 2.0}


def _check_root(root):
    real = os.path.realpath(root)
    bad = {os.path.realpath("/"), os.path.realpath(os.path.expanduser("~")),
           os.path.realpath(build_mod.REPO)}
    if real in bad:
        raise ValueError("refusing to gc dangerous root %r" % root)
    return real


def _inside(root_real, path):
    return os.path.realpath(path).startswith(root_real + os.sep)


def _dir_size(path):
    total = 0
    for dirpath, _dirnames, filenames in os.walk(path):
        for fn in filenames:
            p = os.path.join(dirpath, fn)
            try:
                total += os.lstat(p).st_size
            except OSError:
                pass
    return total


def _rmtree_linksafe(path):
    """Remove a tree without ever descending into symlinks (worktrees
    symlink imgui_build/entt_build into the main repo)."""
    for dirpath, dirnames, filenames in os.walk(path, topdown=False):
        for fn in filenames:
            os.unlink(os.path.join(dirpath, fn))
        for dn in dirnames:
            p = os.path.join(dirpath, dn)
            if os.path.islink(p):
                os.unlink(p)
            else:
                os.rmdir(p)
    os.rmdir(path)


def _iter_run_dirs(root):
    """(run_dir, mtime) for every <root>/<name>/<YYYYmmdd-HHMMSS-n> dir."""
    if not os.path.isdir(root):
        return
    for name in sorted(os.listdir(root)):
        parent = os.path.join(root, name)
        if name == "wt" or not os.path.isdir(parent):
            continue
        for sub in sorted(os.listdir(parent)):
            run_dir = os.path.join(parent, sub)
            if RUN_DIR_RE.match(sub) and os.path.isdir(run_dir) and \
                    not os.path.islink(run_dir):
                yield run_dir, os.path.getmtime(run_dir)


def mptest_scratch():
    return os.path.join(tempfile.gettempdir(), "repgame")


def mptest_env_kwargs():
    out = {}
    for key, env in MPTEST_AUTO_ENV.items():
        if os.environ.get(env):
            out[key] = float(os.environ[env])
    return out


def _tree_mtime(path):
    latest = os.path.getmtime(path)
    for dirpath, _dirnames, filenames in os.walk(path):
        for fn in filenames:
            try:
                latest = max(latest, os.lstat(os.path.join(dirpath, fn)).st_mtime)
            except OSError:
                pass
    return latest


def _mptest_active(path):
    try:
        with open(os.path.join(path, MPTEST_ACTIVE)) as f:
            pid = int(f.read().strip())
    except (OSError, ValueError):
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _iter_mptest_dirs(scratch):
    if not os.path.isdir(scratch):
        return
    for name in sorted(os.listdir(scratch)):
        path = os.path.join(scratch, name)
        if name.startswith(MPTEST_PREFIX) and os.path.isdir(path) and \
                not os.path.islink(path):
            yield path, _tree_mtime(path), _mptest_active(path)


def gc_mptest(scratch=None, dry_run=False, keep_days=None, max_gb=None,
              log=sys.stderr):
    """Reclaim retained mp-test artifact dirs. Active artifact dirs are
    never touched. Age rules apply to completed dirs; the size cap deletes
    oldest completed dirs first while always preserving the newest one."""
    scratch = mptest_scratch() if scratch is None else scratch
    scratch_real = _check_root(scratch)
    keep_days = DEFAULTS["mptest_keep_days"] if keep_days is None else keep_days
    max_gb = DEFAULTS["mptest_max_gb"] if max_gb is None else max_gb
    now = time.time()
    freed = 0
    removed = []

    remaining = []
    for path, mtime, active in _iter_mptest_dirs(scratch):
        if not _inside(scratch_real, path):
            continue
        if active or now - mtime <= keep_days * 86400:
            remaining.append((path, mtime, active))
            continue
        size = _dir_size(path)
        print("gc: old mp-test artifacts (%.0f MB)  %s" % (size / 1e6, path), file=log)
        freed += size
        removed.append(path)
        if not dry_run:
            _rmtree_linksafe(path)

    newest_inactive = None
    inactive = [(m, d) for d, m, active in remaining if not active]
    if inactive:
        newest_inactive = max(inactive)[1]
    cap = max_gb * 1e9
    sizes = sorted(((_dir_size(d), m, d) for d, m, active in remaining if not active),
                   key=lambda t: t[1])
    total = sum(s for s, _m, _d in sizes)
    for size, mtime, path in sizes:
        if total <= cap or path == newest_inactive:
            continue
        print("gc: mp-test over cap, removing (%.0f MB)  %s" % (size / 1e6, path), file=log)
        total -= size
        freed += size
        removed.append(path)
        if not dry_run:
            _rmtree_linksafe(path)
    return {"freed_bytes": freed, "removed": removed}


def _perf_data_sweep(root, max_age_s, dry_run, log):
    freed = 0
    now = time.time()
    for _rel, run_dir in index_mod.iter_run_dirs(root):
        data = os.path.join(run_dir, "perf.data")
        if not os.path.isfile(data) or os.path.islink(data):
            continue
        if now - os.path.getmtime(data) < max_age_s:
            continue
        size = os.path.getsize(data)
        print("gc: perf.data %.1f MB  %s" % (size / 1e6, data), file=log)
        freed += size
        if not dry_run:
            os.unlink(data)
            rj = os.path.join(run_dir, "result.json")
            try:
                with open(rj) as f:
                    result = json.load(f)
                result.setdefault("perf", {})["data_deleted"] = True
                (result.get("artifacts", {}).get("perf") or {}).pop("data", None)
                with open(rj, "w") as f:
                    json.dump(result, f, indent=2, sort_keys=True)
            except (OSError, json.JSONDecodeError):
                pass
    return freed


def gc(root, dry_run=False, keep_days=None, max_gb=None, perf_data_hours=None,
       wt_days=None, mptest_keep_days=None, mptest_max_gb=None,
       mptest_scratch=None, log=sys.stderr):
    """Run all rules. Returns {freed_bytes, removed:[...]}."""
    root_real = _check_root(root)
    keep_days = DEFAULTS["keep_days"] if keep_days is None else keep_days
    max_gb = DEFAULTS["max_gb"] if max_gb is None else max_gb
    perf_h = DEFAULTS["perf_data_hours"] if perf_data_hours is None else perf_data_hours
    wt_days = DEFAULTS["wt_days"] if wt_days is None else wt_days
    now = time.time()
    freed = 0
    removed = []

    freed += _perf_data_sweep(root, perf_h * 3600, dry_run, log)

    run_dirs = [(d, m) for d, m in _iter_run_dirs(root)
                if _inside(root_real, d)]
    for run_dir, mtime in run_dirs:
        if now - mtime > keep_days * 86400:
            size = _dir_size(run_dir)
            print("gc: old run dir (%.0f MB)  %s" % (size / 1e6, run_dir), file=log)
            freed += size
            removed.append(run_dir)
            if not dry_run:
                _rmtree_linksafe(run_dir)
    run_dirs = [(d, m) for d, m in run_dirs if not dry_run and os.path.isdir(d)
                or dry_run and d not in removed]

    cap = max_gb * 1e9
    sizes = sorted((( _dir_size(d), m, d) for d, m in run_dirs), key=lambda t: t[1])
    total = sum(s for s, _m, _d in sizes)
    for size, mtime, run_dir in sizes:
        if total <= cap or now - mtime < 3600:
            continue
        print("gc: over cap, removing (%.0f MB)  %s" % (size / 1e6, run_dir), file=log)
        total -= size
        freed += size
        removed.append(run_dir)
        if not dry_run:
            _rmtree_linksafe(run_dir)

    freed += worktree_mod.prune(root, log, older_than_days=wt_days,
                                dry_run=dry_run)

    if mptest_scratch is not None:
        mp_out = gc_mptest(mptest_scratch, dry_run=dry_run,
                           keep_days=mptest_keep_days,
                           max_gb=mptest_max_gb, log=log)
        freed += mp_out["freed_bytes"]
        removed.extend(mp_out["removed"])

    if removed or freed:
        print("gc: freed %.1f MB total" % (freed / 1e6), file=log)
        if not dry_run:
            index_mod.reindex(root)
    return {"freed_bytes": freed, "removed": removed}


def auto(root, log=sys.stderr):
    """Hourly gc for run-producing commands. No-op when REP_DBG_AUTO_GC=0;
    prints one line only when it removed something."""
    if os.environ.get("REP_DBG_AUTO_GC") == "0":
        return
    try:
        root_real = _check_root(root)
    except ValueError:
        return
    stamp = os.path.join(root_real, STAMP)
    try:
        if time.time() - os.path.getmtime(stamp) < 3600:
            return
    except OSError:
        pass
    try:
        kwargs = {}
        for key, env in AUTO_ENV.items():
            if os.environ.get(env):
                kwargs[key] = float(os.environ[env])
        import io
        buf = io.StringIO()
        out = gc(root, log=buf, mptest_scratch=mptest_scratch(), **kwargs)
        if out["freed_bytes"]:
            print("rep-dbg: auto-gc freed %.1f MB" % (out["freed_bytes"] / 1e6), file=log)
        os.makedirs(root_real, exist_ok=True)
        with open(stamp, "w") as f:
            f.write(datetime.datetime.now().isoformat())
    except Exception:
        pass
