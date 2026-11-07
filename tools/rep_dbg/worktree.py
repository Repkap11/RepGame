"""--rev worktrees: cached checkouts at <root>/wt/<sha>, built in place.

Last use is tracked by touching <root>/wt/.used/<sha> on every ensure()
(create and reuse) — nothing inside the checkout is dirtied. Removal goes
through `git worktree remove --force` for registered worktrees and a
symlink-safe rmtree for orphans (checkouts symlink imgui_build/entt_build
into the main repo, so rmtree must never descend into them).
"""

import os
import subprocess
import time

from . import build as build_mod

# Untracked build dependencies a fresh checkout needs (see `make install`);
# symlinked from the main tree like /tmp/repgame-temp was set up.
# sounds/ deliberately absent: REPGAME_AUDIO defaults to 0 without it, so
# worktree builds don't need the converted .ogg assets.
SYMLINK_DIRS = ["imgui_build", "entt_build"]


def wt_root(root):
    return os.path.join(root, "wt")


def _used_dir(root):
    return os.path.join(wt_root(root), ".used")


def touch_used(root, sha):
    try:
        os.makedirs(_used_dir(root), exist_ok=True)
        path = os.path.join(_used_dir(root), sha)
        with open(path, "w") as f:
            f.write(sha)
    except OSError:
        pass


def _last_used(root, path):
    sha = os.path.basename(path.rstrip("/"))
    stamp = os.path.join(_used_dir(root), sha)
    try:
        return os.path.getmtime(stamp)
    except OSError:
        pass
    try:
        return os.path.getmtime(path)
    except OSError:
        return 0


def ensure(root, sha, log):
    """Checkout `sha` detached at <root>/wt/<sha>; returns the worktree path."""
    dest = os.path.join(wt_root(root), sha)
    touch_used(root, sha)
    if os.path.isdir(dest):
        return dest
    os.makedirs(wt_root(root), exist_ok=True)
    print("rep-dbg: creating worktree %s" % dest, file=log)
    subprocess.check_call(["git", "-C", build_mod.REPO, "worktree", "add", "--detach", dest, sha])
    for d in SYMLINK_DIRS:
        src = os.path.join(build_mod.REPO, d)
        dst = os.path.join(dest, d)
        if os.path.exists(src) and not os.path.exists(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            os.symlink(src, dst)
    return dest


def _registered(root):
    """git worktree list --porcelain -> {path: head_sha} for paths in root."""
    out = subprocess.run(["git", "-C", build_mod.REPO, "worktree", "list", "--porcelain"],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    root_real = os.path.realpath(root)
    entries = {}
    path = None
    for line in out.stdout.splitlines():
        if line.startswith("worktree "):
            path = line[len("worktree "):]
        elif line.startswith("HEAD ") and path:
            if os.path.realpath(path).startswith(root_real + os.sep):
                entries[path] = line[len("HEAD "):]
            path = None
    return entries


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


def managed(root):
    """All worktrees rep-dbg may prune: git-registered worktrees inside
    root (wt/<sha> and hand-made ones like wt-loaderstats-fix) plus
    unregistered orphan dirs under <root>/wt."""
    reg = _registered(root)
    out = {}
    for path, head in reg.items():
        out[path] = {"path": path, "head": head, "registered": True,
                     "last_used": _last_used(root, path),
                     "size": _dir_size(path), "dirty": _is_dirty(path)}
    wt = wt_root(root)
    if os.path.isdir(wt):
        for name in sorted(os.listdir(wt)):
            if name == ".used":
                continue
            path = os.path.join(wt, name)
            if os.path.isdir(path) and not os.path.islink(path) and path not in out:
                out[path] = {"path": path, "head": name, "registered": False,
                             "last_used": _last_used(root, path),
                             "size": _dir_size(path), "dirty": _is_dirty(path)}
    return out


def _is_dirty(path):
    out = subprocess.run(["git", "-C", path, "status", "--porcelain"],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    return bool(out.stdout.strip())


def list(root):
    """`wt list` rows, sorted by last use."""
    return sorted(managed(root).values(), key=lambda e: e["last_used"])


def _rmtree_linksafe(path):
    """Remove a tree without ever descending into symlinks (checkouts
    symlink imgui_build/entt_build into the main repo)."""
    import shutil
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


def prune(root, log, older_than_days=None, all=False, dry_run=False):
    """Remove stale worktrees; returns freed bytes (estimated).

    Default: entries whose last use (or dir mtime) is older than
    `older_than_days`. --all removes everything. Registered worktrees go
    via `git worktree remove --force`; orphans via a symlink-safe rmtree;
    then `git worktree prune` drops stale registrations."""
    entries = managed(root)
    now = time.time()
    wt_real = os.path.realpath(wt_root(root))
    freed = 0
    for path, e in sorted(entries.items()):
        if not all and older_than_days is not None and \
                now - e["last_used"] <= older_than_days * 86400:
            continue
        if not all and older_than_days is None:
            continue
        # Age-based prune keeps dirty hand-made worktrees (e.g.
        # wt-loaderstats-fix outside <root>/wt/) — they may hold
        # uncommitted work. --all still removes them.
        if not all and e["dirty"] and \
                not os.path.realpath(path).startswith(wt_real + os.sep):
            print("rep-dbg: keeping dirty worktree %s (use --all)" % path,
                  file=log)
            continue
        print("rep-dbg: removing worktree %s (%.0f MB%s)"
              % (path, e["size"] / 1e6,
                 ", unregistered" if not e["registered"] else ""), file=log)
        freed += e["size"]
        if dry_run:
            continue
        if e["registered"]:
            subprocess.call(["git", "-C", build_mod.REPO, "worktree",
                             "remove", "--force", path])
            if os.path.exists(path):
                _rmtree_linksafe(path)
        else:
            _rmtree_linksafe(path)
    if not dry_run:
        subprocess.call(["git", "-C", build_mod.REPO, "worktree", "prune"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return freed


def prune_all(root, log):
    return prune(root, log, all=True)
