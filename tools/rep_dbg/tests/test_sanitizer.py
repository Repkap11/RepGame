import io
import os
import tempfile
import unittest

from rep_dbg import sanitizer, crash, logparse


ASAN_REPORT = """=================================================================
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000010
READ of size 4 at 0x602000000010 thread T0
    #0 0x55a1c2 in World::getBlock(int, int, int) src/common/world.cpp:123
    #1 0x55a1d4 in RepGame::game_tick() src/common/game/game_tick.cpp:456
    #2 0x7f88aa in main src/linux/RepGameLinux.cpp:789
SUMMARY: AddressSanitizer: heap-buffer-overflow src/common/world.cpp:123 in World::getBlock
"""

UBSAN_REPORT = """src/common/world.cpp:88:9: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
    #0 0x55a1c2 in World::grow(int) src/common/world.cpp:88
    #1 0x7f88aa in main src/linux/RepGameLinux.cpp:789
"""

TSAN_REPORT = """==================
WARNING: ThreadSanitizer: data race (pid=99)
  Write of size 4 at 0x7b0400000000 by thread T5:
    #0 ChunkLoader::light_worker() src/common/chunk_loader.cpp:200
    #1 void std::thread::_State_impl src/linux/x.cpp:1
  Previous read of size 4 by main thread:
    #0 RepGame::game_tick() src/common/game/game_tick.cpp:100
SUMMARY: ThreadSanitizer: data race src/common/chunk_loader.cpp:200 in ChunkLoader::light_worker()
==================
"""

LSAN_REPORT = """=================================================================
==12345==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 128 byte(s) in 1 object(s) allocated from:
    #0 0x7f88cc in malloc (/lib/x86_64-linux-gnu/libc.so.6+0x98)
    #1 0x55a1ee in World::load() src/common/world.cpp:55
SUMMARY: LeakSanitizer: 128 byte(s) leaked in 1 allocation(s).
"""

GDB_CRASH = """Thread 1 "RepGame" received signal SIGSEGV, Segmentation fault.
0x00005555556ab123 in RepGame::game_tick() at src/common/game/game_tick.cpp:351
351\t    *reinterpret_cast<volatile int *>(nullptr) = 0;
#0  0x00005555556ab123 in RepGame::game_tick() at src/common/game/game_tick.cpp:351
#1  0x00005555556ab999 in RepGame::render_frame() at src/common/game/game_render.cpp:88
#2  0x0000555555700111 in main () at src/linux/RepGameLinux.cpp:200

Thread 5 (Thread 0x7ffff5200640 (LWP 101)):
#0  0x00007ffff7a9f123 in __futex_abstimed_wait_common () from /lib/libc.so.6
#1  0x00007ffff7aa1456 in pthread_cond_wait () from /lib/libc.so.6
"""

GDB_CLEAN = """[Detaching after fork from child process 55]
[Inferior 1 (process 100) exited normally]
"""


class SanitizerEnvTest(unittest.TestCase):
    def test_asan_env(self):
        env = sanitizer.sanitizer_env("asan", "/run/x")
        self.assertIn("log_path=/run/x/asan", env["ASAN_OPTIONS"])
        self.assertIn("detect_leaks=1", env["ASAN_OPTIONS"])
        self.assertTrue(env["LSAN_OPTIONS"].endswith("lsan.supp"))
        self.assertIn("log_path=/run/x/ubsan", env["UBSAN_OPTIONS"])

    def test_tsan_env(self):
        env = sanitizer.sanitizer_env("tsan", "/run/x")
        self.assertIn("log_path=/run/x/tsan", env["TSAN_OPTIONS"])
        self.assertIn("tsan.supp", env["TSAN_OPTIONS"])
        self.assertIn("second_deadlock_stack=1", env["TSAN_OPTIONS"])

    def test_other_builds(self):
        self.assertEqual(sanitizer.sanitizer_env("release", "/x"), {})
        self.assertEqual(sanitizer.sanitizer_env("debug", "/x"), {})


class ParseReportTest(unittest.TestCase):
    def _scan(self, files):
        with tempfile.TemporaryDirectory() as d:
            for name, text in files.items():
                with open(os.path.join(d, name), "w") as f:
                    f.write(text)
            return sanitizer.scan_run_dir(d)

    def test_asan(self):
        counts, top, files = self._scan({"asan.1": ASAN_REPORT})
        self.assertEqual(counts["asan_errors"], 1)
        self.assertEqual(counts["leaks"], 0)
        self.assertEqual(files, ["asan.1"])
        self.assertIn("heap-buffer-overflow", top[0]["summary_line"])
        self.assertIn("world.cpp:123", top[0]["first_repgame_frame"])

    def test_ubsan(self):
        counts, top, _ = self._scan({"ubsan.1": UBSAN_REPORT})
        self.assertEqual(counts["ubsan_errors"], 1)
        self.assertIn("runtime error", top[0]["summary_line"])
        self.assertIn("world.cpp:88", top[0]["first_repgame_frame"])

    def test_tsan(self):
        counts, top, _ = self._scan({"tsan.1": TSAN_REPORT})
        self.assertEqual(counts["tsan_warnings"], 1)
        self.assertIn("chunk_loader.cpp:200", top[0]["first_repgame_frame"])

    def test_lsan(self):
        counts, top, _ = self._scan({"asan.2": LSAN_REPORT})
        self.assertEqual(counts["leaks"], 1)
        self.assertIn("world.cpp:55", top[0]["first_repgame_frame"])

    def test_postprocess_fails(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "asan.1"), "w") as f:
                f.write(ASAN_REPORT)
            result = {"status": "ok"}
            sanitizer.postprocess(d, result, io.StringIO())
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["sanitizer"]["asan_errors"], 1)
            self.assertEqual(result["artifacts"]["sanitizer"], ["asan.1"])


class CrashParseTest(unittest.TestCase):
    def test_crash(self):
        crash_ = crash.parse_gdb_log(GDB_CRASH)
        self.assertEqual(crash_["signal"], "SIGSEGV")
        self.assertEqual(crash_["thread"], 1)
        frames = crash_["top_frames"]
        self.assertTrue(any("RepGame::game_tick" in f and "game_tick.cpp:351" in f
                            for f in frames))

    def test_no_crash(self):
        self.assertTrue(crash.parse_gdb_log(GDB_CLEAN)["no_crash"])

    def test_postprocess_marks_crashed(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "crash.txt"), "w") as f:
                f.write(GDB_CRASH)
            result = {"status": "ok"}
            crash.postprocess(d, result, io.StringIO())
            self.assertEqual(result["status"], "crashed")
            self.assertEqual(result["crash"]["signal"], "SIGSEGV")

    def test_postprocess_no_crash(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "crash.txt"), "w") as f:
                f.write(GDB_CLEAN)
            result = {"status": "ok"}
            crash.postprocess(d, result, io.StringIO())
            self.assertEqual(result["status"], "ok")
            self.assertEqual(result["crash"]["signal"], None)
            self.assertEqual(result["crash"]["note"], "no_crash")


class RepTestsParseTest(unittest.TestCase):
    def test_all_pass(self):
        text = 'Test "a" passed\nTest "b" passed\nAll 30 Rep Tests passed\n'
        self.assertEqual(logparse.parse_rep_tests(text),
                         {"passed": 30, "failed": 0, "failed_names": []})

    def test_failures(self):
        text = ('Test "a" failed with 1\nTest "b" passed\n'
                '2 of 5 test(s) failed:\n  a\n  c\n')
        out = logparse.parse_rep_tests(text)
        self.assertEqual((out["passed"], out["failed"]), (3, 2))
        self.assertEqual(out["failed_names"], ["a", "c"])

    def test_empty(self):
        out = logparse.parse_rep_tests("garbage\n")
        self.assertEqual(out["failed"], 0)


if __name__ == "__main__":
    unittest.main()
