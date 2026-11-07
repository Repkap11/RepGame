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

    def test_us_per_call(self):
        recs = [{"type": "metric", "tick": 20,
                 "prof": {"draw_frame": {"n": 4, "us": 1000, "max_us": 400},
                          "zero": {"n": 0, "us": 0, "max_us": 0}}}]
        out = stats.compute_stats(recs)
        self.assertEqual(out["metric.prof.draw_frame.us_per_call"]["mean"], 250)
        self.assertNotIn("metric.prof.zero.us_per_call", out)
        # records are not mutated
        self.assertNotIn("us_per_call", recs[0]["prof"]["draw_frame"])


if __name__ == "__main__":
    unittest.main()
