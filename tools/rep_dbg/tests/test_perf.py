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

    def test_normalised_per_frame(self):
        a = [{"symbol": "hot", "self_pct": 20.0, "thread": "t"}]
        b = [{"symbol": "hot", "self_pct": 40.0, "thread": "t"}]
        meta_a = {"samples": 1000, "freq": 100, "frames": 100}
        meta_b = {"samples": 1000, "freq": 100, "frames": 400}
        rows = perf.top_diff(a, b, 5, meta_a=meta_a, meta_b=meta_b)
        r = rows[0]
        self.assertEqual(r["norm_kind"], "us_per_frame")
        # a: 20% * 10s / 100 frames = 20000us; b: 40% * 10s / 400 = 10000us
        self.assertAlmostEqual(r["a_norm"], 20000.0)
        self.assertAlmostEqual(r["b_norm"], 10000.0)
        self.assertAlmostEqual(r["norm_change_pct"], -50.0)
        self.assertTrue(r["share_only"])  # pct doubled, cost halved

    def test_normalised_ms_per_s(self):
        a = [{"symbol": "f", "self_pct": 10.0, "thread": "t"}]
        b = [{"symbol": "f", "self_pct": 30.0, "thread": "t"}]
        meta = {"samples": 500, "freq": 100, "duration_s": 10}
        rows = perf.top_diff(a, b, 5, meta_a=meta, meta_b=meta)
        self.assertEqual(rows[0]["norm_kind"], "ms_per_s")
        # 500 samples / 100 Hz = 5 CPU-s; 10%/30% of 5s over 10 wall-s.
        self.assertAlmostEqual(rows[0]["a_norm"], 50.0)
        self.assertAlmostEqual(rows[0]["b_norm"], 150.0)


SCRIPT = """\
RepGame 123 [001] 1.0: cycles:
        main (/bin/x)
        tick (/bin/x)

RepGame 123 [001] 2.0: cycles:
        main (/bin/x)
        tick (/bin/x)
"""


class _Opts:
    freq = 499
    duration = 10
    threads = False
    keep_data = False


class _FakeRun:
    def __init__(self, stdout=""):
        self.stdout = stdout
        self.returncode = 0


def _fake_perf_run(argv, **kw):
    if "report" in argv:
        return _FakeRun(REPORT)
    if "script" in argv:
        return _FakeRun(SCRIPT)
    return _FakeRun("")


class TestPostprocess(unittest.TestCase):
    def setUp(self):
        self.run_dir = tempfile.mkdtemp()
        with open(os.path.join(self.run_dir, "perf.data"), "wb") as f:
            f.write(b"x" * 1024)
        self._old = perf.subprocess.run
        perf.subprocess.run = _fake_perf_run

    def tearDown(self):
        perf.subprocess.run = self._old

    def test_delete_default(self):
        result = {"id": "x", "wall_s": 42.0}
        opts = _Opts()
        perf.postprocess(self.run_dir, result, opts)
        self.assertFalse(os.path.exists(os.path.join(self.run_dir, "perf.data")))
        self.assertTrue(result["perf"]["data_deleted"])
        self.assertEqual(result["perf"]["data_size_bytes"], 1024)
        self.assertEqual(result["perf"]["samples"], 2)
        self.assertEqual(result["perf"]["freq"], 499)
        self.assertEqual(result["perf"]["duration_s"], 42.0)
        self.assertNotIn("data", result["artifacts"]["perf"])
        self.assertTrue(result["perf"]["top"])

    def test_duration_order(self):
        # opts.duration is the record timeout — never the measurement.
        # Order: window.seconds > preset perf.duration_s (attach) > wall_s.
        for result, want in (
            ({"id": "x", "wall_s": 99.0, "window": {"seconds": 8.0},
              "perf": {"duration_s": 50.0}}, 8.0),
            ({"id": "x", "wall_s": 99.0, "perf": {"duration_s": 15.0}}, 15.0),
            ({"id": "x", "wall_s": 99.0}, 99.0),
        ):
            # postprocess deletes perf.data by default — recreate per case.
            with open(os.path.join(self.run_dir, "perf.data"), "wb") as f:
                f.write(b"x" * 1024)
            opts = _Opts()  # opts.duration=10 must be ignored everywhere
            perf.postprocess(self.run_dir, result, opts)
            self.assertEqual(result["perf"]["duration_s"], want, result)

    def test_keep_data(self):
        result = {"id": "x"}
        opts = _Opts()
        opts.keep_data = True
        perf.postprocess(self.run_dir, result, opts)
        self.assertTrue(os.path.exists(os.path.join(self.run_dir, "perf.data")))
        self.assertNotIn("data_deleted", result["perf"])
        self.assertEqual(result["artifacts"]["perf"]["data"], "perf.data")


if __name__ == "__main__":
    unittest.main()
