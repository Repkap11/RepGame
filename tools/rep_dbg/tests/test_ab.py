import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import ab
from rep_dbg import scenarios


class TestScenarios(unittest.TestCase):
    def test_every_window_names_a_scenario(self):
        for name in scenarios.WINDOWS:
            self.assertIn(name, scenarios.SCENARIOS)
            self.assertIn("after_event", scenarios.WINDOWS[name])

    def test_mob_stress(self):
        env = scenarios.SCENARIOS["mob-stress"]
        self.assertEqual(env["REPGAME_PROFILE"], "1")
        self.assertTrue(int(env["REPGAME_MOB_WORLD"].split(",")[0]) > 0)
        # Parked: fly speed 0, so the window measures a stationary camera.
        self.assertEqual(env["REPGAME_FLY"].split(",")[5], "0")
        fields = scenarios.window_metrics("mob-stress")
        self.assertIn("window.prof.mobs_render.us_per_call", fields)
        self.assertIn("stats.metric.mobs.total.mean", fields)
        self.assertEqual(fields[:len(scenarios.WINDOW_METRICS)], scenarios.WINDOW_METRICS)

    def test_window_metrics_default(self):
        self.assertEqual(scenarios.window_metrics("idle"), scenarios.WINDOW_METRICS)

    def test_cfg_spec_keeps_mob_world_commas(self):
        tree, env = ab.parse_spec("current:REPGAME_MOB_WORLD=2000,ai=0,pig")
        self.assertEqual(tree, "current")
        self.assertEqual(env, {"REPGAME_MOB_WORLD": "2000,ai=0,pig"})


class TestSpec(unittest.TestCase):
    def test_current(self):
        self.assertEqual(ab.parse_spec("current"), ("current", {}))

    def test_env(self):
        tree, env = ab.parse_spec("current:REPGAME_LIGHT_SMOOTH=1")
        self.assertEqual(tree, "current")
        self.assertEqual(env, {"REPGAME_LIGHT_SMOOTH": "1"})

    def test_rev_and_multi_env(self):
        tree, env = ab.parse_spec("rev:HEAD~3:REPGAME_LIGHT_SMOOTH=1:REPGAME_FLY=1,2,3")
        self.assertEqual(tree, "rev:HEAD~3")
        self.assertEqual(env, {"REPGAME_LIGHT_SMOOTH": "1", "REPGAME_FLY": "1,2,3"})

    def test_bad_env(self):
        with self.assertRaises(ValueError):
            ab.parse_spec("current:NOEQUALS")


class TestInterleave(unittest.TestCase):
    def test_order(self):
        self.assertEqual(
            ab.interleave_order(["a", "b", "c"], 2),
            [(0, "a"), (0, "b"), (0, "c"), (1, "a"), (1, "b"), (1, "c")])


class TestAggregate(unittest.TestCase):
    def test_basic(self):
        agg = ab.aggregate([1.0, 3.0, 5.0])
        self.assertEqual(agg["median"], 3.0)
        self.assertAlmostEqual(agg["mean"], 3.0)
        self.assertEqual(agg["min"], 1.0)
        self.assertEqual(agg["max"], 5.0)
        self.assertAlmostEqual(agg["stddev"], (8.0 / 3) ** 0.5)


def _agg(vals):
    return ab.aggregate(vals)


class TestVerdict(unittest.TestCase):
    def test_clear(self):
        per_cfg = {"a": _agg([10, 10.2, 9.9]), "b": _agg([14, 14.2, 13.9])}
        self.assertEqual(ab.verdict(per_cfg), "clear")

    def test_noisy(self):
        per_cfg = {"a": _agg([1, 20, 5, 40]), "b": _agg([30, 31, 29])}
        self.assertEqual(ab.verdict(per_cfg), "noisy")

    def test_within_noise(self):
        # Medians differ but each cfg's own min-max spread is bigger.
        per_cfg = {"a": _agg([10, 10.4, 10.8]), "b": _agg([10.3, 10.6, 10.9])}
        self.assertEqual(ab.verdict(per_cfg), "within_noise")

    def test_metric_table(self):
        per_metric = {"m": {"a": _agg([1, 2, 3, 30]), "b": _agg([50, 51, 52])}}
        table = ab.metric_table(per_metric)
        self.assertEqual(table["m"]["verdict"], "noisy")
        self.assertTrue(table["m"]["noisy"])
        self.assertIn("a", table["m"]["cfgs"])

    def test_summary_md_markers(self):
        result = {"cfg_order": ["a", "b"], "metrics": {
            "loud": {"verdict": "noisy", "noisy": True,
                     "cfgs": {"a": _agg([1, 30]), "b": _agg([40, 41])}},
            "close": {"verdict": "within_noise", "noisy": False,
                      "cfgs": {"a": _agg([10, 10.4, 10.8]), "b": _agg([10.3, 10.6, 10.9])}},
            "win": {"verdict": "clear", "noisy": False,
                    "cfgs": {"a": _agg([10, 10.1, 9.9]), "b": _agg([20, 20.1, 19.9])}},
        }}
        md = ab.summary_md(result)
        self.assertIn("loud **noisy**", md)
        self.assertIn("close (within noise)", md)
        self.assertIn("| win |", md)


if __name__ == "__main__":
    unittest.main()
