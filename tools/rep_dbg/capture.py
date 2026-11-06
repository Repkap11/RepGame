"""rep-dbg shot/diff support: capture spec helpers, TGA->PNG, contact
sheets, result postprocessing, and bistable reruns."""

import math
import os
import re
import subprocess
import sys

FLAG_BITS = [
    (1, "lighting off"),
    (2, "texture detail off"),
    (4, "auto-rotate off"),
    (8, "fog off"),
    (16, "face shade off"),
    (32, "footprint-mip light off"),
    (64, "face grow off"),
    (128, "alpha/dither off"),
    (256, "dither off"),
]


def parse_views(spec):
    """'x,y,z,yaw,pitch[,flags];...' -> [[x,y,z,yaw,pitch,flags], ...]."""
    views = []
    for group in spec.split(";"):
        group = group.strip()
        if not group:
            continue
        parts = [float(p) for p in group.split(",")]
        if len(parts) < 5:
            raise ValueError("bad capture view %r (want x,y,z,yaw,pitch[,flags])" % group)
        views.append(parts[:5] + [int(parts[5]) if len(parts) > 5 else 0])
    return views


def format_views(views):
    return ";".join("%s,%s,%s,%s,%s%s" % (v[0], v[1], v[2], v[3], v[4],
                                         ("," + str(v[5])) if v[5] else "")
                    for v in views)


def sweep_views(spec):
    """Same camera once per debug-flag bit, plus a flags-0 baseline."""
    views = parse_views(spec)
    if len(views) != 1:
        raise ValueError("--flags-sweep wants a single view")
    base = views[0]
    out = [[*base[:5], 0]]
    out += [[*base[:5], bit] for bit, _label in FLAG_BITS]
    return out


def whip_views(spec, n, step):
    """N views, yaw increasing by `step` degrees from the F12 base."""
    views = parse_views(spec)
    if len(views) != 1:
        raise ValueError("--whip wants a single view")
    base = views[0]
    return [[base[0], base[1], base[2], base[3] + i * step, base[4], base[5]]
            for i in range(n)]


def tga_to_png(tga_path):
    from PIL import Image
    png = tga_path[:-4] + ".png" if tga_path.endswith(".tga") else tga_path + ".png"
    Image.open(tga_path).save(png)
    return png


def contact_sheet(tiles, out, thumb_w=480, label_h=16):
    """tiles: [(pil_image, label)] -> grid PNG with labels under each tile."""
    from PIL import Image, ImageDraw
    n = len(tiles)
    cols = max(1, math.ceil(math.sqrt(n)))
    rows = math.ceil(n / cols)
    tw = thumb_w
    th = int(tiles[0][0].height * tw / tiles[0][0].width)
    canvas = Image.new("RGB", (cols * tw, rows * (th + label_h)), (30, 30, 30))
    d = ImageDraw.Draw(canvas)
    for i, (im, label) in enumerate(tiles):
        x, y = (i % cols) * tw, (i // cols) * (th + label_h)
        canvas.paste(im.resize((tw, th)), (x, y + label_h))
        d.text((x + 4, y + 2), label, fill="white")
    canvas.save(out)
    return out


def postprocess(run_dir, result, mode=None, log=sys.stderr):
    """shot runs: TGA->PNG for each capture_<i>_display.tga, join the
    capture_shot events into artifacts.screenshots, build sweep/whip
    sheets."""
    shots = {}
    with open(os.path.join(run_dir, "results.jsonl"), errors="replace") as f:
        import json
        for line in f:
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("type") == "event" and rec.get("name") == "capture_shot":
                shots[rec.get("view")] = rec
    spec = (result.get("env") or {}).get("REPGAME_CAPTURE", "")
    views = []
    try:
        views = parse_views(spec)
    except ValueError:
        pass
    shot_dir = os.path.join(run_dir, "screenshots")
    entries = []
    for fname in sorted(os.listdir(shot_dir)) if os.path.isdir(shot_dir) else []:
        m = re.match(r"capture_(\d+)_display\.tga$", fname)
        if not m:
            continue
        i = int(m.group(1))
        tga = "screenshots/" + fname
        png = os.path.relpath(tga_to_png(os.path.join(shot_dir, fname)), run_dir)
        ev = shots.get(i, {})
        entries.append({"view": i, "file": png, "tga": tga,
                        "flags": views[i][5] if i < len(views) else None,
                        "timeout": bool(ev.get("timeout"))})
    result.setdefault("artifacts", {})["screenshots"] = entries
    for e in entries:
        print(e["file"], file=log)
    if mode in ("sweep", "whip") and entries:
        from PIL import Image
        tiles = []
        for e in entries:
            flag = e["flags"]
            label = "baseline" if flag == 0 else (
                dict(FLAG_BITS).get(flag, "flags %s" % flag) if mode == "sweep"
                else "view %d" % e["view"])
            tiles.append((Image.open(os.path.join(run_dir, e["file"])), label))
        name = mode + ".png"
        contact_sheet(tiles, os.path.join(run_dir, name))
        result["artifacts"][mode] = name
        print(name, file=log)


def rerun(result, root, log=sys.stderr):
    """Re-execute a recorded run once (same cmd/env) in a fresh run dir for
    the bistable-capture_0 recheck. Returns the new run dir."""
    from . import runner as runner_mod
    run_dir, _id = runner_mod.new_run_dir(root, "rerun")
    home = os.path.join(run_dir, "home")
    os.makedirs(os.path.join(home, ".repgame"), exist_ok=True)
    env = dict(os.environ)
    env["HOME"] = home
    env["SDL_VIDEODRIVER"] = "offscreen"
    env["SDL_AUDIODRIVER"] = "dummy"
    env.update(result.get("env") or {})
    argv = result["cmd"]
    print("rep-dbg: rerun %s" % " ".join(argv), file=log)
    with open(os.path.join(run_dir, "stdout.log"), "w") as logf:
        proc = subprocess.Popen(argv, cwd=run_dir, env=env,
                                stdout=logf, stderr=subprocess.STDOUT)
        code = proc.wait()
    shot_dir = os.path.join(run_dir, "screenshots")
    pngs = {}
    for fname in sorted(os.listdir(shot_dir)) if os.path.isdir(shot_dir) else []:
        m = re.match(r"capture_(\d+)_display\.tga$", fname)
        if m:
            pngs[int(m.group(1))] = tga_to_png(os.path.join(shot_dir, fname))
    return run_dir, pngs, code
