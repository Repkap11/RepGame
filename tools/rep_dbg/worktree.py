"""--rev worktrees: cached checkouts at <root>/wt/<sha>, built in place.
Only that directory is ever touched, and removal goes through
`git worktree remove`."""

import os
import subprocess

from . import build as build_mod

# Untracked build dependencies a fresh checkout needs (see `make install`);
# symlinked from the main tree like /tmp/repgame-temp was set up.
# sounds/ deliberately absent: REPGAME_AUDIO defaults to 0 without it, so
# worktree builds don't need the converted .ogg assets.
SYMLINK_DIRS = ["imgui_build", "entt_build"]


def wt_root(root):
    return os.path.join(root, "wt")


def ensure(root, sha, log):
    """Checkout `sha` detached at <root>/wt/<sha>; returns the worktree path."""
    dest = os.path.join(wt_root(root), sha)
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


def list(root):
    base = wt_root(root)
    if not os.path.isdir(base):
        return []
    return sorted(d for d in os.listdir(base) if os.path.isdir(os.path.join(base, d)))


def prune(root, log):
    removed = []
    for sha in list(root):
        dest = os.path.join(wt_root(root), sha)
        print("rep-dbg: removing worktree %s" % dest, file=log)
        subprocess.call(["git", "-C", build_mod.REPO, "worktree", "remove", "--force", dest])
        if not os.path.exists(dest):
            removed.append(sha)
    return removed
