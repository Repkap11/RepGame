"""rep-dbg ab: interleaved A/B/C reps across cfgs.

A cfg spec is `[rev:<ref>|worktree:<path>|current][:K=V[:K=V...]]` — the part
before the first ':' picks the tree (current tree, a --rev worktree, or an
explicit worktree path); the rest is env pairs applied on top of the
scenario env. Each rep is a normal indexed run (kind "ab-rep"); the ab run
itself writes a result.json (kind "ab") with per-cfg aggregates per
metric and a noise flag, plus summary.md.
"""

import math
import os

from . import build as build_mod
from . import runner as runner_mod
from . import worktree as worktree_mod

DEFAULT_METRICS = [
    "wall_s",
    "game.settle_tick",
    "stats.metric.loader.light_jobs.mean",
    "stats.metric.loader.light_pending.max",
    "stats.metric.loader.chunks_loading.mean",
]


def parse_spec(spec):
    """'rev:HEAD~3:K=V' -> ('rev:HEAD~3', {K:V}). Env pairs are the trailing
    ':'-separated segments that contain '='; the rest is the tree spec."""
    parts = spec.split(":")
    env_start = len(parts)
    while env_start > 1 and "=" in parts[env_start - 1]:
        env_start -= 1
    tree = ":".join(parts[:env_start])
    env = {}
    for kv in parts[env_start:]:
        k, _, v = kv.partition("=")
        env[k] = v
    if not (tree == "current" or tree.startswith("rev:") or tree.startswith("worktree:")):
        raise ValueError("bad cfg spec %r (want current|rev:<ref>|worktree:<path>[:K=V[:K=V...]])" % spec)
    return tree, env


def resolve_tree(tree_spec, root, log):
    """cfg tree spec -> a buildable directory."""
    if tree_spec == "current":
        return build_mod.REPO
    if tree_spec.startswith("rev:"):
        sha = build_mod.git(build_mod.REPO, "rev-parse", tree_spec[4:])
        return worktree_mod.ensure(root, sha, log)
    if tree_spec.startswith("worktree:"):
        path = tree_spec[len("worktree:"):]
        if not os.path.isdir(path):
            raise ValueError("worktree cfg: no such dir %s" % path)
        return path
    raise ValueError("bad cfg tree %r (want current|rev:<ref>|worktree:<path>)" % tree_spec)


def interleave_order(cfg_names, reps):
    """rep-major, cfg-minor: rep1 A,B,C; rep2 A,B,C ..."""
    return [(rep, name) for rep in range(reps) for name in cfg_names]


def aggregate(values):
    """median/mean/stddev/min/max across reps."""
    vals = sorted(values)
    n = len(vals)
    mean = sum(vals) / n
    stddev = math.sqrt(sum((v - mean) ** 2 for v in vals) / n) if n > 1 else 0.0
    median = vals[n // 2] if n % 2 else (vals[n // 2 - 1] + vals[n // 2]) / 2
    return {"median": median, "mean": mean, "stddev": stddev,
            "min": vals[0], "max": vals[-1], "n": n}


def noise_flag(per_cfg):
    """'noisy' when any cfg's stddev/|mean| exceeds 10%, or when the
    difference between the two most-separated medians is smaller than the
    larger cfg's (max-min) spread."""
    for agg in per_cfg.values():
        if agg["mean"] != 0 and agg["stddev"] / abs(agg["mean"]) > 0.10:
            return True
    aggs = sorted((a for a in per_cfg.values()), key=lambda a: a["median"])
    if len(aggs) >= 2:
        spread = max(aggs[0]["max"] - aggs[0]["min"], aggs[-1]["max"] - aggs[-1]["min"])
        if aggs[-1]["median"] - aggs[0]["median"] < spread:
            return True
    return False


def metric_table(per_metric):
    """per_metric: {field: {cfg: agg}} — attach the noisy flag per field."""
    out = {}
    for field, per_cfg in per_metric.items():
        out[field] = {"cfgs": per_cfg, "noisy": noise_flag(per_cfg)}
    return out


def summary_md(result):
    """Markdown table: rows = metrics, cols = cfgs (median±stddev,
    delta-vs-first-cfg %, noisy marker)."""
    cfgs = result["cfg_order"]
    lines = ["| metric | " + " | ".join(cfgs) + " |",
             "|---|" + "|".join("---" for _ in cfgs) + "|"]
    for field, entry in result["metrics"].items():
        med0 = entry["cfgs"][cfgs[0]]["median"]
        cells = []
        for cfg in cfgs:
            agg = entry["cfgs"][cfg]
            cell = "%.4g±%.2g" % (agg["median"], agg["stddev"])
            if cfg != cfgs[0] and med0 != 0:
                cell += " (%+.1f%%)" % ((agg["median"] - med0) / abs(med0) * 100)
            cells.append(cell)
        name = field + (" **noisy**" if entry["noisy"] else "")
        lines.append("| " + name + " | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"
