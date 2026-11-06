"""Image diffing for `rep-dbg diff` — numpy metrics, heat.png, side.png."""

import os

import numpy as np
from PIL import Image, ImageDraw


def _load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def compare(path_a, path_b, threshold=8):
    """Per-pixel max-channel delta metrics; bbox in [x0,y0,x1,y1] pixels."""
    a, b = _load(path_a), _load(path_b)
    if a.shape != b.shape:
        raise ValueError("size mismatch: %s %s vs %s %s" %
                         (path_a, a.shape, path_b, b.shape))
    delta = np.abs(a - b).max(axis=2)
    mask = delta > threshold
    changed = int(mask.sum())
    if changed:
        ys, xs = np.nonzero(mask)
        bbox = [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())]
    else:
        bbox = None
    return {"changed_px": changed,
            "changed_frac": changed / mask.size,
            "max_delta": int(delta.max()),
            "mean_delta": float(delta.mean()),
            "bbox": bbox,
            "stable": None}


def _heat_array(a, b):
    """Amplified delta heat; unchanged pixels = dimmed grayscale of a."""
    delta = np.abs(a - b).max(axis=2).astype(np.float64)
    norm = np.clip(delta * 4, 0, 255) / 255.0
    r = norm
    g = np.clip(norm * 2 - 0.5, 0, 1)
    bl = np.clip(1 - norm * 1.5, 0, 1) * 0.6
    heat = np.stack([r, g, bl], axis=2)
    gray = a.mean(axis=2, keepdims=True) / 255.0 * 0.35
    base = np.repeat(gray, 3, axis=2)
    return np.where(delta[..., None] > 0, heat, base)


def heat_png(path_a, path_b, out):
    a, b = _load(path_a), _load(path_b)
    if a.shape != b.shape:
        raise ValueError("size mismatch: %s vs %s" % (path_a, path_b))
    img = Image.fromarray((_heat_array(a, b) * 255).astype(np.uint8))
    img.save(out)
    return out


def side_png(path_a, path_b, path_heat, out):
    ims = [Image.open(p).convert("RGB") for p in (path_a, path_b, path_heat)]
    w = max(i.width for i in ims)
    label_h = 16
    canvas = Image.new("RGB", (w * 3 + 8, ims[0].height + label_h), (30, 30, 30))
    d = ImageDraw.Draw(canvas)
    for i, (im, label) in enumerate(zip(ims, ("a", "b", "heat"))):
        x = i * (w + 4)
        canvas.paste(im, (x, label_h))
        d.text((x + 4, 2), label, fill="white")
    canvas.save(out)
    return out
