import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import perf


REPORT = """\
# ========
#
   41.23%  RepGame     RepGame::game_tick()
   20.01%  loader-1    ChunkLoader::light_worker()
   10.50%  RepGame     ChunkLoader::getChunk()
    5.00%  render      draw_world()
"""

STAT_CSV = """\
# started on ...
123456789,,cycles,100.00,,
234567890,,instructions,100.00,,
1000000,,cache-references,100.00,,
250000,,cache-misses,100.00,,
<not supported>,,branches,,,
<not counted>,,context-switches,50.00,,
300.12,,task-clock,100.00,,
"""


class TestPreflight(unittest.TestCase):
    def _fake_sys(self, paranoid, kptr):
        td = tempfile.mkdtemp()
        os.makedirs(os.path.join(td, "kernel"))
        with open(os.path.join(td, "kernel/perf_event_paranoid"), "w") as f:
            f.write(str(paranoid))
        with open(os.path.join(td, "kernel/kptr_restrict"), "w") as f:
            f.write(str(kptr))
        return td

    def setUp(self):
        self._old = os.environ.get("REP_DBG_PROC_SYS")

    def tearDown(self):
        if self._old is None:
            os.environ.pop("REP_DBG_PROC_SYS", None)
        else:
            os.environ["REP_DBG_PROC_SYS"] = self._old

    def test_paranoia_high_refuses(self):
        import io
        os.environ["REP_DBG_PROC_SYS"] = self._fake_sys(4, 1)
        buf = io.StringIO()
        self.assertFalse(perf.preflight(buf))
        self.assertIn("perf_event_paranoid", buf.getvalue())
        self.assertIn("sysctl", buf.getvalue())

    def test_paranoia_ok_with_kptr_warn(self):
        import io
        os.environ["REP_DBG_PROC_SYS"] = self._fake_sys(1, 1)
        buf = io.StringIO()
        self.assertTrue(perf.preflight(buf))
        self.assertIn("kptr_restrict", buf.getvalue())

    def test_all_clear(self):
        import io
        os.environ["REP_DBG_PROC_SYS"] = self._fake_sys(0, 0)
        buf = io.StringIO()
        self.assertTrue(perf.preflight(buf))
        self.assertEqual(buf.getvalue(), "")


class TestStatCsv(unittest.TestCase):
    def test_parse(self):
        stat = perf.parse_stat_csv(STAT_CSV)
        self.assertEqual(stat["cycles"], 123456789.0)
        self.assertEqual(stat["instructions"], 234567890.0)
        self.assertAlmostEqual(stat["ipc"], 234567890.0 / 123456789.0)
        self.assertAlmostEqual(stat["cache_miss_rate"], 0.25)
        self.assertIsNone(stat["branches"])
        self.assertIsNone(stat["context-switches"])
        self.assertAlmostEqual(stat["task_clock_ms"], 300.12)


class TestReport(unittest.TestCase):
    def test_parse(self):
        rows = perf.parse_report(REPORT)
        self.assertEqual(len(rows), 4)
        self.assertEqual(rows[0]["symbol"], "RepGame::game_tick()")
        self.assertEqual(rows[0]["thread"], "RepGame")
        self.assertAlmostEqual(rows[1]["self_pct"], 20.01)


class TestDiff(unittest.TestCase):
    def test_deltas(self):
        a = [{"symbol": "f", "self_pct": 10.0, "thread": "t"},
             {"symbol": "g", "self_pct": 5.0, "thread": "t"}]
        b = [{"symbol": "f", "self_pct": 4.0, "thread": "t"},
             {"symbol": "h", "self_pct": 8.0, "thread": "t"}]
        rows = perf.top_diff(a, b, 10)
        self.assertEqual(rows[0]["symbol"], "h")
        self.assertAlmostEqual(rows[0]["delta"], 8.0)
        deltas = {r["symbol"]: r["delta"] for r in rows}
        self.assertAlmostEqual(deltas["f"], -6.0)
        self.assertAlmostEqual(deltas["g"], -5.0)


if __name__ == "__main__":
    unittest.main()
