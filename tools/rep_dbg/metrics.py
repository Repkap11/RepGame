"""rep-dbg metrics: per-field stats, CSV rows and a PNG line chart over a
run's metric records (or a bare results.jsonl / stdout.log via the shim)."""

import csv
import io
import json
import os
import sys

from . import logparse
from . import stats as stats_mod


def load_records(ref, root):
    """ref: run id/dir, a results.jsonl, or a stdout.log (shim). -> (records, run_dir|None)."""
    if os.path.isfile(ref):
        if ref.endswith(".log"):
            with open(ref, errors="replace") as f:
                return logparse.parse_lines(f), None
        recs = [json.loads(l) for l in open(ref) if l.strip()]
        return recs, os.path.dirname(ref)
    from . import results as results_mod
    r = results_mod.load(ref, root)
    return r.records(), r.dir


def metric_records(records, after_settle=False):
    recs = [r for r in records if r.get("type") == "metric"]
    if after_settle:
        settle = next((e.get("tick") for e in records
                       if e.get("type") == "event" and e.get("name") == "fly_settled"), None)
        recs = [r for r in recs if settle is None or r.get("tick", 0) > settle]
    return recs


def flatten(rec):
    """One metric record -> {dotted.field: number} (tick included)."""
    flat = {}

    def walk(obj, prefix):
        for k, v in obj.items():
            path = prefix + k
            if isinstance(v, dict):
                walk(v, path + ".")
            elif isinstance(v, list):
                for i, item in enumerate(v):
                    if isinstance(item, (int, float)) and not isinstance(item, bool):
                        flat["%s.%d" % (path, i)] = item
            elif isinstance(v, (int, float)) and not isinstance(v, bool):
                flat[path] = v
    walk(rec, "")
    return flat


def csv_rows(metrics):
    """(header, rows) for --csv: tick plus every dotted field."""
    flat_recs = [flatten(r) for r in metrics]
    fields = sorted({f for fr in flat_recs for f in fr if f != "tick"})
    header = ["tick"] + fields
    rows = [[fr.get("tick"), *[fr.get(f, "") for f in fields]] for fr in flat_recs]
    return header, rows


def print_table(stats, fields=None):
    for key in sorted(stats):
        if fields and key not in fields:
            continue
        agg = stats[key]
        if agg:
            print("%-48s mean=%-12.4g max=%-12.4g p50=%-12.4g p95=%-12.4g last=%s" % (
                key, agg["mean"], agg["max"], agg["p50"], agg["p95"], agg["last"]))


def plot(metrics, fields, out_path, width=1200, height=600):
    """PNG line chart: tick on x, one polyline per field (auto-normalized)."""
    from PIL import Image, ImageDraw
    series = []
    for field in fields:
        ys = [flatten(r).get(field) for r in metrics]
        xs = [r.get("tick", i) for i, r in enumerate(metrics)]
        pts = [(x, y) for x, y in zip(xs, ys) if y is not None]
        if pts:
            series.append((field, pts))
    if not series:
        raise ValueError("no data for fields %s" % fields)

    img = Image.new("RGB", (width, height), "white")
    d = ImageDraw.Draw(img)
    left, right, top, bottom = 70, width - 20, 20, height - 60
    xmin = min(x for _f, pts in series for x, _y in pts)
    xmax = max(x for _f, pts in series for x, _y in pts)
    ymin = min(y for _f, pts in series for _x, y in pts)
    ymax = max(y for _f, pts in series for _x, y in pts)
    if xmax == xmin:
        xmax = xmin + 1
    if ymax == ymin:
        ymax = ymin + 1

    def px(x):
        return left + (x - xmin) / (xmax - xmin) * (right - left)

    def py(y):
        return bottom - (y - ymin) / (ymax - ymin) * (bottom - top)

    d.rectangle([left, top, right, bottom], outline="black")
    for i in range(5):
        gx = left + i * (right - left) / 4
        d.line([gx, top, gx, bottom], fill=(230, 230, 230))
        d.text((gx - 10, bottom + 4), "%g" % (xmin + i * (xmax - xmin) / 4), fill="black")
        gy = top + i * (bottom - top) / 4
        d.line([left, gy, right, gy], fill=(230, 230, 230))
        d.text((4, gy - 6), "%g" % (ymax - i * (ymax - ymin) / 4), fill="black")
    colors = [(220, 40, 40), (40, 100, 220), (30, 160, 60), (200, 140, 0), (140, 40, 200), (0, 170, 170)]
    for i, (field, pts) in enumerate(series):
        color = colors[i % len(colors)]
        d.line([(px(x), py(y)) for x, y in pts], fill=color, width=2)
        d.rectangle([left + 8, top + 8 + i * 14, left + 20, top + 18 + i * 14], fill=color)
        d.text((left + 24, top + 8 + i * 14), field, fill="black")
    img.save(out_path)
    return out_path
