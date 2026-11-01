#!/usr/bin/env python3
"""Voxelize a 3D model into colored blocks and write a RepGame flat world.

Usage:
    model_to_world.py MODEL --world NAME [--size 64] [--up y|z] [--x 4]
                        [--repgame-dir ~/.repgame] [--no-glass] [--force]
                        [--color NAME=#RRGGBB]...

Writes chunk files (the same RLE format MapStorage persists) for a flat
world under <repgame-dir>/<world>/ plus a `world_type` marker file so the
client generates a sandstone floor under the model.
"""

import argparse
import json
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


def load_geometries(path, up):
    """Baked world-space meshes, one per scene-graph geometry node."""
    scene = trimesh.load(path, force="scene")
    parts = []
    for node in scene.graph.nodes_geometry:
        transform, geom_name = scene.graph[node]
        geom = scene.geometry[geom_name]
        if not isinstance(geom, trimesh.Trimesh):
            continue
        g = geom.copy()
        g.apply_transform(transform)
        parts.append(g)
    if not parts:
        sys.exit("No triangle geometry found in model.")
    if up == "z":
        # Z-up model -> Y-up world: rotate -90 degrees about x.
        rot = trimesh.transformations.rotation_matrix(-np.pi / 2, [1, 0, 0])
        for g in parts:
            g.apply_transform(rot)
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


def material_rgba_image(geom, overrides=None):
    """Per-geometry color source.

    Returns (img_rgba_float_0_255 or None, uv_faces (n,3,2) or None,
    face_rgba (n,4) 0..255, label, effective mean rgba).

    `overrides` maps material name -> sRGB (0..255). A matching material is
    painted that flat color (no texture/factor); alpha comes from the
    material's factor when present.

    Texture: PBR baseColorTexture * baseColorFactor, or SimpleMaterial
    image/diffuse. glTF baseColorFactor is LINEAR and baseColorTexture is
    sRGB — combine in linear space and convert back to sRGB for palette
    matching. KHR_texture_transform is ignored (base UVs used as authored).
    Fallbacks: face/vertex colors (treated as sRGB), material color (linear),
    light gray.
    """
    n = len(geom.faces)
    visual = geom.visual
    colors = np.tile(np.array([204.0, 204.0, 204.0, 255.0]), (n, 1))

    uv = getattr(visual, "uv", None)
    mat = getattr(visual, "material", None)
    label = getattr(mat, "name", None) or type(mat).__name__ if mat is not None else "none"
    if overrides and mat is not None and getattr(mat, "name", None) in overrides:
        rgb = overrides[mat.name]
        a = 255.0
        if getattr(mat, "baseColorFactor", None) is not None:
            a = np.clip(norm_factor(mat.baseColorFactor)[3] * 255.0, 0, 255)
        colors[:, :3] = rgb
        colors[:, 3] = a
        eff = np.concatenate([rgb, [a]])
        return None, None, colors, f"{mat.name} (override)", eff
    if uv is not None and len(uv) >= geom.vertices.shape[0]:
        img = getattr(mat, "image", None) if mat is not None else None
        if img is None and mat is not None and hasattr(mat, "baseColorTexture"):
            img = mat.baseColorTexture
        factor = np.ones(4)  # linear-space multiplier
        if mat is not None and getattr(mat, "baseColorFactor", None) is not None:
            factor = norm_factor(mat.baseColorFactor)
        elif mat is not None and getattr(mat, "diffuse", None) is not None:
            factor = norm_factor(mat.diffuse)
        if img is not None:
            arr = np.asarray(img.convert("RGBA"), dtype=np.float64)
            # texture rgb is sRGB, factor is linear: multiply in linear space
            rgb = linear_to_srgb(srgb_to_linear(arr[..., :3] / 255.0) * factor[:3]) * 255.0
            arr[..., :3] = np.clip(rgb, 0, 255)
            arr[..., 3] = np.clip(arr[..., 3] * factor[3], 0, 255)
            eff = np.concatenate([arr[..., :3].reshape(-1, 3).mean(0), [arr[..., 3].mean()]])
            return arr, uv[geom.faces], colors, label, eff
        # textured uv but no image: fall through to color sources

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
    return None, None, colors, label, colors.mean(axis=0)


def sample_image(img, uv):
    """Nearest-pixel texture lookup, repeat wrap, v flipped (trimesh)."""
    h, w = img.shape[:2]
    px = np.clip((np.mod(uv[:, 0], 1.0) * w).astype(np.int64), 0, w - 1)
    py = np.clip((np.mod(1.0 - uv[:, 1], 1.0) * h).astype(np.int64), 0, h - 1)
    return img[py, px]


def nearest_palette(lab, pal_lab, pal_ids):
    d = ((lab[:, None, :] - pal_lab[None, :, :]) ** 2).sum(-1)
    return pal_ids[d.argmin(1)]


def main():
    ap = argparse.ArgumentParser(description="Voxelize a 3D model into a RepGame flat world.")
    ap.add_argument("model")
    ap.add_argument("--world", required=True)
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--up", choices=["y", "z"], default="y")
    ap.add_argument("--x", type=int, default=4)
    ap.add_argument("--repgame-dir", default=os.path.expanduser("~/.repgame"))
    ap.add_argument("--no-glass", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument(
        "--color",
        action="append",
        default=[],
        metavar="NAME=#RRGGBB",
        help="Override a material's color (repeatable), e.g. --color Carpaint=#8E2323",
    )
    args = ap.parse_args()

    overrides = {}
    for spec in args.color:
        m = re.fullmatch(r"([^=]+)=#([0-9a-fA-F]{6})", spec.strip())
        if not m:
            sys.exit(f"Bad --color '{spec}' (want NAME=#RRGGBB)")
        r, g, b = (int(m.group(2)[i : i + 2], 16) for i in (0, 2, 4))
        overrides[m.group(1)] = np.array([r, g, b], dtype=np.float64)

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

    print(f"Loading {args.model} ...")
    parts = load_geometries(args.model, args.up)
    nverts = sum(len(g.vertices) for g in parts)
    nfaces = sum(len(g.faces) for g in parts)
    bounds = np.array([g.bounds for g in parts])  # (G,2,3)
    bmin, bmax = bounds[:, 0].min(0), bounds[:, 1].max(0)
    extents = bmax - bmin
    print(f"  {nverts} verts, {nfaces} tris across {len(parts)} geometries, extents {extents}")

    scale = args.size / float(extents.max())
    origin = bmin
    dims = np.maximum(np.ceil(extents * scale), 1).astype(np.int64) + 1
    print(f"  voxel dims: {dims[0]} x {dims[1]} x {dims[2]} blocks")

    # --- sampling pass: collect (vox, block_id[, glass]) votes --------------
    # combined key per sample: vox_linear * 4096 + (bid | 2048 for glass).
    # Each sample votes with weight tri_area/samples_in_tri (voxel units) so
    # dense tiny-triangle meshes don't outvote large smooth panels.
    vote_keys = []
    vote_w = []
    mat_stats = {}  # label -> [tri_count, effective mean rgba]
    matched_overrides = set()
    for gi, geom in enumerate(parts):
        gv = ((geom.vertices - origin) * scale).astype(np.float64)
        tris = gv[geom.faces]  # (n,3,3)
        img, uv_faces, face_rgba, label, eff = material_rgba_image(geom, overrides)
        mat_stats.setdefault(label, [0, eff])
        mat_stats[label][0] += len(geom.faces)
        if overrides and label.endswith(" (override)"):
            matched_overrides.add(label[: -len(" (override)")])
        for lo in range(0, len(tris), BATCH_TRIS):
            hi = min(lo + BATCH_TRIS, len(tris))
            t = tris[lo:hi]
            e01 = t[:, 1] - t[:, 0]
            e02 = t[:, 2] - t[:, 0]
            e12 = t[:, 2] - t[:, 1]
            areas = 0.5 * np.linalg.norm(np.cross(e01, e02), axis=1)
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
                ii, jj = np.meshgrid(
                    np.arange(n + 1), np.arange(n + 1), indexing="ij"
                )
                m = (ii + jj) <= n
                # barycentric lattice sample points (i/n, j/n), i+j<=n
                B = np.stack([ii[m] / n, jj[m] / n], axis=1).astype(np.float64)
                B = np.concatenate([B, 1.0 - B.sum(1, keepdims=True)], axis=1)
                pts = np.einsum("tcd,sc->tsd", tt, B).reshape(-1, 3)
                tri_of = np.repeat(idx, len(B))
                b_of = np.tile(np.arange(len(B)), len(idx))
                vox = np.clip(np.floor(pts), 0, dims - 1).astype(np.int64)
                del pts
                vlin = (vox[:, 0] * dims[1] + vox[:, 1]) * dims[2] + vox[:, 2]
                del vox
                if img is not None:
                    suv = np.einsum(
                        "sd,sdv->sv", B[b_of], uv_faces[lo + tri_of]
                    )
                    rgba = sample_image(img, suv)
                    del suv
                else:
                    rgba = face_rgba[lo + tri_of]
                lab = srgb_to_lab(rgba[:, :3] / 255.0)
                alpha = rgba[:, 3] / 255.0
                del rgba
                keys = np.empty(len(vlin), np.int64)
                gm = (
                    alpha < ALPHA_THRESHOLD
                    if glass_lab is not None
                    else np.zeros(len(vlin), bool)
                )
                om = ~gm
                if om.any():
                    keys[om] = vlin[om] * 4096 + nearest_palette(
                        lab[om], opaque_lab, opaque_ids
                    )
                if gm.any():
                    keys[gm] = vlin[gm] * 4096 + 2048 + nearest_palette(
                        lab[gm], glass_lab, glass_ids
                    )
                vote_keys.append(keys)
                vote_w.append(np.repeat(areas[idx] / len(B), len(B)))
                del keys, vlin, lab
            del t, e, ns
        print(f"  sampled geometry {gi + 1}/{len(parts)}", flush=True)
        del tris, gv

    for name in set(overrides) - matched_overrides:
        print(f"warning: --color '{name}' matched no material", file=sys.stderr)

    if args.verbose:
        print("Materials by tri count:")
        for label, (ntri, eff) in sorted(
            mat_stats.items(), key=lambda kv: -kv[1][0]
        )[:15]:
            print(
                f"  {label[:48]:50s} tris={ntri:8d} "
                f"mean_sRGB=({eff[0]:.0f},{eff[1]:.0f},{eff[2]:.0f}) a={eff[3]:.0f}"
            )

    keys = np.concatenate(vote_keys)
    weights = np.concatenate(vote_w)
    del vote_keys, vote_w
    uk, inv = np.unique(keys, return_inverse=True)
    del keys
    wsum = np.bincount(inv, weights=weights)
    del inv, weights
    vox_of = uk >> 12
    low = uk & 4095
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
    winners = np.where(ow_u >= gw_u, obid_u, gbid_u)

    grid = np.full(int(np.prod(dims)), -1, np.int16)
    grid[uniq_vox] = winners.astype(np.int16)
    grid = grid.reshape(tuple(dims))
    nvox = int((grid >= 0).sum())
    print(f"  {nvox} voxels after voting")

    # --- placement: min y -> y=1, min x -> --x, z centered on 0 --------------
    x0 = args.x
    y0 = 1
    z0 = -(int(dims[2]) // 2)
    ii, jj, kk = np.nonzero(grid >= 0)
    wx, wy, wz = ii + x0, jj + y0, kk + z0

    # Chunks whose 64^3 range (halo included) contains a model voxel:
    # chunk cp covers world [62*cp - 1, 62*cp + 62], so a voxel at w belongs
    # to floor((w-1)/62), floor(w/62) and floor((w+1)/62).
    needed = set()
    for dx in (-1, 0, 1):
        cxs = (wx + dx) // CHUNK
        for dy in (-1, 0, 1):
            cys = (wy + dy) // CHUNK
            for dz in (-1, 0, 1):
                czs = (wz + dz) // CHUNK
                needed.update(zip(cxs.tolist(), cys.tolist(), czs.tolist()))

    usage = {}
    written = 0
    ly, lx, lz = np.meshgrid(
        np.arange(-1, CHUNK_INTERNAL - 1),
        np.arange(-1, CHUNK_INTERNAL - 1),
        np.arange(-1, CHUNK_INTERNAL - 1),
        indexing="ij",
    )  # index order: y, x, z (z fastest) matches (y+1)*64*64 + (x+1)*64 + (z+1)
    for cx, cy, cz in sorted(needed):
        wyv = cy * CHUNK + ly
        wxv = cx * CHUNK + lx
        wzv = cz * CHUNK + lz
        gx, gy, gz = wxv - x0, wyv - y0, wzv - z0
        inside = (
            (gx >= 0) & (gx < dims[0])
            & (gy >= 0) & (gy < dims[1])
            & (gz >= 0) & (gz < dims[2])
        )
        vals = np.full(gx.shape, -1, np.int64)
        if inside.any():
            vals[inside] = grid[gx[inside], gy[inside], gz[inside]]
        default = np.where(wyv <= 0, sandstone_id, 0)
        ids = np.where(vals >= 0, vals, default).ravel()
        # usage counts only interior cells (local 0..61), not the halo —
        # halo cells belong to the neighbouring chunk's interior.
        nonhalo = (lx >= 0) & (lx <= 61) & (ly >= 0) & (ly <= 61) & (lz >= 0) & (lz <= 61)
        for bid, cnt in zip(*np.unique(vals[nonhalo & (vals >= 0)], return_counts=True)):
            usage[int(bid)] = usage.get(int(bid), 0) + int(cnt)
        # RLE in index order
        change = np.flatnonzero(np.r_[True, ids[1:] != ids[:-1], True])
        rec = np.empty(len(change) - 1, RECORD_DTYPE)
        rec["id"] = ids[change[:-1]]
        rec["rotation"] = 0
        rec["power"] = 0
        rec["display_id"] = rec["id"]
        rec["waterlogged"] = 0
        rec["num"] = np.diff(change)
        with open(world_dir / f"chunk_{cx}_{cy}_{cz}", "wb") as f:
            f.write(struct.pack("<I", RECORD_SIZE))
            f.write(rec.tobytes())
        written += 1

    with open(world_dir / "world_type", "w") as f:
        f.write("flat\n")

    print(f"Model dims: {dims[0]} x {dims[1]} x {dims[2]} blocks")
    print(f"Voxels placed: {nvox}")
    print(f"Chunks written: {written}  ->  {world_dir}")
    print("Top blocks:")
    for bid, cnt in sorted(usage.items(), key=lambda kv: -kv[1])[:10]:
        print(f"  {names.get(bid, bid)}: {cnt}")
    print(f"\nRun: ./out/linux/release/RepGame {args.world}")


if __name__ == "__main__":
    main()
