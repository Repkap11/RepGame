"""Metric aggregation: stats.<dotted-field> -> {mean,max,p50,p95,last}."""

import math


def aggregate(values):
    if not values:
        return None
    vals = sorted(values)
    return {
        "mean": sum(vals) / len(vals),
        "max": vals[-1],
        "p50": _pct(vals, 50),
        "p95": _pct(vals, 95),
        "last": values[-1],
    }


def _pct(sorted_vals, p):
    idx = int(math.ceil(p / 100.0 * len(sorted_vals))) - 1
    return sorted_vals[max(0, min(idx, len(sorted_vals) - 1))]


def _flatten(obj, prefix, out):
    for key, val in obj.items():
        path = prefix + key
        if isinstance(val, dict):
            _flatten(val, path + ".", out)
        elif isinstance(val, (int, float)) and not isinstance(val, bool):
            out.setdefault(path, []).append(val)
        elif isinstance(val, list):
            for i, item in enumerate(val):
                if isinstance(item, (int, float)) and not isinstance(item, bool):
                    out.setdefault("%s.%d" % (path, i), []).append(item)


def compute_stats(metrics, settle_tick=None):
    """metric records -> {"metric.<dotted>": aggregate, ...}.

    settle_tick (fly_settled tick) adds "<key>_after_settle" variants built
    from the records past it.
    """
    values = {}
    values_settled = {}
    for rec in metrics:
        flat = {}
        _flatten({k: v for k, v in rec.items() if k not in ("type", "tick")}, "", flat)
        for path, vals in flat.items():
            key = "metric." + path
            values.setdefault(key, []).extend(vals)
            if settle_tick is not None and rec.get("tick", 0) > settle_tick:
                values_settled.setdefault(key, []).extend(vals)
    stats = {}
    for key, vals in values.items():
        stats[key] = aggregate(vals)
    for key, vals in values_settled.items():
        stats[key + "_after_settle"] = aggregate(vals)
    return stats
