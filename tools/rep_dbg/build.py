"""Building and locating the RepGame binary, plus git info."""

import os
import subprocess

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

BUILD_DIRS = {"release": "release", "debug": "debug", "asan": "asan", "tsan": "tsan"}
MAKE_TARGETS = {"release": "linux", "debug": "linux_debug", "asan": "linux_asan", "tsan": "linux_tsan"}


def git(tree, *args):
    out = subprocess.run(["git", "-C", tree] + list(args),
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if out.returncode != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), out.stderr.strip()))
    return out.stdout.strip()


def git_info(tree, rev_arg=None):
    sha = git(tree, "rev-parse", rev_arg) if rev_arg else git(tree, "rev-parse", "HEAD")
    dirty = bool(git(tree, "status", "--porcelain"))
    return {"sha": sha, "dirty": dirty, "rev_arg": rev_arg}


def binary_path(tree, build):
    return os.path.join(tree, "out", "linux", BUILD_DIRS[build], "RepGame")


def ensure_built(tree, build, log):
    """Run the make wrapper in `tree`; returns the binary path or None."""
    if subprocess.call(["make", MAKE_TARGETS[build]], cwd=tree, stdout=log, stderr=subprocess.STDOUT) != 0:
        return None
    path = binary_path(tree, build)
    return path if os.path.isfile(path) else None
