"""Compatibility shim: turns a pre-results stdout.log (FLYMET lines, the
fly settle line, capture progress lines) into synthetic results.jsonl
records so `--rev` runs of old commits flow through the same result.json /
stats pipeline. result.json marks these "source":"log-shim"."""

import json
import re

FLYMET_RE = re.compile(
    r"FLYMET t=(\d+) ph=(\d+) rd=(\d+) p=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
    r"cl=(\d+) j=(\d+) p=(\d+) c=(\d+) d=(\d+) x=(\d+) bfs=(\d+) pri=(\d+) "
    r"stv=(\d+) s=(\d+) stl=(\d+) occ=(\d+)/(\d+) \| (.*)$"
)
FLY_SETTLED_RE = re.compile(r"flight begins at tick (\d+)")
CAPTURE_SHOT_RE = re.compile(r"Capture view (\d+): shot pending after (\d+) ticks( \(TIMEOUT.*)?")


def _light_json(dbg):
    """Same k=v parsing as results::light_json_from_dbg."""
    out = {}
    prefix = ""
    for tok in re.split(r"[\s()\[\]|]+", dbg):
        if tok == "us:":
            prefix = "us_"
            continue
        if "=" not in tok:
            continue
        key, _, val = tok.partition("=")
        if not key or prefix + key in out:
            continue
        try:
            out[prefix + key] = int(val)
        except ValueError:
            try:
                out[prefix + key] = float(val)
            except ValueError:
                out[prefix + key] = val
    return out


def parse_lines(lines, git=""):
    """log text -> list of synthetic results.jsonl records (no summary —
    the runner synthesizes it from the exit status)."""
    records = [{"type": "header", "schema": 1, "source": "log-shim", "git": git}]
    for line in lines:
        m = FLYMET_RE.search(line)
        if m:
            g = m.groups()
            records.append({
                "type": "metric",
                "tick": int(g[0]),
                "fly_phase": int(g[1]),
                "rd": int(g[2]),
                "pos": [float(g[3]), float(g[4]), float(g[5])],
                "loader": {
                    "chunks_loading": int(g[6]),
                    "light_jobs": int(g[7]),
                    "light_pending": int(g[8]),
                    "light_claims": int(g[9]),
                    "light_dirty": int(g[10]),
                    "light_dropped": int(g[11]),
                    "light_bfs_active": int(g[12]),
                    "light_bfs_priority": int(g[13]),
                    "light_bfs_starve": int(g[14]),
                    "light_seeds": int(g[15]),
                    "light_stale_count": int(g[16]),
                    "chunks_occluded": int(g[17]),
                    "chunks_drawable": int(g[18]),
                },
                "light": _light_json(g[19]),
            })
            continue
        m = FLY_SETTLED_RE.search(line)
        if m:
            records.append({"type": "event", "tick": int(m.group(1)), "name": "fly_settled"})
            continue
        m = CAPTURE_SHOT_RE.search(line)
        if m:
            view = int(m.group(1))
            records.append({
                "type": "event",
                "tick": 0,
                "name": "capture_shot",
                "detail": {
                    "view": view,
                    "file": "screenshots/capture_%d_display.tga" % view,
                    "ticks": int(m.group(2)),
                    "timeout": 1 if m.group(3) else 0,
                },
            })
    return records


# --- rep_tests output (the only other log parse: --tests has no results) --
TEST_FAIL = re.compile(r'Test "(\w+)" failed')
TEST_LISTED_FAIL = re.compile(r"^\s+(\w+)$", re.M)
TEST_FAIL_HDR = re.compile(r"(\d+) of (\d+) test\(s\) failed:")
TEST_ALL_PASS = re.compile(r"All (\d+) Rep Tests passed")


def parse_rep_tests(text):
    """rep_tests stdout -> {passed, failed, failed_names}."""
    m = TEST_FAIL_HDR.search(text)
    if m:
        failed, run = int(m.group(1)), int(m.group(2))
        names = TEST_LISTED_FAIL.findall(text[m.end():])[:failed]
        return {"passed": run - failed, "failed": failed, "failed_names": names}
    m = TEST_ALL_PASS.search(text)
    if m:
        return {"passed": int(m.group(1)), "failed": 0, "failed_names": []}
    return {"passed": 0, "failed": 0, "failed_names": [],
            "note": "no test summary found"}


def convert(log_path, out_path, git=""):
    with open(log_path, "r", errors="replace") as f:
        records = parse_lines(f, git=git)
    with open(out_path, "w") as f:
        for rec in records:
            f.write(json.dumps(rec) + "\n")
    return len(records)
