#!/usr/bin/env python3
"""Generate bitmaps/end_noise.png and bitmaps/end_stars.png — the tiling
textures behind the End sky cube and the screen-space end portal/gateway
starfield (see docs/rendering.md). Both are white noise, so the tiling
seams are statistically invisible; no edge blending is needed.

end_noise: 128x128 per-pixel purple noise in the style of vanilla's
environment/end_sky.png — bright (mean ~(115,88,156)) with low contrast
(std/mean ~0.22) and strongly channel-correlated, so the same texture
serves both consumers the way vanilla's does: the End sky cube multiplies
it by #282828, the end portal starfield's background layer multiplies it
by #06191C (both tints live in the shaders). It is white noise, so the
tiling seams are statistically invisible.

end_stars: 256x256 black with sparse grayscale star blobs in the spirit of
the vanilla end portal texture (~9% coverage, mostly dim with rare bright
stars). Generated at half resolution and replicated 2x2 so stars read as
small clusters instead of single-pixel salt; replication of the full canvas
tiles seamlessly by construction.

Run once after editing; the outputs are committed binary assets.
"""
import os
import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..")
SEED = 20261124


def gen_end_noise():
    rng = np.random.default_rng(SEED + 1)
    # Per-pixel intensity noise around 1.0 (half-range 0.22 matches the
    # reference's std/mean), one field shared by all channels plus a whisper
    # of per-channel noise so the purple doesn't go perfectly flat.
    intensity = 1.0 + (rng.random((128, 128)) - 0.5) * 0.75
    base = np.array([115.0, 88.0, 156.0], dtype=np.float32)
    rgb = intensity[..., None] * base
    rgb += (rng.random((128, 128, 3)) - 0.5) * 10.0
    out = np.zeros((128, 128, 4), dtype=np.uint8)
    out[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    out[..., 3] = 255
    Image.fromarray(out).save(os.path.join(ROOT, "bitmaps/end_noise.png"))
    print("end_noise 128x128 mean:", out[..., :3].reshape(-1, 3).mean(0).round(1), "std:", out[..., :3].reshape(-1, 3).std(0).round(1))


def gen_end_stars():
    rng = np.random.default_rng(SEED)
    lo = np.zeros((128, 128), dtype=np.float32)
    # Coverage ~9%: mostly dim stars, a few bright ones — the brightness
    # distribution is r**6-ish so dim dominates but the tail reaches 1.0.
    mask = rng.random((128, 128)) < 0.09
    bright = 0.06 + 0.94 * rng.random((128, 128)) ** 6
    lo[mask] = bright[mask]
    # Replicate 2x2 -> 256x256 star blobs; exact tiling of the low-res grid.
    hi = np.repeat(np.repeat(lo, 2, axis=0), 2, axis=1)
    out = np.zeros((256, 256, 4), dtype=np.uint8)
    v = np.clip(hi * 255.0, 0, 255).astype(np.uint8)
    # Very slight purple/blue cast on the brighter stars keeps the field
    # from reading as pure white noise once tinted per-layer.
    out[..., 0] = v
    out[..., 1] = (v.astype(np.uint16) * 240 // 255).astype(np.uint8)
    out[..., 2] = np.clip(v.astype(np.uint16) * 255 // 240, 0, 255).astype(np.uint8)
    out[..., 3] = 255
    Image.fromarray(out).save(os.path.join(ROOT, "bitmaps/end_stars.png"))
    nz = (out[..., :3].sum(-1) > 12).mean()
    print("end_stars 256x256 coverage: %.4f" % nz)


gen_end_noise()
gen_end_stars()
