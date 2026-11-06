import os
import tempfile
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import metrics


def _rec(tick, light_jobs, pending):
    return {"type": "metric", "tick": tick,
            "loader": {"light_jobs": light_jobs, "light_pending": pending}}


class TestFlatten(unittest.TestCase):
    def test_dotted(self):
        flat = metrics.flatten(_rec(20, 3.5, 0))
        self.assertEqual(flat["tick"], 20)
        self.assertEqual(flat["loader.light_jobs"], 3.5)
        self.assertEqual(flat["loader.light_pending"], 0)


class TestCsv(unittest.TestCase):
    def test_rows(self):
        recs = [_rec(0, 1, 2), _rec(20, 3, 4)]
        header, rows = metrics.csv_rows(recs)
        self.assertEqual(header[0], "tick")
        self.assertIn("loader.light_jobs", header)
        self.assertEqual(rows[0][0], 0)
        self.assertEqual(rows[1][1:], [3, 4])


class TestAfterSettle(unittest.TestCase):
    def test_filter(self):
        recs = [_rec(0, 1, 0), _rec(10, 2, 0), _rec(20, 3, 0)]
        recs.insert(0, {"type": "event", "name": "fly_settled", "tick": 5})
        out = metrics.metric_records(recs, after_settle=True)
        self.assertEqual([r["tick"] for r in out], [10, 20])


class TestPlot(unittest.TestCase):
    def test_png_exists(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("PIL not installed")
        recs = [_rec(t, t % 7, 3) for t in range(0, 100, 20)]
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "metrics.png")
            metrics.plot(recs, ["loader.light_jobs"], out)
            self.assertTrue(os.path.exists(out))
            with open(out, "rb") as f:
                self.assertEqual(f.read(4), b"\x89PNG")


if __name__ == "__main__":
    unittest.main()
