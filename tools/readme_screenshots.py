#!/usr/bin/env python3
"""Capture the README screenshot set via `rep-dbg shot` (REPGAME_CAPTURE).

Each scene is a deterministic capture run against the MAP_SEED=0 world:
isolated $HOME, pinned camera, fixed world time, HUD off. The display PNG is
copied into screenshots/<scene>.png for the README to reference.

Scenes:
  overworld    legacy generator, spawn view: camera at 0,0 facing +X a few
               blocks up — the same shot as screenshots/repgame.png
  nether       the nether cavern band (slot 1, encoded y -5952..-1985, see
               docs/dimensions.md): lava sea + shelf ledges at x/z 0
  new_terrain  the repheim band (slot 3) — the reworked generator, identical
               to a new-gen overworld at the same x/z. Same camera as the
               overworld shot, at the repheim-encoded y.
  end          the End (slot 2): island + obsidian pillars at x/z 0

Usage:
  tools/readme_screenshots.py                 # every scene
  tools/readme_screenshots.py overworld end   # a subset
  tools/readme_screenshots.py --out /tmp/shots --quality 1
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REP_DBG = os.path.join(REPO, "tools", "rep-dbg")

# Camera specs are REPGAME_CAPTURE view strings: "x,y,z,yaw,pitch".
# yaw: 0 faces -z (north), 90 faces +x (east). pitch > 0 looks down.
# Encoded block y per docs/dimensions.md / dimension.hpp:
#   nether  = local - 5952        (local ~110: above the lava sea, under the roof)
#   repheim = rep frame y - 7936  (rep_y >= 0; rep frame matches overworld y)
#   end     = 1984 + local y      (local ~2048 island surface, ~2074 above it)
SCENES = {
    "overworld": {"build": "release", "spec": "0.5,66,0.5,90,9"},
    "nether": {"build": "release", "spec": "96,-5860,0,270,15"},
    "new_terrain": {"build": "release", "spec": "300,-7856,200,225,10"},
    "end": {"build": "release", "spec": "130,4058,0.5,270,14"},
}


def run_scene(name, scene, out_dir, quality):
    # A cold world takes ~11k ticks to drain chunk/light queues before the
    # shot can fire; give the settle gate room so it wins over the timeout.
    cmd = [sys.executable, REP_DBG, "shot", scene["spec"],
           "--build", scene["build"], "--name", "readme_" + name,
           "--settle", "60", "--timeout", "18000"]
    if quality is not None:
        cmd += ["--quality", str(quality)]
    run_group = os.path.join(
        os.environ.get("REP_DBG_ROOT", os.path.expanduser("~/.repgame/rep-dbg")),
        "readme_" + name)
    before = set(os.listdir(run_group)) if os.path.isdir(run_group) else set()
    proc = subprocess.run(cmd, cwd=REPO, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout[-4000:])
        print("%s: rep-dbg shot failed (exit %d)" % (name, proc.returncode),
              file=sys.stderr)
        return None
    new = [d for d in os.listdir(run_group) if d not in before]
    if len(new) != 1:
        print("%s: expected 1 new run dir in %s, found %s"
              % (name, run_group, new), file=sys.stderr)
        return None
    run_dir = os.path.join(run_group, new[0])
    with open(os.path.join(run_dir, "result.json")) as f:
        result = json.load(f)
    if result.get("status") != "ok":
        print("%s: run status %s (%s)" % (name, result.get("status"), run_dir),
              file=sys.stderr)
        return None
    shots = (result.get("artifacts") or {}).get("screenshots") or []
    if not shots:
        print("%s: no screenshots in result" % name, file=sys.stderr)
        return None
    src = os.path.join(run_dir, shots[0]["file"])
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, name + ".png")
    shutil.copyfile(src, dst)
    print("%s -> %s" % (name, dst))
    return dst


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenes", nargs="*", metavar="scene",
                    help="scenes to capture (default: all); one of: "
                         + ", ".join(sorted(SCENES)))
    ap.add_argument("--out", default=os.path.join(REPO, "screenshots"),
                    help="output dir (default: screenshots/)")
    ap.add_argument("--quality", type=int, default=None,
                    help="REPGAME_CAPTURE_QUALITY 0..3 (default: game default)")
    opts = ap.parse_args()

    bad = [n for n in opts.scenes if n not in SCENES]
    if bad:
        ap.error("unknown scene(s): %s (choose from %s)"
                 % (", ".join(bad), ", ".join(sorted(SCENES))))
    names = opts.scenes or list(SCENES)
    ok = True
    for name in names:
        if run_scene(name, SCENES[name], opts.out, opts.quality) is None:
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
