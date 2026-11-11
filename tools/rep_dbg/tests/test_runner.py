import json
import os
import shutil
import tempfile
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import runner, results


def _make_run_dir(tc, records, exit_code=0):
    run_dir = tempfile.mkdtemp()
    tc.addCleanup(shutil.rmtree, run_dir, ignore_errors=True)
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
        run_dir, result = _make_run_dir(self, [HEADER] + metrics + [SUMMARY])
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["game"]["exit_reason"], "fly_ticks")
        self.assertEqual(result["stats"]["metric.loader.light_jobs"]["max"], 60)
        self.assertIn("metric.loader.light_jobs_after_settle", result["stats"])
        self.assertEqual(result["artifacts"]["log"], "stdout.log")

    def test_crashed(self):
        run_dir, result = _make_run_dir(self, [HEADER], exit_code=-11)
        self.assertIsNone(result["game"])
        self.assertEqual(result["status"], "crashed")

    def test_timeout(self):
        run_dir, result = _make_run_dir(self, [HEADER], exit_code=-15)
        result["exit"]["code"] = -15
        runner.assemble_result(result, run_dir, timed_out=True)
        self.assertEqual(result["status"], "timeout")

    def test_asserts_failed(self):
        records = [HEADER, {"type": "assert", "tick": 5, "line": 3, "cmd": "assert_light 1 2 3 sky >= 12",
                            "ok": False, "expected": ">=12", "actual": "9"}, SUMMARY]
        run_dir, result = _make_run_dir(self, records)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["asserts"]["failed"], 1)
        self.assertEqual(result["asserts"]["failures"][0]["line"], 3)

    def test_log_shim(self):
        # No header record -> the shim rebuilds results.jsonl from the log.
        run_dir, result = _make_run_dir(self, [])
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


FAKE_GAME = """#!/bin/sh
R="$REPGAME_RESULTS"
echo '{"type":"header","schema":1}' >> "$R"
metric() { echo "{\\"type\\":\\"metric\\",\\"tick\\":$1,\\"prof\\":{\\"draw_frame\\":{\\"n\\":10,\\"us\\":1000,\\"max_us\\":200}}}" >> "$R"; }
trap 'echo "{\\"type\\":\\"summary\\",\\"tick\\":999,\\"ticks\\":999,\\"exit_reason\\":\\"signal\\",\\"asserts\\":{\\"total\\":0,\\"failed\\":0},\\"shots\\":[],\\"settle_tick\\":0}" >> "$R"; exit 0' TERM INT
metric 20
echo '{"type":"event","tick":20,"name":"fly_settled"}' >> "$R"
t=40
while true; do metric $t; t=$((t+20)); sleep 0.05; done
"""

FAKE_GAME_EARLY_EXIT = """#!/bin/sh
echo '{"type":"header","schema":1}' >> "$REPGAME_RESULTS"
exit 0
"""


class TestWindow(unittest.TestCase):
    def _opts(self, root, script_path, timed_window):
        import argparse
        return argparse.Namespace(
            root=root, env=[], rev=None, rev_arg=None, build="release",
            world="W", flat=True, keep_world=None, timeout=30,
            name="win", kind="run", window=None, timed_window=timed_window,
            extra=[], tree=None, ab_id=None, cfg=None)

    def _fake_binary(self, text):
        import stat
        td = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, td, ignore_errors=True)
        path = os.path.join(td, "fakegame")
        with open(path, "w") as f:
            f.write(text)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        return path

    def _run(self, script, window):
        from rep_dbg import build as build_mod
        old = build_mod.ensure_built
        build_mod.ensure_built = lambda tree, build, log: self._fake_binary(script)
        try:
            root = tempfile.mkdtemp()
            self.addCleanup(shutil.rmtree, root, ignore_errors=True)
            import io
            result, run_dir = runner.run(self._opts(root, None, window), io.StringIO())
            return result, run_dir
        finally:
            build_mod.ensure_built = old

    def test_window_ok(self):
        calls = {}
        window = {"after_event": "fly_settled", "warmup_s": 0.05, "window_s": 0.3,
                  "on_start": lambda pid, rd: calls.setdefault("pid", pid),
                  "on_end": lambda h: calls.setdefault("end", True)}
        result, run_dir = self._run(FAKE_GAME, window)
        self.assertEqual(result["status"], "ok")
        w = result["window"]
        self.assertIsNone(w.get("error"))
        self.assertIsInstance(calls.get("pid"), int)
        self.assertTrue(calls.get("end"))
        self.assertGreater(w["frames"], 0)
        self.assertGreater(w["fps"], 0)
        self.assertAlmostEqual(w["prof"]["draw_frame"]["us_per_call"], 100.0)
        self.assertGreater(w["seconds"], 0.1)
        self.assertLess(w["start_tick"], w["end_tick"])

    def test_window_early_exit(self):
        window = {"after_event": "fly_settled", "warmup_s": 0.05, "window_s": 0.3}
        result, run_dir = self._run(FAKE_GAME_EARLY_EXIT, window)
        self.assertEqual(result["status"], "failed")
        self.assertIn("error", result["window"])
        self.assertIsNone(result["window"]["frames"])


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
        run_dir, result = _make_run_dir(self, records)
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
