"""`rep-dbg inspect` — wraps `RepGame inspect` (offline world inspector).

Target resolution: a run id or run dir maps to <run>/home/.repgame/<world>
(world taken from result.json's cmd). A path (contains '/'; write
./World1 for a cwd-relative dir) is passed through. Anything else is a
world name, resolved by the binary under $HOME/.repgame.
"""

import hashlib
import json
import os
import subprocess
import sys

from . import results as results_mod

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(REPO, "out/linux/release/RepGame")


def resolve_target(arg, root=None):
    """Returns (world_dir_or_name, description)."""
    run_dir = None
    if "/" in arg and os.path.isdir(arg):
        if os.path.isfile(os.path.join(arg, "result.json")):
            run_dir = arg
        else:
            return arg, "path"
    else:
        try:
            # 'last', run ids (<name>/<ts>-<n>), or run dirs under --root.
            run_dir = results_mod.resolve(arg, root)
        except LookupError:
            return (arg, "path") if "/" in arg else (arg, "world name")
    if run_dir is not None:
        try:
            result = json.load(open(os.path.join(run_dir, "result.json")))
            world = result["cmd"][1]
        except (OSError, KeyError, IndexError) as e:
            raise ValueError("run %s: no world in result.json cmd (%s)" % (run_dir, e))
        wdir = os.path.join(run_dir, "home", ".repgame", world)
        return wdir, "run %s world %s" % (os.path.basename(run_dir), world)
    return arg, "world name"


def run_inspect(binary, target, sub_args, env=None):
    argv = [binary, "inspect", target] + list(sub_args)
    e = dict(os.environ)
    if env:
        e.update(env)
    return subprocess.run(argv, env=e)


def block_color(name):
    """Stable per-name RGB: hash the block name."""
    h = hashlib.sha1(name.encode()).digest()
    r, g, b = h[0], h[1], h[2]
    # Keep colors readable: mix toward mid brightness.
    return (64 + r % 160, 64 + g % 160, 64 + b % 160)


def slice_png(obj, out_path, scale=8, legend_rows=18):
    """Render a slice JSON object to a PNG: one colour per block name,
    `scale`x blocks, plus a legend strip on the right."""
    from PIL import Image, ImageDraw

    rows = obj["rows"]
    legend = obj["legend"]  # char -> name
    w, h = len(rows[0]), len(rows)
    names = sorted(legend.values())
    legend_w = 0
    if names:
        legend_w = 120
    img = Image.new("RGB", (w * scale + legend_w, h * scale), (0, 0, 0))
    draw = ImageDraw.Draw(img)
    for z, row in enumerate(rows):
        for x, ch in enumerate(row):
            color = block_color(legend.get(ch, "AIR")) if ch != " " else (12, 12, 16)
            draw.rectangle([x * scale, z * scale, (x + 1) * scale - 1, (z + 1) * scale - 1], fill=color)
    if names:
        ly = 4
        for name in names:
            color = block_color(name)
            draw.rectangle([w * scale + 6, ly, w * scale + 16, ly + 10], fill=color)
            draw.text((w * scale + 20, ly), name, fill=(230, 230, 230))
            ly += legend_rows
    img.save(out_path)
    return out_path


def do_inspect(opts):
    """Captures stdout so --png can be rendered and --json output is
    printed verbatim."""
    try:
        target, desc = resolve_target(opts.target, opts.root)
    except ValueError as e:
        print("rep-dbg: %s" % e, file=sys.stderr)
        return 4
    args = list(opts.args or [])
    png_out = None
    if "--png" in args:
        i = args.index("--png")
        if i + 1 >= len(args):
            print("rep-dbg: --png needs an output path", file=sys.stderr)
            return 4
        png_out = args[i + 1]
        del args[i:i + 2]
    want_json = opts.json or png_out is not None
    if want_json and "--json" not in args:
        args.append("--json")
    print("rep-dbg: inspect %s (%s)" % (target, desc), file=sys.stderr)
    argv = [opts.binary or BINARY, "inspect", target] + args
    proc = subprocess.run(argv, stdout=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        return proc.returncode
    out = proc.stdout.strip()
    if png_out is not None:
        try:
            obj = json.loads(out)
        except json.JSONDecodeError:
            print(out, file=sys.stderr)
            print("rep-dbg: --png: expected JSON slice output", file=sys.stderr)
            return 4
        slice_png(obj, png_out)
        print("rep-dbg: wrote %s" % png_out, file=sys.stderr)
        return 0
    print(out)
    return 0
