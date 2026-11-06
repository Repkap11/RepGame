import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import ab


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


class TestNoise(unittest.TestCase):
    def test_clean(self):
        per_cfg = {"a": _agg([10, 10.2, 9.9]), "b": _agg([14, 14.2, 13.9])}
        self.assertFalse(ab.noise_flag(per_cfg))

    def test_high_variance(self):
        per_cfg = {"a": _agg([1, 20, 5, 40]), "b": _agg([30, 31, 29])}
        self.assertTrue(ab.noise_flag(per_cfg))

    def test_overlapping_ranges(self):
        per_cfg = {"a": _agg([10, 15, 20]), "b": _agg([12, 14, 16])}
        self.assertTrue(ab.noise_flag(per_cfg))

    def test_metric_table(self):
        per_metric = {"m": {"a": _agg([1, 2, 3, 30]), "b": _agg([50, 51, 52])}}
        table = ab.metric_table(per_metric)
        self.assertTrue(table["m"]["noisy"])
        self.assertIn("a", table["m"]["cfgs"])


if __name__ == "__main__":
    unittest.main()
