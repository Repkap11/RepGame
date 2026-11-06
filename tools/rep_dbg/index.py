"""The run index: <root>/index.jsonl, one line per finished run.
Append-only; rebuildable by scanning result.json files."""

import json
import os


def index_path(root):
    return os.path.join(root, "index.jsonl")


def append(root, entry):
    os.makedirs(root, exist_ok=True)
    with open(index_path(root), "a") as f:
        f.write(json.dumps(entry) + "\n")


def load(root):
    entries = []
    try:
        with open(index_path(root)) as f:
            for line in f:
                line = line.strip()
                if line:
                    entries.append(json.loads(line))
    except OSError:
        pass
    return entries


def iter_run_dirs(root):
    """Yield every dir holding a result.json (id = path relative to root)."""
    if not os.path.isdir(root):
        return
    for name in sorted(os.listdir(root)):
        if name == "wt":
            continue
        parent = os.path.join(root, name)
        if not os.path.isdir(parent):
            continue
        for sub in sorted(os.listdir(parent)):
            run_dir = os.path.join(parent, sub)
            if os.path.isfile(os.path.join(run_dir, "result.json")):
                yield name + "/" + sub, run_dir


def index_entry(result, run_dir, root):
    """The one-line index record for a finished run."""
    stats = result.get("stats", {})
    keys = {}
    for field in ("metric.loader.light_jobs", "metric.loader.light_seeds"):
        if field in stats and stats[field]:
            keys[field + ".mean"] = stats[field].get("mean")
    entry = {
        "id": result.get("id"),
        "name": result.get("name"),
        "kind": result.get("kind"),
        "status": result.get("status"),
        "git": result.get("git", {}).get("sha"),
        "build": result.get("build"),
        "wall_s": result.get("wall_s"),
        "started": result.get("started"),
        "dir": os.path.relpath(run_dir, root),
        "stats": keys,
    }
    for key in ("ab_id", "cfg"):
        if result.get(key) is not None:
            entry[key] = result[key]
    return entry


def reindex(root):
    entries = []
    for _rel, run_dir in iter_run_dirs(root):
        try:
            with open(os.path.join(run_dir, "result.json")) as f:
                result = json.load(f)
        except (OSError, json.JSONDecodeError):
            continue
        entries.append(index_entry(result, run_dir, root))
    with open(index_path(root), "w") as f:
        for e in entries:
            f.write(json.dumps(e) + "\n")
    return entries
