#!/usr/bin/env python3
"""Voxelize a 3D model into colored blocks and write a RepGame flat world.

Usage:
    model_to_world.py MODEL [MODEL ...] --world NAME [--size 64] [--up y|z]
                        [--x 4] [--gap 8] [--repgame-dir ~/.repgame]
                        [--no-glass] [--force]
                        [--color NAME=#RRGGBB]... [--prefer A,B,C]...

Multiple MODELs are each scaled to --size on their longest side and placed
in a row along +x (--gap blocks apart), all z-centered on 0 at y=1.

Writes chunk files (the same RLE format MapStorage persists) for a flat
world under <repgame-dir>/<world>/ plus a `world_type` marker file so the
client generates a sandstone floor under the model.
"""

import argparse
import concurrent.futures
import json
import multiprocessing
import os
import re
import struct
import sys
from pathlib import Path

import numpy as np

try:
    import trimesh
except ImportError:
    sys.exit(
        "trimesh is required. Create the project venv:\n"
        "  python3 -m venv out/model_venv\n"
        "  out/model_venv/bin/pip install 'trimesh==4.12.2' numpy pillow\n"
        "then run with out/model_venv/bin/python."
    )

REPO = Path(__file__).resolve().parent.parent
CHUNK = 62           # usable chunk extent
CHUNK_INTERNAL = 64  # stored extent including the 1-cell halo
RECORD_SIZE = 18     # sizeof(STORAGE_TYPE): BlockState(14) + u32 count
ALPHA_THRESHOLD = 0.75
GLASS_MIN_SHARE = 0.3  # glass wins a voxel with >= this share of its votes
PREFER_BOOST = 1e6     # vote-weight multiplier for --prefer materials
# Cap on outside-air flood fill iterations; unreached cells count as
# inside, which just falls back to plain majority votes.
FLOOD_MAX_SWEEPS = 8
BATCH_TRIS = 200000

# On-disk RLE record: packed BlockState {u32 id, u8 rotation, i32 power,
# u32 display_id, u8 waterlogged} + u32 count. Matches STORAGE_TYPE.
RECORD_DTYPE = np.dtype(
    [
        ("id", "<u4"),
        ("rotation", "u1"),
        ("power", "<i4"),
        ("display_id", "<u4"),
        ("waterlogged", "u1"),
        ("num", "<u4"),
    ],
    align=False,
)
assert RECORD_DTYPE.itemsize == RECORD_SIZE


def load_tex_ids():
    ids = {}
    for line in open(REPO / "include/common/texture_ids_gen.hpp"):
        m = re.match(r"#define\s+TEX_(\S+)\s+(\d+)", line)
        if m:
            ids[m.group(1)] = int(m.group(2))
    return ids


def tile_mean_rgb(atlas, tile):
    """Alpha-weighted mean rgb (0..1) of a 16x16 atlas tile (alpha>0 pixels)."""
    x0, y0 = (tile % 24) * 16, (tile // 24) * 16
    px = atlas[y0 : y0 + 16, x0 : x0 + 16].astype(np.float64)
    a = px[..., 3:4] / 255.0
    w = a.sum()
    if w <= 0:
        return np.array([0.5, 0.5, 0.5])
    return (px[..., :3] * a).sum(axis=(0, 1)) / w / 255.0


def single_texture_name(textures):
    if "all" in textures:
        return textures["all"]
    vals = set(textures.values())
    return vals.pop() if len(vals) == 1 else None


def build_palette(atlas, tex_ids, glass_enabled):
    """Returns opaque_ids, opaque_rgb, glass_ids, glass_rgb, names."""
    opaque, glass, names = [], [], {}
    for path in sorted((REPO / "data/blocks").glob("*.json")):
        d = json.load(open(path))
        name, bid = d["name"], d["id"]
        is_glass = name == "GLASS" or name.endswith("_STAINED_GLASS")
        is_opaque = (
            name.endswith("_CONCRETE")
            or name == "CONCRETE"
            or name.endswith("_WOOL")
            or (name.endswith("_TERRACOTTA") and "GLAZED" not in name)
        )
        if not is_glass and not is_opaque:
            continue
        if not glass_enabled and is_glass:
            continue
        tex = single_texture_name(d.get("textures", {}))
        if tex is None:
            continue
        tile = tex_ids.get(tex.upper())
        if tile is None:
            continue
        names[bid] = name
        (glass if is_glass else opaque).append((bid, tile_mean_rgb(atlas, tile)))
    opaque_ids = np.array([b for b, _ in opaque])
    opaque_rgb = np.array([c for _, c in opaque])
    if glass:
        glass_ids = np.array([b for b, _ in glass])
        glass_rgb = np.array([c for _, c in glass])
    else:
        glass_ids = np.empty(0, np.int64)
        glass_rgb = np.empty((0, 3))
    return opaque_ids, opaque_rgb, glass_ids, glass_rgb, names


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)


def linear_to_srgb(l):
    l = np.asarray(l, dtype=np.float64)
    return np.where(l > 0.0031308, 1.055 * np.power(l, 1.0 / 2.4) - 0.055, 12.92 * l)


def srgb_to_lab(rgb):
    """rgb float [0,1] -> CIELAB (D65). Vectorized."""
    rgb = np.asarray(rgb, dtype=np.float64)
    linear = srgb_to_linear(rgb)
    M = np.array(
        [
            [0.4124564, 0.3575761, 0.1804375],
            [0.2126729, 0.7151522, 0.0721750],
            [0.0193339, 0.1191920, 0.9503041],
        ]
    )
    xyz = linear @ M.T
    t = xyz / np.array([0.95047, 1.0, 1.08883])
    f = np.where(t > 0.008856, np.cbrt(t), 7.787 * t + 16.0 / 116.0)
    L = 116 * f[..., 1] - 16
    a = 500 * (f[..., 0] - f[..., 1])
    b = 200 * (f[..., 1] - f[..., 2])
    return np.stack([L, a, b], axis=-1)


class BakedGeometry:
    """Transformed vertex data plus the ORIGINAL visual reference.

    Visuals are shared, not copied (trimesh's geometry.copy() deep-copies
    them and breaks material identity — needed for the id-image cache).
    """

    __slots__ = ("vertices", "faces", "visual", "names")


def load_geometries(path, up, rotate=None):
    """Baked world-space geometries, one per scene-graph geometry node."""
    scene = trimesh.load(path, force="scene")
    parts = []
    for node in scene.graph.nodes_geometry:
        transform, geom_name = scene.graph[node]
        geom = scene.geometry[geom_name]
        if not isinstance(geom, trimesh.Trimesh):
            continue
        g = BakedGeometry()
        g.vertices = trimesh.transformations.transform_points(
            geom.vertices, transform
        )
        g.faces = geom.faces
        g.visual = geom.visual
        g.names = (node, geom_name)  # scene node name, geometry name
        parts.append(g)
    if not parts:
        sys.exit("No triangle geometry found in model.")
    if up == "z":
        # Z-up model -> Y-up world: rotate -90 degrees about x.
        rot = trimesh.transformations.rotation_matrix(-np.pi / 2, [1, 0, 0])
        for g in parts:
            g.vertices = trimesh.transformations.transform_points(
                g.vertices, rot
            )
    if rotate:
        ax = {"x": [1, 0, 0], "y": [0, 1, 0], "z": [0, 0, 1]}
        rot = np.eye(4)
        for axis, deg in rotate:
            rot = trimesh.transformations.rotation_matrix(
                np.radians(deg), ax[axis]
            ) @ rot
        for g in parts:
            g.vertices = trimesh.transformations.transform_points(
                g.vertices, rot
            )
    return parts


def norm_factor(f):
    """Normalize a material factor to 0..1 float (uint8-style or float input)."""
    f = np.asarray(f).ravel()
    if f.size == 0:
        return np.ones(4)
    if np.issubdtype(f.dtype, np.integer) or f.astype(np.float64).max() > 1.0:
        f = f.astype(np.float64) / 255.0
    else:
        f = f.astype(np.float64)
    out = np.ones(4)
    out[: min(f.size, 4)] = f[:4]
    return out


def block_lut(pal_lab, pal_ids):
    """sRGB -> palette block id lookup, quantized to 64 levels per channel.

    Cell (r,g,b) holds the nearest block for sRGB value ((i*4+2)/255) per
    channel; one 4096-cell slab at a time keeps the distance temp small.
    """
    lut = np.empty((64, 64, 64), dtype=np.uint16)
    q = np.arange(64)
    gg, bb = np.meshgrid(q, q, indexing="ij")
    g = (gg * 4 + 2) / 255.0
    b = (bb * 4 + 2) / 255.0
    for r in range(64):
        rgb = np.stack(
            [np.full(g.shape, (r * 4 + 2) / 255.0), g, b], axis=-1
        ).reshape(-1, 3)
        lab = srgb_to_lab(rgb)
        lut[r] = nearest_palette(lab, pal_lab, pal_ids).reshape(64, 64)
    return lut


def rgba_to_block_ids(rgba, opaque_lut, glass_lut):
    """rgba (...,4) in 0..255 -> same-shape id array. Ids < 2048 are opaque
    palette entries; >= 2048 are glass palette ids + 2048."""
    rgba = np.asarray(rgba)
    q = np.clip(rgba[..., :3], 0, 255).astype(np.int32) >> 2
    ids = opaque_lut[q[..., 0], q[..., 1], q[..., 2]].astype(np.int32)
    if glass_lut is not None:
        gmask = (rgba[..., 3] / 255.0) < ALPHA_THRESHOLD
        ids[gmask] = glass_lut[
            q[..., 0][gmask], q[..., 1][gmask], q[..., 2][gmask]
        ].astype(np.int32) + 2048
    return ids


def material_probe(geom, overrides):
    """Classify a geometry's color source without baking anything.

    Returns (kind, info, label):
      'override': info = (rgb sRGB 0..255 (3,), alpha 0..255)
      'texture' : info = (img, factor (4,), key, alt, uv)
      'color'   : info = None — face/vertex/material color path.
    """
    visual = geom.visual
    uv = getattr(visual, "uv", None)
    mat = getattr(visual, "material", None)
    label = getattr(mat, "name", None) or type(mat).__name__ if mat is not None else "none"
    if overrides and mat is not None and getattr(mat, "name", None) in overrides:
        a = 255.0
        if getattr(mat, "baseColorFactor", None) is not None:
            a = np.clip(norm_factor(mat.baseColorFactor)[3] * 255.0, 0, 255)
        return "override", (overrides[mat.name], a), f"{mat.name} (override)"
    if uv is not None and len(uv) >= geom.vertices.shape[0]:
        img = getattr(mat, "image", None) if mat is not None else None
        if img is None and mat is not None and hasattr(mat, "baseColorTexture"):
            img = mat.baseColorTexture
        if img is not None:
            factor = np.ones(4)  # linear-space multiplier
            if mat is not None and getattr(mat, "baseColorFactor", None) is not None:
                factor = norm_factor(mat.baseColorFactor)
            elif mat is not None and getattr(mat, "diffuse", None) is not None:
                factor = norm_factor(mat.diffuse)
            # Cache keys exactly as material_id_source used: id(mat), with
            # (name, image id, factor) aliasing duplicate material objects.
            key = ("mat", id(mat))
            alt = (getattr(mat, "name", None), id(img), factor.tobytes())
            return "texture", (img, factor, key, alt, uv), label
    return "color", None, label


def bake_arr(arr, factor, opaque_lut, glass_lut):
    """RGBA float 0..255 x linear factor -> (id_img int32, eff mean rgba)."""
    arr = arr.copy()
    rgb = linear_to_srgb(srgb_to_linear(arr[..., :3] / 255.0) * factor[:3]) * 255.0
    arr[..., :3] = np.clip(rgb, 0, 255)
    arr[..., 3] = np.clip(arr[..., 3] * factor[3], 0, 255)
    id_img = rgba_to_block_ids(arr, opaque_lut, glass_lut)
    eff = np.concatenate([arr[..., :3].reshape(-1, 3).mean(0), [arr[..., 3].mean()]])
    return id_img, eff


def bake_image_group(group, opaque_lut, glass_lut):
    """(img, [bake jobs]) -> [((key, alt), id_img, eff)].

    One image is decoded once per group — PIL lazy decoding is not
    thread-safe, so jobs sharing an img object must stay in one task.
    """
    img, jobs = group
    arr = np.asarray(img.convert("RGBA"), dtype=np.float64)
    out = []
    for _img, factor, key, alt in jobs:
        id_img, eff = bake_arr(arr, factor, opaque_lut, glass_lut)
        out.append(((key, alt), id_img, eff))
    return out


def resolve_source(geom, pr, opaque_lut, glass_lut, mat_cache):
    """Turn a material_probe result into the sampling-time source.

    Returns (id_img or None, uv_faces or None, face_ids or None, label,
    eff). 'texture' probes must already be baked into mat_cache.
    """
    kind, info, label = pr
    n = len(geom.faces)
    if kind == "texture":
        img, factor, key, alt, uv = info
        e = mat_cache.get(key) or mat_cache[alt]
        id_img, _, eff = e
        return id_img, uv[geom.faces], None, label, eff
    colors = np.tile(np.array([204.0, 204.0, 204.0, 255.0]), (n, 1))
    if kind == "override":
        rgb, a = info
        colors[:, :3] = rgb
        colors[:, 3] = a
        eff = np.concatenate([rgb, [a]])
        return (None, None, rgba_to_block_ids(colors, opaque_lut, glass_lut),
                label, eff)
    visual = geom.visual
    mat = getattr(visual, "material", None)
    face_colors = getattr(visual, "face_colors", None)
    vertex_colors = getattr(visual, "vertex_colors", None)
    if face_colors is not None and len(face_colors) == n:
        colors[:] = face_colors[:, :4]
    elif vertex_colors is not None and len(vertex_colors):
        colors[:] = np.asarray(vertex_colors)[geom.faces].mean(axis=1)
    elif mat is not None:
        base = getattr(mat, "baseColorFactor", None)
        if base is None:
            base = getattr(mat, "diffuse", None)
        if base is not None:
            b = norm_factor(base)  # linear factor -> sRGB
            srgb = np.clip(linear_to_srgb(b[:3]) * 255.0, 0, 255)
            colors[:, :3] = srgb
            colors[:, 3] = np.clip(b[3] * 255.0, 0, 255)
    return (None, None, rgba_to_block_ids(colors, opaque_lut, glass_lut),
            label, colors.mean(axis=0))


def sample_image(img, uv):
    """Nearest-pixel texture lookup, repeat wrap, v flipped (trimesh)."""
    h, w = img.shape[:2]
    px = np.clip((np.mod(uv[:, 0], 1.0) * w).astype(np.int64), 0, w - 1)
    py = np.clip((np.mod(1.0 - uv[:, 1], 1.0) * h).astype(np.int64), 0, h - 1)
    return img[py, px]


def nearest_palette(lab, pal_lab, pal_ids):
    d = ((lab[:, None, :] - pal_lab[None, :, :]) ** 2).sum(-1)
    return pal_ids[d.argmin(1)]


# Read-only inputs for fork workers (inherited copy-on-write — never
# pickled). Filled before the pool is created.
_WORK = {}


def _sample_unit(unit):
    """Sample one (model, part, tri_lo, tri_hi) range; returns locally
    reduced (unique_keys int64, summed_weights float64)."""
    mi, gi, lo, hi = unit
    verts, faces = _WORK["parts"][gi]
    id_img, uv_faces, face_ids = _WORK["src"][gi]
    origin, scale, dims = _WORK["tf"][mi]
    gv = (verts - origin) * scale
    tris = gv[faces[lo:hi]]
    vote_keys = []
    vote_w = []
    t = tris
    e01 = t[:, 1] - t[:, 0]
    e02 = t[:, 2] - t[:, 0]
    e12 = t[:, 2] - t[:, 1]
    nrm = np.cross(e01, e02)
    nl = np.linalg.norm(nrm, axis=1)
    areas = 0.5 * nl
    u = np.zeros_like(nrm)
    ok = nl > 0
    u[ok] = nrm[ok] / nl[ok, None]
    # per-triangle facing bits: +x=1, -x=2, +y=4, -y=8, +z=16, -z=32
    smask = (
        (u[:, 0] > 0.1) * 1 + (u[:, 0] < -0.1) * 2
        + (u[:, 1] > 0.1) * 4 + (u[:, 1] < -0.1) * 8
        + (u[:, 2] > 0.1) * 16 + (u[:, 2] < -0.1) * 32
    ).astype(np.int64)
    e = np.stack(
        [
            np.linalg.norm(e01, axis=1),
            np.linalg.norm(e12, axis=1),
            np.linalg.norm(t[:, 2] - t[:, 0], axis=1),
        ],
        axis=1,
    )
    ns = np.maximum(1, np.ceil(e.max(axis=1) / 0.5)).astype(np.int64)
    for n in np.unique(ns):
        idx = np.where(ns == n)[0]
        tt = t[idx]
        ii, jj = np.meshgrid(np.arange(n + 1), np.arange(n + 1), indexing="ij")
        m = (ii + jj) <= n
        # barycentric lattice sample points (i/n, j/n), i+j<=n
        B = np.stack([ii[m] / n, jj[m] / n], axis=1).astype(np.float64)
        B = np.concatenate([B, 1.0 - B.sum(1, keepdims=True)], axis=1)
        pts = np.einsum("tcd,sc->tsd", tt, B).reshape(-1, 3)
        tri_of = np.repeat(idx, len(B))
        b_of = np.tile(np.arange(len(B)), len(idx))
        vox = np.clip(np.floor(pts), 0, dims - 1).astype(np.int64)
        del pts
        # vlin is globally unique: model mi's grid starts at vbase[mi].
        vlin = (vox[:, 0] * dims[1] + vox[:, 1]) * dims[2] + vox[:, 2] + _WORK["vbase"][mi]
        del vox
        if id_img is not None:
            suv = np.einsum("sd,sdv->sv", B[b_of], uv_faces[lo + tri_of])
            ids = sample_image(id_img, suv).astype(np.int64)
            del suv
        else:
            ids = face_ids[lo + tri_of].astype(np.int64)
        # key = (vox * 64 + facing_mask) * 4096 + (bid | 2048 for glass)
        vote_keys.append((vlin * 64 + smask[tri_of]) * 4096 + ids)
        boost = _WORK["boost"][gi] if "boost" in _WORK else 1.0
        vote_w.append(np.repeat(areas[idx] / len(B), len(B)) * boost)
        del vlin, ids
    keys = np.concatenate(vote_keys)
    weights = np.concatenate(vote_w)
    uk, inv = np.unique(keys, return_inverse=True)
    return uk, np.bincount(inv, weights=weights)


def merge_votes(pairs):
    """Merge [(unique_keys, weights)] into a single reduced pair."""
    k = np.concatenate([p[0] for p in pairs])
    w = np.concatenate([p[1] for p in pairs])
    uk, inv = np.unique(k, return_inverse=True)
    return uk, np.bincount(inv, weights=w)


def outside_air(occ):
    """6-connected flood fill of ~occ from the grid boundary.

    Repeated axis sweeps: for each axis, contiguous air runs per line are
    identified by cumsum(occ); a run is outside if any of its cells is
    already outside. Run boundaries and the transposed (rest, n) layouts
    of the static masks are precomputed once; each sweep is one strided
    gather of `outside`, a reduceat over runs, and a scatter back.
    Returns (outside bool, PADDED dims+2, iteration count) — the padded
    ring is outside air, which is what out-of-grid neighbours resolve to.
    """
    P = np.pad(occ, 1)          # occupied; the pad layer is air
    air = ~P
    outside = air.copy()
    outside[1:-1, 1:-1, 1:-1] = False  # only the border ring starts outside
    # Static per-axis data: (rest, n) contiguous masks + run segmentation.
    layouts = []
    for ax in range(3):
        n = P.shape[ax]
        rest = P.size // n
        Pa = np.ascontiguousarray(
            np.moveaxis(P, ax, 0).reshape(n, rest).T
        )
        Aa = np.ascontiguousarray(
            np.moveaxis(air, ax, 0).reshape(n, rest).T
        )
        cum = np.cumsum(Pa, axis=1, dtype=np.int32)
        g = (cum + np.arange(rest, dtype=np.int32)[:, None] * (n + 1)).ravel()
        bounds = np.flatnonzero(np.r_[True, g[1:] != g[:-1], True])
        layouts.append((ax, n, rest, Aa, bounds, np.diff(bounds)))
    it = 0
    while it < FLOOD_MAX_SWEEPS:
        it += 1
        before = outside.sum()
        for ax, n, rest, Aa, bounds, lens in layouts:
            osh = np.moveaxis(outside, ax, 0).shape  # (n, d1, d2)
            # gather outside into this axis's contiguous (rest, n) layout
            Oa = np.ascontiguousarray(
                np.moveaxis(outside, ax, 0).reshape(n, rest).T
            )
            runmax = np.maximum.reduceat(
                Oa.ravel(), bounds[:-1]
            ).astype(bool)
            new = np.repeat(runmax, lens) & Aa.ravel()
            np.moveaxis(outside, ax, 0)[:] = (
                new.reshape(rest, n).T.reshape(osh)
            )
        if outside.sum() == before:
            break
    return outside, it


def votes_to_grid(vox_of, mask_of, low, wsum, dims, tag=""):
    """Visible-vote filter + per-voxel winner -> int16 block grid (dims)."""
    # --- visible-vote filter -------------------------------------------------
    # A vote only "sees" the voxel exterior if the sample's triangle faces
    # toward a neighbour that is outside air. Occupancy is unaffected.
    occ = np.zeros(int(np.prod(dims)), bool)
    occ[vox_of] = True
    occ = occ.reshape(tuple(dims))
    outside, fiters = outside_air(occ)  # padded (dims+2); pad ring = outside
    print(f"  {tag}outside-air flood fill: {fiters} sweep iterations")
    # O[vox]: 6-bit mask of which neighbours are outside air (same bit
    # convention as the per-sample facing mask). Grid cell (i,j,k) maps
    # to padded index (i+1,j+1,k+1), so out-of-grid neighbours correctly
    # read the pad ring (outside air).
    Og = np.zeros(tuple(dims), np.uint8)
    Og |= outside[2:, 1:-1, 1:-1]                        # +x -> bit0
    Og |= (outside[:-2, 1:-1, 1:-1] << 1).astype(np.uint8)
    Og |= (outside[1:-1, 2:, 1:-1] << 2).astype(np.uint8)
    Og |= (outside[1:-1, :-2, 1:-1] << 3).astype(np.uint8)
    Og |= (outside[1:-1, 1:-1, 2:] << 4).astype(np.uint8)
    Og |= (outside[1:-1, 1:-1, :-2] << 5).astype(np.uint8)
    O = Og.reshape(-1)[vox_of]
    del Og, occ
    vis = (mask_of & O) != 0
    # Voxels with at least one visible entry drop the rest; voxels with no
    # visible entry (interior, or broken/flipped normals) keep all entries.
    starts = np.flatnonzero(np.r_[True, vox_of[1:] != vox_of[:-1]])
    counts = np.diff(np.r_[starts, len(vox_of)])
    anyvis = np.maximum.reduceat(vis.astype(np.uint8), starts) > 0
    keep = vis | np.repeat(~anyvis, counts)
    vox2 = vox_of[keep]
    low2 = low[keep]
    w2 = wsum[keep]
    key2 = vox2 * 4096 + low2
    del vox2, low2
    uk, inv = np.unique(key2, return_inverse=True)
    del key2
    wsum = np.bincount(inv, weights=w2)
    del inv, w2
    vox_of = uk >> 12
    low = uk & 4095
    del uk
    is_glass_vote = low >= 2048
    bid_of = np.where(is_glass_vote, low - 2048, low)

    # Per voxel: highest total vote weight wins; opaque wins ties against
    # glass. For each class sort by (vox, weight) — the last entry of each
    # voxel group is its winner.
    def best_per_vox(mask):
        if not mask.any():
            return (np.empty(0, np.int64),) * 2 + (np.empty(0),)
        order = np.lexsort((wsum[mask], vox_of[mask]))
        v = vox_of[mask][order]
        b = bid_of[mask][order]
        w = wsum[mask][order]
        ends = np.r_[np.flatnonzero(v[1:] != v[:-1]) + 1, len(v)] - 1
        return v[ends], b[ends], w[ends]

    ovox, obid, ow = best_per_vox(~is_glass_vote)
    gvox, gbid, gw = best_per_vox(is_glass_vote)

    uniq_vox = np.unique(vox_of)
    oi = np.searchsorted(ovox, uniq_vox)
    oi_c = np.clip(oi, 0, max(len(ovox) - 1, 0))
    has_o = np.zeros(len(uniq_vox), bool)
    if len(ovox):
        has_o = ovox[oi_c] == uniq_vox
    gi_ = np.searchsorted(gvox, uniq_vox)
    gi_c = np.clip(gi_, 0, max(len(gvox) - 1, 0))
    has_g = np.zeros(len(uniq_vox), bool)
    if len(gvox):
        has_g = gvox[gi_c] == uniq_vox
    ow_u = np.where(has_o, ow[oi_c] if len(ow) else 0, -1.0)
    gw_u = np.where(has_g, gw[gi_c] if len(gw) else 0, -1.0)
    obid_u = np.where(has_o, obid[oi_c] if len(obid) else 0, -1)
    gbid_u = np.where(has_g, gbid[gi_c] if len(gbid) else 0, -1)
    # Per-voxel total weight (all entries), for the glass-share rule.
    starts2 = np.flatnonzero(np.r_[True, vox_of[1:] != vox_of[:-1], True])
    total_u = np.add.reduceat(wsum, starts2[:-1])
    # Glass wins if it outvotes opaque, or holds >= GLASS_MIN_SHARE of the
    # voxel's total vote (thin glass next to opaque trim stays glass).
    gwins = (gw_u > ow_u) | (gw_u >= GLASS_MIN_SHARE * total_u)
    winners = np.where(gwins, gbid_u, obid_u)

    grid = np.full(int(np.prod(dims)), -1, np.int16)
    grid[uniq_vox] = winners.astype(np.int16)
    grid = grid.reshape(tuple(dims))
    print(f"  {tag}{int((grid >= 0).sum())} voxels after voting")
    return grid


def _write_chunk_unit(cc):
    """Write one chunk file; returns ((cx,cy,cz), {bid: count})."""
    cx, cy, cz = cc
    lx, ly, lz = _WORK["lx"], _WORK["ly"], _WORK["lz"]
    wyv = cy * CHUNK + ly
    wxv = cx * CHUNK + lx
    wzv = cz * CHUNK + lz
    vals = np.full(lx.shape, -1, np.int64)
    for grid, dims, x0, y0, z0 in _WORK["models"]:
        gx, gy, gz = wxv - x0, wyv - y0, wzv - z0
        inside = (
            (gx >= 0) & (gx < dims[0])
            & (gy >= 0) & (gy < dims[1])
            & (gz >= 0) & (gz < dims[2])
        )
        # Models are placed non-overlapping; first bbox containing the
        # cell wins.
        m = inside & (vals < 0)
        if m.any():
            vals[m] = grid[gx[m], gy[m], gz[m]]
    default = np.where(wyv <= 0, _WORK["sandstone_id"], 0)
    ids = np.where(vals >= 0, vals, default).ravel()
    # usage counts only interior cells (local 0..61), not the halo.
    nonhalo = (lx >= 0) & (lx <= 61) & (ly >= 0) & (ly <= 61) & (lz >= 0) & (lz <= 61)
    usage = {}
    for bid, cnt in zip(*np.unique(vals[nonhalo & (vals >= 0)], return_counts=True)):
        usage[int(bid)] = int(cnt)
    # RLE in index order
    change = np.flatnonzero(np.r_[True, ids[1:] != ids[:-1], True])
    rec = np.empty(len(change) - 1, RECORD_DTYPE)
    rec["id"] = ids[change[:-1]]
    rec["rotation"] = 0
    rec["power"] = 0
    rec["display_id"] = rec["id"]
    rec["waterlogged"] = 0
    rec["num"] = np.diff(change)
    with open(_WORK["world_dir"] / f"chunk_{cx}_{cy}_{cz}", "wb") as f:
        f.write(struct.pack("<I", RECORD_SIZE))
        f.write(rec.tobytes())
    return cc, usage


def report_materials(mat_stats, mat_nodes=None, limit=15):
    """Print the 'Materials by tri count' table; mat_nodes adds --prefer hints."""
    print("Materials by tri count:")
    rows = sorted(mat_stats.items(), key=lambda kv: -kv[1][0])
    for label, (ntri, eff) in rows[:limit] if limit else rows:
        line = (
            f"  {label[:48]:50s} tris={ntri:8d} "
            f"mean_sRGB=({eff[0]:.0f},{eff[1]:.0f},{eff[2]:.0f}) a={eff[3]:.0f}"
        )
        if mat_nodes is not None:
            nodes = sorted(mat_nodes.get(label, ()), key=len)
            line += "  nodes: " + ", ".join(n[:40] for n in nodes[:3])
        print(line)


def main():
    ap = argparse.ArgumentParser(description="Voxelize a 3D model into a RepGame flat world.")
    ap.add_argument("model", nargs="+",
                    help="model file(s); multiple are placed in a row along +x")
    ap.add_argument("--world")  # required unless --list
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--gap", type=int, default=8,
                    help="blocks between models' bounding boxes (default 8)")
    ap.add_argument("--up", choices=["y", "z"], default="y")
    ap.add_argument("--x", type=int, default=4)
    ap.add_argument("--repgame-dir", default=os.path.expanduser("~/.repgame"))
    ap.add_argument("--no-glass", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument(
        "--list",
        action="store_true",
        help="List materials (tri counts, mean colors, example node names) "
        "for picking --color/--prefer names, then exit; no world written.",
    )
    ap.add_argument(
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="worker threads/processes for baking, sampling and chunk writing "
        "(default: all cores; 1 = fully in-process, for debugging)",
    )
    ap.add_argument(
        "--color",
        action="append",
        default=[],
        metavar="NAME=#RRGGBB",
        help="Override a material's color (repeatable), e.g. --color Carpaint=#8E2323",
    )
    ap.add_argument(
        "--prefer",
        action="append",
        default=[],
        metavar="A,B,C",
        help="Ordered priority list of materials, scene-node or geometry "
        "names that win every voxel they touch (repeatable; earlier names "
        "win over later ones), e.g. --prefer Metal_Brushed,Carpaint",
    )
    ap.add_argument(
        "--rotate",
        action="append",
        default=[],
        metavar="[STEM=]SPEC",
        help="Rotate the model(s) in world space (right-hand-rule degrees "
        "about the Y-up axes, applied after --up). SPEC is a comma list of "
        "axis+degrees steps, e.g. --rotate x-90 or --rotate x90,z90. "
        "Prefix with a model file stem to scope it: --rotate Go2=x-90 "
        "(repeatable; global steps run first).",
    )
    args = ap.parse_args()
    if not args.list and not args.world:
        ap.error("--world is required unless --list")

    overrides = {}
    for spec in args.color:
        m = re.fullmatch(r"([^=]+)=#([0-9a-fA-F]{6})", spec.strip())
        if not m:
            sys.exit(f"Bad --color '{spec}' (want NAME=#RRGGBB)")
        r, g, b = (int(m.group(2)[i : i + 2], 16) for i in (0, 2, 4))
        overrides[m.group(1)] = np.array([r, g, b], dtype=np.float64)

    rot_global = []
    rot_by_stem = {}
    for spec in args.rotate:
        stem, eq, body = spec.partition("=")
        if not eq:
            stem, body = None, spec
        steps = []
        for s in body.split(","):
            m = re.fullmatch(r"([xyz])(-?\d+(?:\.\d+)?)", s.strip())
            if not m:
                sys.exit(f"Bad --rotate '{spec}' (want [STEM=]x-90,y180,...)")
            steps.append((m.group(1), float(m.group(2))))
        (rot_global if stem is None else rot_by_stem.setdefault(stem, [])).extend(steps)

    if not args.list:
        world_dir = Path(args.repgame_dir) / args.world
        if world_dir.is_dir() and any(world_dir.glob("chunk_*")) and not args.force:
            sys.exit(f"{world_dir} already contains chunk files (use --force to overwrite).")
        world_dir.mkdir(parents=True, exist_ok=True)

    from PIL import Image

    atlas = np.asarray(Image.open(REPO / "bitmaps/blocks.png").convert("RGBA"))
    tex_ids = load_tex_ids()
    opaque_ids, opaque_rgb, glass_ids, glass_rgb, names = build_palette(
        atlas, tex_ids, not args.no_glass
    )
    if not len(opaque_ids):
        sys.exit("Empty opaque palette.")
    opaque_lab = srgb_to_lab(opaque_rgb)
    glass_lab = srgb_to_lab(glass_rgb) if len(glass_ids) else None
    print(f"Palette: {len(opaque_ids)} opaque, {len(glass_ids)} glass")
    # Quantized sRGB -> block id LUTs; all later palette work is a lookup.
    opaque_lut = block_lut(opaque_lab, opaque_ids)
    glass_lut = block_lut(glass_lab, glass_ids) if glass_lab is not None else None

    sandstone_id = next(
        (
            json.load(open(p))["id"]
            for p in (REPO / "data/blocks").glob("*.json")
            if json.load(open(p))["name"] == "SANDSTONE"
        ),
        None,
    )
    if sandstone_id is None:
        sys.exit("SANDSTONE block not found in data/blocks.")

    models = []  # (path, parts, origin, scale, dims)
    used_stems = set()
    for path in args.model:
        stem = Path(path).stem
        rot = rot_global + rot_by_stem.get(stem, [])
        used_stems.add(stem)
        print(f"Loading {path} ...")
        parts = load_geometries(path, args.up, rot)
        if rot:
            print(f"  rotation: {', '.join(f'{a}{d:g}' for a, d in rot)}")
        nverts = sum(len(g.vertices) for g in parts)
        nfaces = sum(len(g.faces) for g in parts)
        bounds = np.array(
            [(g.vertices.min(axis=0), g.vertices.max(axis=0)) for g in parts]
        )  # (G,2,3)
        bmin, bmax = bounds[:, 0].min(0), bounds[:, 1].max(0)
        extents = bmax - bmin
        print(f"  {nverts} verts, {nfaces} tris across {len(parts)} geometries, extents {extents}")
        scale = args.size / float(extents.max())
        dims = np.maximum(np.ceil(extents * scale), 1).astype(np.int64) + 1
        print(f"  voxel dims: {dims[0]} x {dims[1]} x {dims[2]} blocks")
        models.append((path, parts, bmin, scale, dims))

    jobs = max(1, args.jobs)

    # --- classify materials; bake unique textures in parallel --------------
    probes = []  # per model: [probe per part]
    bake_jobs = {}  # key -> (img, factor, key, alt)
    mat_cache = {}  # key/alt -> (id_img, label, eff)
    for _, parts, _, _, _ in models:
        prs = []
        for geom in parts:
            pr = material_probe(geom, overrides)
            prs.append(pr)
            if pr[0] == "texture":
                img, factor, key, alt, uv = pr[1]
                if key in mat_cache or alt in mat_cache:
                    continue
                if key not in bake_jobs and alt not in bake_jobs:
                    bake_jobs[key] = (img, factor, key, alt)
        probes.append(prs)
    if bake_jobs:
        # Group by image object so a PIL image is only decoded inside one
        # task (its lazy decoder races across threads otherwise).
        bake_groups = {}
        for j in bake_jobs.values():
            bake_groups.setdefault(id(j[0]), (j[0], []))[1].append(j)
        work = lambda g: bake_image_group(g, opaque_lut, glass_lut)
        if jobs > 1 and len(bake_groups) > 1:
            with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
                baked = list(ex.map(work, bake_groups.values()))
        else:
            baked = [work(g) for g in bake_groups.values()]
        for group_result in baked:
            for (key, alt), id_img, eff in group_result:
                mat_cache[key] = mat_cache[alt] = (id_img, None, eff)

    # Resolve each part's sampling source (pure cache hits for textures).
    part_src = []
    part_owner = []  # part index -> model index
    part_boost = []
    # Ordered priority list: rank-0 material gets PREFER_BOOST**N, rank-1
    # gets **(N-1), ... unlisted materials keep weight 1.
    prefer_list = [
        n.strip() for spec in args.prefer for n in spec.split(",") if n.strip()
    ]
    prefer_rank = {n: r for r, n in enumerate(prefer_list)}
    matched_prefers = set()
    mat_stats = {}  # label -> [tri_count, effective mean rgba]
    mat_nodes = {}  # label -> {scene node names} (for --list)
    matched_overrides = set()
    for mi, (_, parts, _, _, _) in enumerate(models):
        for geom, pr in zip(parts, probes[mi]):
            id_img, uv_faces, face_ids, label, eff = resolve_source(
                geom, pr, opaque_lut, glass_lut, mat_cache
            )
            part_src.append((id_img, uv_faces, face_ids))
            part_owner.append(mi)
            part_boost.append(1.0)
            mat_stats.setdefault(label, [0, eff])
            mat_stats[label][0] += len(geom.faces)
            mat_nodes.setdefault(label, set()).add(geom.names[0])
            if pr[0] == "override":
                matched_overrides.add(label[: -len(" (override)")])
            # --prefer entries may name a material, a scene node or a
            # geometry; best (lowest) rank wins per part.
            mat = getattr(geom.visual, "material", None)
            cands = [getattr(mat, "name", None), *geom.names]
            matched = [c for c in cands if c is not None and c in prefer_rank]
            if matched:
                r = min(prefer_rank[c] for c in matched)
                part_boost[-1] = PREFER_BOOST ** (len(prefer_list) - r)
                matched_prefers.update(matched)
    part_boost = np.asarray(part_boost, np.float64)

    for name in set(overrides) - matched_overrides:
        print(f"warning: --color '{name}' matched no material", file=sys.stderr)
    for name in set(prefer_list) - matched_prefers:
        print(f"warning: --prefer '{name}' matched no material", file=sys.stderr)
    for name in set(rot_by_stem) - used_stems:
        print(f"warning: --rotate '{name}' matched no model", file=sys.stderr)

    if args.list:
        report_materials(mat_stats, mat_nodes, limit=None)
        return

    # --- sampling pass: collect (vox, block_id[, glass]) votes --------------
    # combined key per sample: vox_linear * 4096 + (bid | 2048 for glass).
    # Each sample votes with weight tri_area/samples_in_tri (voxel units) so
    # dense tiny-triangle meshes don't outvote large smooth panels. Work
    # units are (model, part, lo, hi) tri ranges, largest first; each returns
    # locally reduced (unique keys, weights) which the parent merges.
    flat_parts = [g for _, parts, _, _, _ in models for g in parts]
    units = [
        (part_owner[gi], gi, lo, min(lo + BATCH_TRIS, len(p.faces)))
        for gi, p in enumerate(flat_parts)
        for lo in range(0, len(p.faces), BATCH_TRIS)
    ]
    units.sort(key=lambda u: u[2] - u[3])  # largest first
    _WORK["parts"] = [(g.vertices, g.faces) for g in flat_parts]
    _WORK["src"] = part_src
    _WORK["tf"] = [(m[2], m[3], m[4]) for m in models]  # origin, scale, dims
    # model mi's voxel ids are offset by vbase[mi] so keys are unique.
    _WORK["vbase"] = np.concatenate(
        [[0], np.cumsum([int(np.prod(m[4])) for m in models])]
    ).astype(np.int64)
    _WORK["boost"] = part_boost

    pool = None
    if jobs > 1:
        try:
            ctx = multiprocessing.get_context("fork")
            pool = ctx.Pool(jobs)
            results = pool.imap_unordered(_sample_unit, units)
        except (ValueError, OSError):
            print("note: 'fork' unavailable, falling back to --jobs 1",
                  file=sys.stderr)
            jobs = 1
    if jobs == 1:
        results = map(_sample_unit, units)

    acc = []  # [(unique_keys, weights)] from workers, periodically merged
    acc_len = 0
    done = 0
    next_report = 1
    for uk, w in results:
        acc.append((uk, w))
        acc_len += len(uk)
        if acc_len > 50_000_000:
            acc = [merge_votes(acc)]
            acc_len = len(acc[0][0])
        done += 1
        if done * 10 // len(units) >= next_report or done == len(units):
            next_report = done * 10 // len(units) + 1
            print(f"  sampling {done}/{len(units)} units", flush=True)
    if pool is not None:
        pool.close()
        pool.join()
        pool = None

    if args.verbose:
        report_materials(mat_stats)

    uk, wsum = merge_votes(acc)
    del acc
    # key = (vox * 64 + facing_mask) * 4096 + (bid | 2048 for glass)
    vox_of = uk >> 18
    mask_of = ((uk >> 12) & 63).astype(np.uint8)
    low = uk & 4095
    del uk

    # --- resolve each model's grid (votes are keyed by global voxel id; ---
    # --- model mi's votes are the contiguous range [vbase[mi], vbase[mi+1]))
    vbase = _WORK["vbase"]
    cuts = np.searchsorted(vox_of, vbase)
    multi = len(models) > 1
    grids = []
    for mi, (path, _, _, _, dims) in enumerate(models):
        s = slice(cuts[mi], cuts[mi + 1])
        tag = f"{Path(path).name}: " if multi else ""
        grids.append(
            votes_to_grid(
                vox_of[s] - vbase[mi], mask_of[s], low[s], wsum[s], dims, tag
            )
        )
    del vox_of, mask_of, low, wsum

    # --- placement: min y -> y=1, first min x -> --x, z centered on 0; -----
    # --- further models continue along +x with --gap between bboxes. -----
    placed = []  # (grid, dims, x0, y0, z0)
    needed = set()
    x_cur = args.x
    for (path, _, _, _, dims), grid in zip(models, grids):
        x0, y0, z0 = x_cur, 1, -(int(dims[2]) // 2)
        placed.append((grid, dims, x0, y0, z0))
        x_cur += int(dims[0]) + args.gap
        ii, jj, kk = np.nonzero(grid >= 0)
        wx, wy, wz = ii + x0, jj + y0, kk + z0
        # Chunks whose 64^3 range (halo included) contains a model voxel:
        # chunk cp covers world [62*cp - 1, 62*cp + 62], so a voxel at w
        # belongs to floor((w-1)/62), floor(w/62) and floor((w+1)/62).
        for dx in (-1, 0, 1):
            cxs = (wx + dx) // CHUNK
            for dy in (-1, 0, 1):
                cys = (wy + dy) // CHUNK
                for dz in (-1, 0, 1):
                    czs = (wz + dz) // CHUNK
                    needed.update(zip(cxs.tolist(), cys.tolist(), czs.tolist()))

    # index order: y, x, z (z fastest) matches (y+1)*64*64 + (x+1)*64 + (z+1)
    _WORK.update(
        models=placed,
        sandstone_id=sandstone_id,
        world_dir=world_dir,
    )
    _WORK["ly"], _WORK["lx"], _WORK["lz"] = np.meshgrid(
        np.arange(-1, CHUNK_INTERNAL - 1),
        np.arange(-1, CHUNK_INTERNAL - 1),
        np.arange(-1, CHUNK_INTERNAL - 1),
        indexing="ij",
    )
    if jobs > 1:
        ctx = multiprocessing.get_context("fork")
        pool = ctx.Pool(jobs)
    chunk_results = (
        pool.imap_unordered(_write_chunk_unit, sorted(needed))
        if pool is not None
        else map(_write_chunk_unit, sorted(needed))
    )
    usage = {}
    written = 0
    for _, u in chunk_results:
        for bid, cnt in u.items():
            usage[bid] = usage.get(bid, 0) + cnt
        written += 1
    if pool is not None:
        pool.close()
        pool.join()
        pool = None

    with open(world_dir / "world_type", "w") as f:
        f.write("flat\n")

    if len(placed) == 1:
        dims = placed[0][1]
        print(f"Model dims: {dims[0]} x {dims[1]} x {dims[2]} blocks")
    else:
        print("Models placed:")
        for (path, *_), (_, dims, x0, _, _) in zip(models, placed):
            print(
                f"  {Path(path).name}: {dims[0]} x {dims[1]} x {dims[2]}"
                f" blocks @ x={x0}"
            )
    print(f"Voxels placed: {sum(int((g >= 0).sum()) for g, *_ in placed)}")
    print(f"Chunks written: {written}  ->  {world_dir}")
    print("Top blocks:")
    for bid, cnt in sorted(usage.items(), key=lambda kv: -kv[1])[:10]:
        print(f"  {names.get(bid, bid)}: {cnt}")
    print(f"\nRun: ./out/linux/release/RepGame {args.world}")


if __name__ == "__main__":
    main()
