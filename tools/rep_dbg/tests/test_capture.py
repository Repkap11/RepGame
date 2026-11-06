import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import capture, imgdiff
from PIL import Image


def _img(path, color=(10, 20, 30), size=(16, 12)):
    im = Image.new("RGB", size, color)
    im.save(path)
    return path


class TestDiffMetrics(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.dir = self.td.name

    def tearDown(self):
        self.td.cleanup()

    def _p(self, name):
        return os.path.join(self.dir, name)

    def test_identical(self):
        a = _img(self._p("a.png"))
        d = imgdiff.compare(a, a)
        self.assertEqual(d["changed_px"], 0)
        self.assertEqual(d["max_delta"], 0)
        self.assertIsNone(d["bbox"])

    def test_one_pixel(self):
        a = _img(self._p("a.png"))
        b = _img(self._p("b.png"))
        im = Image.open(b)
        im.putpixel((3, 2), (10 + 50, 20, 30))
        im.save(b)
        d = imgdiff.compare(a, b, threshold=8)
        self.assertEqual(d["changed_px"], 1)
        self.assertEqual(d["bbox"], [3, 2, 3, 2])
        self.assertEqual(d["max_delta"], 50)

    def test_threshold(self):
        a = _img(self._p("a.png"))
        b = _img(self._p("b.png"))
        im = Image.open(b)
        im.putpixel((0, 0), (10 + 5, 20, 30))
        im.save(b)
        self.assertEqual(imgdiff.compare(a, b, threshold=8)["changed_px"], 0)
        self.assertEqual(imgdiff.compare(a, b, threshold=2)["changed_px"], 1)

    def test_size_mismatch(self):
        a = _img(self._p("a.png"))
        b = _img(self._p("b.png"), size=(8, 8))
        with self.assertRaises(ValueError):
            imgdiff.compare(a, b)

    def test_outputs(self):
        a = _img(self._p("a.png"))
        b = _img(self._p("b.png"))
        im = Image.open(b)
        im.putpixel((4, 4), (255, 0, 0))
        im.save(b)
        heat = imgdiff.heat_png(a, b, self._p("heat.png"))
        self.assertTrue(os.path.exists(heat))
        side = imgdiff.side_png(a, b, heat, self._p("side.png"))
        self.assertTrue(os.path.exists(side))
        self.assertEqual(Image.open(side).width, 16 * 3 + 8)


class TestSpec(unittest.TestCase):
    SPEC = "1.5,80,2.5,30,-10"

    def test_parse(self):
        self.assertEqual(capture.parse_views(self.SPEC)[0][:5], [1.5, 80, 2.5, 30, -10])
        self.assertEqual(capture.parse_views("1,2,3,4,5,7")[0][5], 7)

    def test_sweep(self):
        views = capture.sweep_views(self.SPEC)
        self.assertEqual(len(views), len(capture.FLAG_BITS) + 1)
        self.assertEqual(views[0][5], 0)
        self.assertEqual([v[5] for v in views[1:]], [b for b, _ in capture.FLAG_BITS])
        for v in views:
            self.assertEqual(v[:5], [1.5, 80, 2.5, 30, -10])

    def test_whip(self):
        views = capture.whip_views(self.SPEC, 4, 30)
        self.assertEqual([v[3] for v in views], [30, 60, 90, 120])

    def test_format_roundtrip(self):
        spec = capture.format_views(capture.parse_views("1,2,3,4,5,1;6,7,8,9,10,0"))
        self.assertEqual(capture.parse_views(spec), [[1.0, 2.0, 3.0, 4.0, 5.0, 1],
                                                   [6.0, 7.0, 8.0, 9.0, 10.0, 0]])


class TestTga(unittest.TestCase):
    def test_convert(self):
        with tempfile.TemporaryDirectory() as td:
            tga = os.path.join(td, "capture_0_display.tga")
            Image.new("RGB", (8, 8), (1, 2, 3)).save(tga)
            png = capture.tga_to_png(tga)
            self.assertTrue(png.endswith(".png"))
            self.assertEqual(Image.open(png).size, (8, 8))


class TestContactSheet(unittest.TestCase):
    def test_grid(self):
        tiles = [(Image.new("RGB", (32, 24), (i * 20, 0, 0)), "t%d" % i)
                 for i in range(4)]
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "sheet.png")
            capture.contact_sheet(tiles, out, thumb_w=64)
            im = Image.open(out)
            self.assertEqual(im.width, 64 * 2)
            self.assertEqual(im.height, (int(24 * 64 / 32) + 16) * 2)


if __name__ == "__main__":
    unittest.main()
