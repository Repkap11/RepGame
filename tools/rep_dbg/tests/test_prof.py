import unittest

from rep_dbg import stats


METRIC_RECS = [
    {"type": "metric", "tick": 20,
     "loader": {"light_jobs": 3},
     "prof": {"tick": {"n": 20, "us": 8200, "max_us": 900},
              "world_draw": {"n": 15, "us": 12000, "max_us": 2000}}},
    {"type": "metric", "tick": 40,
     "loader": {"light_jobs": 1},
     "prof": {"tick": {"n": 20, "us": 7600, "max_us": 850}}},
]


class ProfStatsTest(unittest.TestCase):
    def test_prof_flatten(self):
        out = stats.compute_stats(METRIC_RECS)
        tick_us = out["metric.prof.tick.us"]
        self.assertAlmostEqual(tick_us["mean"], (8200 + 7600) / 2)
        self.assertEqual(tick_us["max"], 8200)
        self.assertEqual(tick_us["last"], 7600)
        self.assertEqual(out["metric.prof.world_draw.us"]["mean"], 12000)
        self.assertEqual(out["metric.prof.world_draw.n"]["mean"], 15)


if __name__ == "__main__":
    unittest.main()
