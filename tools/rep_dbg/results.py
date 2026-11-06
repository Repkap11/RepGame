"""rep_dbg.results — the Python API over result.json / results.jsonl.

    from rep_dbg import results
    r = results.load("last")            # or a run id / run dir
    r.status, r.stats["metric.loader.light_jobs"]["max"]
    for m in r.metrics(after_settle=True): m["loader"]["light_jobs"]
    r.events("capture_shot"); r.probes(); r.artifact("log")
    results.query(name="fly-popin", status="ok")   # -> list of Run
"""

import json
import os
import re

from . import index as index_mod

DEFAULT_ROOT = "/tmp/rep-dbg"


def default_root():
    return os.environ.get("REP_DBG_ROOT", DEFAULT_ROOT)


def resolve(ref, root=None):
    """ref: 'last', a run id (<name>/<ts>-<n>), or a run dir. -> run dir."""
    root = root or default_root()
    if ref == "last":
        entries = index_mod.load(root)
        if not entries:
            entries = [index_mod.index_entry(_load_result(d), d, root)
                       for _i, d in (index_mod.iter_run_dirs(root) or [])]
        if not entries:
            raise LookupError("no runs under %s" % root)
        return os.path.join(root, entries[-1]["dir"])
    candidate = os.path.join(root, ref)
    for path in (candidate, ref):
        if os.path.isfile(os.path.join(path, "result.json")):
            return path
    raise LookupError("no such run: %s" % ref)


def _load_result(run_dir):
    with open(os.path.join(run_dir, "result.json")) as f:
        return json.load(f)


class Run:
    def __init__(self, run_dir):
        self.dir = run_dir
        self.result = _load_result(run_dir)
        self.id = self.result.get("id")
        self.status = self.result.get("status")
        self.stats = self.result.get("stats", {})
        self.asserts = self.result.get("asserts", {})

    def records(self):
        path = os.path.join(self.dir, "results.jsonl")
        recs = []
        try:
            with open(path) as f:
                for line in f:
                    line = line.strip()
                    if line:
                        recs.append(json.loads(line))
        except OSError:
            pass
        return recs

    def _settle_tick(self):
        summary = self.result.get("game") or {}
        if summary.get("settle_tick"):
            return summary["settle_tick"]
        for e in self.events("fly_settled"):
            return e.get("tick")
        return None

    def metrics(self, after_settle=None):
        recs = [r for r in self.records() if r.get("type") == "metric"]
        if after_settle:
            settle = self._settle_tick()
            if settle is not None:
                recs = [r for r in recs if r.get("tick", 0) > settle]
            else:
                recs = []
        return recs

    def events(self, name=None):
        recs = [r for r in self.records() if r.get("type") == "event"]
        if name is not None:
            recs = [r for r in recs if r.get("name") == name]
        return recs

    def probes(self):
        return [r for r in self.records() if r.get("type") == "probe"]

    def artifact(self, key):
        """Absolute path of an artifact, or None."""
        artifacts = self.result.get("artifacts", {})
        if key in artifacts:
            val = artifacts[key]
            if isinstance(val, dict):
                val = val.get("data")
            if isinstance(val, list):
                val = val[0] if val else None
            if val:
                return os.path.join(self.dir, val)
        candidate = os.path.join(self.dir, key)
        return candidate if os.path.exists(candidate) else None

    def field(self, dotted):
        val = self.result
        parts = dotted.split(".")
        i = 0
        while i < len(parts):
            if isinstance(val, list):
                val = val[int(parts[i])]
                i += 1
                continue
            # Stat keys are themselves dotted ("metric.loader.light_jobs"),
            # so join segments until a key matches.
            for j in range(len(parts), i, -1):
                key = ".".join(parts[i:j])
                if key in val:
                    val = val[key]
                    i = j
                    break
            else:
                raise KeyError(dotted)
        return val


def load(ref="last", root=None):
    return Run(resolve(ref, root))


def query(root=None, name=None, status=None, git=None, since=None, last=None):
    root = root or default_root()
    entries = index_mod.load(root)
    if not entries:
        entries = []
        for _id, run_dir in index_mod.iter_run_dirs(root) or []:
            try:
                entries.append(index_mod.index_entry(_load_result(run_dir), run_dir, root))
            except (OSError, json.JSONDecodeError):
                continue
    if since:
        cutoff = _parse_since(since)
        entries = [e for e in entries if (e.get("started") or "") >= cutoff]
    for key, want in (("name", name), ("status", status), ("git", git)):
        if want:
            entries = [e for e in entries if str(e.get(key) or "").startswith(want)]
    if last:
        entries = entries[-last:]
    runs = []
    for e in entries:
        try:
            runs.append(Run(os.path.join(root, e["dir"])))
        except (OSError, json.JSONDecodeError, KeyError):
            continue
    return runs


def _parse_since(spec):
    """'2h'/'30m'/'3d' -> an ISO timestamp string cutoff."""
    import datetime
    m = re.match(r"^(\d+)([smhd])$", spec)
    if not m:
        raise ValueError("bad --since spec %r (want e.g. 30m, 2h, 3d)" % spec)
    secs = {"s": 1, "m": 60, "h": 3600, "d": 86400}[m.group(2)] * int(m.group(1))
    cutoff = datetime.datetime.now() - datetime.timedelta(seconds=secs)
    return cutoff.strftime("%Y-%m-%dT%H:%M:%S")
