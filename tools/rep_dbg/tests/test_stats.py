import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import stats


class TestStats(unittest.TestCase):
    def test_aggregate(self):
        agg = stats.aggregate([1, 2, 3, 4])
        self.assertEqual(agg["mean"], 2.5)
        self.assertEqual(agg["max"], 4)
        self.assertEqual(agg["last"], 4)
        self.assertEqual(agg["p50"], 2)
        self.assertIn("p95", agg)

    def test_compute_stats_keys(self):
        metrics = [
            {"type": "metric", "tick": 20, "fly_phase": 1, "rd": 6,
             "pos": [1.0, 2.0, 3.0],
             "loader": {"light_jobs": 5, "light_seeds": 10},
             "light": {"loop": 1}},
            {"type": "metric", "tick": 40, "fly_phase": 1, "rd": 6,
             "pos": [2.0, 2.0, 3.0],
             "loader": {"light_jobs": 7, "light_seeds": 4},
             "light": {"loop": 2}},
        ]
        s = stats.compute_stats(metrics)
        self.assertEqual(s["metric.loader.light_jobs"]["mean"], 6.0)
        self.assertEqual(s["metric.loader.light_seeds"]["max"], 10)
        self.assertEqual(s["metric.pos.0"]["last"], 2.0)
        self.assertNotIn("metric.tick", s)

    def test_after_settle(self):
        metrics = [
            {"type": "metric", "tick": 10, "loader": {"light_jobs": 100}},
            {"type": "metric", "tick": 30, "loader": {"light_jobs": 2}},
            {"type": "metric", "tick": 50, "loader": {"light_jobs": 4}},
        ]
        s = stats.compute_stats(metrics, settle_tick=20)
        self.assertEqual(s["metric.loader.light_jobs_after_settle"]["mean"], 3.0)
        self.assertEqual(s["metric.loader.light_jobs"]["mean"], (100 + 2 + 4) / 3)


if __name__ == "__main__":
    unittest.main()
