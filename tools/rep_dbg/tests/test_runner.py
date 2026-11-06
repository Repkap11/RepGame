import json
import os
import tempfile
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import runner, results


def _make_run_dir(records, exit_code=0):
    run_dir = tempfile.mkdtemp()
    os.makedirs(os.path.join(run_dir, "screenshots"), exist_ok=True)
    with open(os.path.join(run_dir, "stdout.log"), "w") as f:
        f.write("FLYMET t=20 ph=1 rd=6 p=(0.5,80.0,0.5) cl=0 j=0 p=0 c=0 d=0 x=0 bfs=0 pri=0 stv=0 s=0 stl=0 occ=0/10 | loop=1\n")
    with open(os.path.join(run_dir, "results.jsonl"), "w") as f:
        for r in records:
            f.write(json.dumps(r) + "\n")
    result = {"id": "t/1", "name": "t", "kind": "run",
              "exit": {"code": exit_code, "signal": None},
              "git": {"sha": "abc", "dirty": False, "rev_arg": None},
              "build": "release"}
    runner.assemble_result(result, run_dir)
    return run_dir, result


HEADER = {"type": "header", "schema": 1, "git": "abc", "build": "release"}
SUMMARY = {"type": "summary", "tick": 100, "ticks": 100, "exit_reason": "fly_ticks",
           "asserts": {"total": 0, "failed": 0}, "shots": [], "settle_tick": 20}


class TestAssemble(unittest.TestCase):
    def test_ok(self):
        metrics = [{"type": "metric", "tick": t, "loader": {"light_jobs": t}}
                   for t in (20, 40, 60)]
        run_dir, result = _make_run_dir([HEADER] + metrics + [SUMMARY])
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["game"]["exit_reason"], "fly_ticks")
        self.assertEqual(result["stats"]["metric.loader.light_jobs"]["max"], 60)
        self.assertIn("metric.loader.light_jobs_after_settle", result["stats"])
        self.assertEqual(result["artifacts"]["log"], "stdout.log")

    def test_crashed(self):
        run_dir, result = _make_run_dir([HEADER], exit_code=-11)
        self.assertIsNone(result["game"])
        self.assertEqual(result["status"], "crashed")

    def test_timeout(self):
        run_dir, result = _make_run_dir([HEADER], exit_code=-15)
        result["exit"]["code"] = -15
        runner.assemble_result(result, run_dir, timed_out=True)
        self.assertEqual(result["status"], "timeout")

    def test_asserts_failed(self):
        records = [HEADER, {"type": "assert", "tick": 5, "line": 3, "cmd": "assert_light 1 2 3 sky >= 12",
                            "ok": False, "expected": ">=12", "actual": "9"}, SUMMARY]
        run_dir, result = _make_run_dir(records)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["asserts"]["failed"], 1)
        self.assertEqual(result["asserts"]["failures"][0]["line"], 3)

    def test_log_shim(self):
        # No header record -> the shim rebuilds results.jsonl from the log.
        run_dir, result = _make_run_dir([])
        self.assertEqual(result["source"], "log-shim")
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["game"]["exit_reason"], "unknown")
        self.assertEqual(result["stats"]["metric.loader.chunks_loading"]["last"], 0)


class TestExitCodes(unittest.TestCase):
    def test_codes(self):
        self.assertEqual(runner.exit_code({"status": "ok"}), 0)
        self.assertEqual(runner.exit_code({"status": "failed"}), 1)
        self.assertEqual(runner.exit_code({"status": "crashed"}), 2)
        self.assertEqual(runner.exit_code({"status": "timeout"}), 2)
        self.assertEqual(runner.exit_code({"status": "build_failed"}), 3)


class TestLogflags(unittest.TestCase):
    def test_flags(self):
        from rep_dbg import logflags
        flags = logflags.scan_lines([
            "ERROR: AddressSanitizer: heap-use-after-free",
            "GL Error:0x501",
            "Capture view 0: shot pending after 600 ticks (TIMEOUT — loader still busy)",
            "normal line",
        ])
        self.assertEqual(flags["asan_errors"], 1)
        self.assertEqual(flags["gl_errors"], 1)
        self.assertEqual(flags["capture_timeouts"], 1)
        self.assertEqual(flags["ubsan_errors"], 0)


class TestResultsApi(unittest.TestCase):
    def test_run_api(self):
        records = [HEADER,
                   {"type": "metric", "tick": 10, "loader": {"light_jobs": 1}},
                   {"type": "metric", "tick": 30, "loader": {"light_jobs": 2}},
                   {"type": "event", "tick": 20, "name": "fly_settled"},
                   {"type": "probe", "tick": 25, "pos": [1, 2, 3], "block": 5,
                    "name": "stone", "rotation": 0, "sky": 15, "blocklight": 0},
                   SUMMARY]
        run_dir, result = _make_run_dir(records)
        with open(os.path.join(run_dir, "result.json"), "w") as f:
            json.dump(result, f)
        r = results.Run(run_dir)
        self.assertEqual(len(r.metrics()), 2)
        self.assertEqual(len(r.metrics(after_settle=True)), 1)
        self.assertEqual(r.events("fly_settled")[0]["tick"], 20)
        self.assertEqual(len(r.probes()), 1)
        self.assertEqual(r.field("stats.metric.loader.light_jobs.max"), 2)
        self.assertTrue(r.artifact("log").endswith("stdout.log"))


if __name__ == "__main__":
    unittest.main()
