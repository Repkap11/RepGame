import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from rep_dbg import inspect as inspect_mod


def _make_run(root, name, world="World1"):
    run_dir = os.path.join(root, name, "20260101-000000-1")
    wdir = os.path.join(run_dir, "home", ".repgame", world)
    os.makedirs(wdir)
    with open(os.path.join(run_dir, "result.json"), "w") as f:
        json.dump({"id": name + "/20260101-000000-1",
                   "cmd": ["/path/RepGame", world],
                   "status": "ok", "kind": "script"}, f)
    return run_dir, wdir


class TestResolve(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="repdbg_inspect_")
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)

    def test_run_id_resolves_to_world_dir(self):
        run_dir, wdir = _make_run(self.root, "myrun")
        target, desc = inspect_mod.resolve_target("myrun/20260101-000000-1", self.root)
        self.assertEqual(target, wdir)
        self.assertIn("World1", desc)

    def test_run_dir_resolves(self):
        run_dir, wdir = _make_run(self.root, "myrun")
        target, _ = inspect_mod.resolve_target(run_dir, self.root)
        self.assertEqual(target, wdir)

    def test_plain_dir_is_path(self):
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d, ignore_errors=True)
        target, desc = inspect_mod.resolve_target(d, self.root)
        self.assertEqual((target, desc), (d, "path"))

    def test_unknown_name_is_world_name(self):
        target, desc = inspect_mod.resolve_target("NoSuchWorld99", self.root)
        self.assertEqual((target, desc), ("NoSuchWorld99", "world name"))

    def test_missing_run_falls_back_to_path(self):
        target, desc = inspect_mod.resolve_target("missing/00000000-000000-1", self.root)
        self.assertEqual(desc, "path")


CANNED_SLICE = {
    "y": 1, "x1": 0, "z1": 0, "x2": 3, "z2": 2,
    "legend": {" ": "AIR", "a": "STONE", "b": "SANDSTONE"},
    "rows": ["aa  ", "abb ", "aaab"],
}


class TestSlicePng(unittest.TestCase):
    def test_png_written(self):
        out = os.path.join(tempfile.mkdtemp(), "slice.png")
        self.addCleanup(shutil.rmtree, os.path.dirname(out), ignore_errors=True)
        inspect_mod.slice_png(CANNED_SLICE, out)
        from PIL import Image
        im = Image.open(out).convert("RGB")
        self.assertEqual(im.width, 4 * 8 + 120)
        self.assertEqual(im.height, 3 * 8)
        # Tile (0,0) is 'a' = STONE, tile (3,0) is ' ' = AIR/dark.
        stone = inspect_mod.block_color("STONE")
        self.assertEqual(im.getpixel((4, 4)), stone)
        self.assertEqual(im.getpixel((3 * 8 + 4, 4)), (12, 12, 16))

    def test_colors_stable(self):
        self.assertEqual(inspect_mod.block_color("STONE"), inspect_mod.block_color("STONE"))
        self.assertNotEqual(inspect_mod.block_color("STONE"), inspect_mod.block_color("SANDSTONE"))


if __name__ == "__main__":
    unittest.main()
