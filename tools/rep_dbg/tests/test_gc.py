import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import gc as gc_mod
from rep_dbg import worktree as worktree_mod
from rep_dbg import index as index_mod
from rep_dbg import build as build_mod


def _run_dir(root, name, days_old=0, result=True, size_bytes=0):
    ts = time.strftime("%Y%m%d-%H%M%S", time.localtime(time.time() - days_old * 86400))
    d = os.path.join(root, name, ts + "-1")
    os.makedirs(d, exist_ok=True)
    if result:
        with open(os.path.join(d, "result.json"), "w") as f:
            json.dump({"id": "%s/%s-1" % (name, ts), "status": "ok"}, f)
    if size_bytes:
        with open(os.path.join(d, "blob"), "wb") as f:
            f.write(b"x" * size_bytes)
    old = time.time() - days_old * 86400
    os.utime(d, (old, old))
    return d


class GcTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.log = io.StringIO()

    def test_old_run_dir_removed(self):
        old = _run_dir(self.root, "a", days_old=30)
        new = _run_dir(self.root, "b", days_old=0)
        gc_mod.gc(self.root, keep_days=7, max_gb=100, perf_data_hours=24,
                  wt_days=3, log=self.log)
        self.assertFalse(os.path.exists(old))
        self.assertTrue(os.path.exists(new))

    def test_dry_run(self):
        old = _run_dir(self.root, "a", days_old=30)
        gc_mod.gc(self.root, dry_run=True, keep_days=7, max_gb=100,
                  perf_data_hours=24, wt_days=3, log=self.log)
        self.assertTrue(os.path.exists(old))
        self.assertIn("gc:", self.log.getvalue())

    def test_perf_data_sweep(self):
        d = _run_dir(self.root, "p", days_old=0)
        data = os.path.join(d, "perf.data")
        with open(data, "wb") as f:
            f.write(b"p" * 4096)
        with open(os.path.join(d, "result.json"), "w") as f:
            json.dump({"id": "p", "status": "ok",
                       "artifacts": {"perf": {"data": "perf.data"}}}, f)
        old = time.time() - 30 * 3600
        os.utime(data, (old, old))
        gc_mod.gc(self.root, keep_days=7, max_gb=100, perf_data_hours=24,
                  wt_days=3, log=self.log)
        self.assertFalse(os.path.exists(data))
        result = json.load(open(os.path.join(d, "result.json")))
        self.assertTrue(result["perf"]["data_deleted"])
        self.assertNotIn("data", result["artifacts"]["perf"])

    def test_size_cap(self):
        _run_dir(self.root, "small", days_old=0.1)
        big = _run_dir(self.root, "big", days_old=0.1, size_bytes=2000)
        gc_mod.gc(self.root, keep_days=7, max_gb=0.000001, perf_data_hours=24,
                  wt_days=3, log=self.log)
        self.assertFalse(os.path.exists(big))

    def test_cap_never_deletes_under_1h(self):
        young = _run_dir(self.root, "y", days_old=0)
        gc_mod.gc(self.root, keep_days=7, max_gb=0.000001, perf_data_hours=24,
                  wt_days=3, log=self.log)
        self.assertTrue(os.path.exists(young))

    def test_symlink_not_followed(self):
        outside = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, outside, ignore_errors=True)
        with open(os.path.join(outside, "keep.me"), "w") as f:
            f.write("precious")
        d = _run_dir(self.root, "a", days_old=30)
        os.symlink(outside, os.path.join(d, "link_out"))
        old = time.time() - 30 * 86400
        os.utime(d, (old, old))  # creating the symlink refreshed d's mtime
        gc_mod.gc(self.root, keep_days=7, max_gb=100, perf_data_hours=24,
                  wt_days=3, log=self.log)
        self.assertFalse(os.path.exists(d))
        self.assertTrue(os.path.exists(os.path.join(outside, "keep.me")))

    def test_index_rebuilt(self):
        _run_dir(self.root, "a", days_old=30)
        keep = _run_dir(self.root, "b", days_old=0)
        index_mod.reindex(self.root)
        gc_mod.gc(self.root, keep_days=7, max_gb=100, perf_data_hours=24,
                  wt_days=3, log=self.log)
        entries = index_mod.load(self.root)
        dirs = [e["dir"] for e in entries]
        self.assertEqual(len(dirs), 1)
        self.assertIn(os.path.basename(keep), dirs[0])

    def test_refuse_dangerous_root(self):
        with self.assertRaises(ValueError):
            gc_mod.gc("/", log=self.log)
        with self.assertRaises(ValueError):
            gc_mod.gc(os.path.expanduser("~"), log=self.log)
        with self.assertRaises(ValueError):
            gc_mod.gc(build_mod.REPO, log=self.log)


def _mptest_dir(scratch, name, days_old=0, size_bytes=0):
    d = os.path.join(scratch, "mptest_" + name)
    os.makedirs(d)
    payload = os.path.join(d, "server.log")
    with open(payload, "wb") as f:
        f.write(b"x" * size_bytes)
    old = time.time() - days_old * 86400
    os.utime(payload, (old, old))
    os.utime(d, (old, old))
    return d


class MpTestGcTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.scratch = os.path.join(self.root, "repgame")
        os.makedirs(self.scratch)
        self.log = io.StringIO()

    def test_old_removed_new_kept(self):
        old = _mptest_dir(self.scratch, "old", days_old=3)
        new = _mptest_dir(self.scratch, "new", days_old=0)
        gc_mod.gc_mptest(self.scratch, keep_days=2, max_gb=100, log=self.log)
        self.assertFalse(os.path.exists(old))
        self.assertTrue(os.path.exists(new))

    def test_dry_run(self):
        old = _mptest_dir(self.scratch, "old", days_old=3)
        out = gc_mod.gc_mptest(self.scratch, dry_run=True, keep_days=2,
                               max_gb=100, log=self.log)
        self.assertTrue(os.path.exists(old))
        self.assertIn(old, out["removed"])

    def test_size_cap(self):
        small = _mptest_dir(self.scratch, "small", days_old=0.1, size_bytes=1)
        big = _mptest_dir(self.scratch, "big", days_old=0.2, size_bytes=2000)
        gc_mod.gc_mptest(self.scratch, keep_days=100, max_gb=0.000001,
                         log=self.log)
        self.assertFalse(os.path.exists(big))
        self.assertTrue(os.path.exists(small))

    def test_cap_preserves_newest_completed(self):
        young = _mptest_dir(self.scratch, "young", days_old=0, size_bytes=2000)
        gc_mod.gc_mptest(self.scratch, keep_days=100, max_gb=0.000001,
                         log=self.log)
        self.assertTrue(os.path.exists(young))

    def test_cap_deletes_older_young_completed(self):
        older = _mptest_dir(self.scratch, "older", days_old=0, size_bytes=2000)
        newest = _mptest_dir(self.scratch, "newest", days_old=0, size_bytes=2000)
        gc_mod.gc_mptest(self.scratch, keep_days=100, max_gb=0.000001,
                         log=self.log)
        self.assertFalse(os.path.exists(older))
        self.assertTrue(os.path.exists(newest))

    def test_active_artifact_untouched(self):
        active = _mptest_dir(self.scratch, "active", days_old=3, size_bytes=2000)
        marker = os.path.join(active, gc_mod.MPTEST_ACTIVE)
        with open(marker, "w") as f:
            f.write(str(os.getpid()))
        old = time.time() - 3 * 86400
        os.utime(marker, (old, old))
        gc_mod.gc_mptest(self.scratch, keep_days=2, max_gb=0, log=self.log)
        self.assertTrue(os.path.exists(active))

    def test_active_size_does_not_count_toward_completed_cap(self):
        active = _mptest_dir(self.scratch, "active", days_old=0, size_bytes=2000)
        with open(os.path.join(active, gc_mod.MPTEST_ACTIVE), "w") as f:
            f.write(str(os.getpid()))
        older = _mptest_dir(self.scratch, "older", days_old=0.01, size_bytes=500)
        newer = _mptest_dir(self.scratch, "newer", days_old=0, size_bytes=500)
        gc_mod.gc_mptest(self.scratch, keep_days=100, max_gb=0.0000015,
                         log=self.log)
        self.assertTrue(os.path.exists(older))
        self.assertTrue(os.path.exists(newer))

    def test_nested_write_refreshes_artifact_age(self):
        d = _mptest_dir(self.scratch, "active", days_old=3)
        nested = os.path.join(d, "server_home", ".repgame")
        os.makedirs(nested)
        with open(os.path.join(nested, "world.db"), "w") as f:
            f.write("current")
        gc_mod.gc_mptest(self.scratch, keep_days=2, max_gb=100, log=self.log)
        self.assertTrue(os.path.exists(d))

    def test_non_mptest_and_symlink_untouched(self):
        plain = os.path.join(self.scratch, "plain")
        os.makedirs(plain)
        outside = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, outside, ignore_errors=True)
        with open(os.path.join(outside, "keep.me"), "w") as f:
            f.write("precious")
        os.symlink(outside, os.path.join(self.scratch, "mptest_link"))
        gc_mod.gc_mptest(self.scratch, keep_days=0, max_gb=0, log=self.log)
        self.assertTrue(os.path.exists(plain))
        self.assertTrue(os.path.exists(os.path.join(outside, "keep.me")))

    def test_rep_dbg_gc_integrates_mptest_scratch(self):
        old = _mptest_dir(self.scratch, "old", days_old=3)
        gc_mod.gc(self.root, keep_days=7, max_gb=100, perf_data_hours=24,
                  wt_days=3, mptest_keep_days=2, mptest_max_gb=100,
                  mptest_scratch=self.scratch, log=self.log)
        self.assertFalse(os.path.exists(old))


class _Repo:
    """A temp git repo used as build_mod.REPO for worktree tests."""

    def __init__(self):
        self.path = tempfile.mkdtemp()
        for args in (["init", "-q"], ["config", "user.email", "t@t"],
                     ["config", "user.name", "t"]):
            subprocess.check_call(["git", "-C", self.path] + args)
        with open(os.path.join(self.path, "f"), "w") as f:
            f.write("x")
        subprocess.check_call(["git", "-C", self.path, "add", "f"])
        subprocess.check_call(["git", "-C", self.path, "commit", "-qm", "init"])
        self.sha = subprocess.check_output(
            ["git", "-C", self.path, "rev-parse", "HEAD"], text=True).strip()


class WorktreeTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.log = io.StringIO()
        self.repo = _Repo()
        self.addCleanup(shutil.rmtree, self.repo.path, ignore_errors=True)
        self._old = build_mod.REPO
        build_mod.REPO = self.repo.path

    def tearDown(self):
        build_mod.REPO = self._old

    def test_ensure_stamps_used(self):
        wt = worktree_mod.ensure(self.root, self.repo.sha, self.log)
        self.assertTrue(os.path.isdir(wt))
        stamp = os.path.join(self.root, "wt", ".used", self.repo.sha)
        self.assertTrue(os.path.exists(stamp))

    def test_recent_kept_old_removed(self):
        wt = worktree_mod.ensure(self.root, self.repo.sha, self.log)
        freed = worktree_mod.prune(self.root, self.log, older_than_days=3)
        self.assertTrue(os.path.isdir(wt))
        self.assertEqual(freed, 0)
        old = time.time() - 10 * 86400
        os.utime(os.path.join(self.root, "wt", ".used", self.repo.sha), (old, old))
        worktree_mod.prune(self.root, self.log, older_than_days=3)
        self.assertFalse(os.path.exists(wt))

    def test_orphan_dir_removed(self):
        orphan = os.path.join(self.root, "wt", "deadbeef")
        os.makedirs(orphan)
        worktree_mod.prune(self.root, self.log, all=True)
        self.assertFalse(os.path.exists(orphan))

    def test_stale_registration_pruned(self):
        wt = worktree_mod.ensure(self.root, self.repo.sha, self.log)
        shutil.rmtree(wt)  # dir vanished (as after a /tmp wipe)
        worktree_mod.prune(self.root, self.log, all=True)
        out = subprocess.check_output(
            ["git", "-C", self.repo.path, "worktree", "list"], text=True)
        self.assertNotIn(wt, out)

    def test_managed_lists_registered_outside_wt(self):
        hand = os.path.join(self.root, "wt-hand")
        subprocess.check_call(["git", "-C", self.repo.path, "worktree",
                               "add", "--detach", hand, self.repo.sha],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        entries = worktree_mod.managed(self.root)
        self.assertIn(hand, entries)
        self.assertTrue(entries[hand]["registered"])

    def test_dry_run_keeps(self):
        wt = worktree_mod.ensure(self.root, self.repo.sha, self.log)
        worktree_mod.prune(self.root, self.log, all=True, dry_run=True)
        self.assertTrue(os.path.isdir(wt))

    def test_dirty_handmade_worktree_kept(self):
        # A registered worktree outside <root>/wt with uncommitted changes
        # survives age-based prune; --all still removes it.
        hand = os.path.join(self.root, "wt-hand")
        subprocess.check_call(["git", "-C", self.repo.path, "worktree",
                               "add", "--detach", hand, self.repo.sha],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(os.path.join(hand, "wip.txt"), "w") as f:
            f.write("uncommitted")
        old = time.time() - 10 * 86400
        os.utime(hand, (old, old))
        worktree_mod.prune(self.root, self.log, older_than_days=3)
        self.assertTrue(os.path.isdir(hand))
        self.assertIn("keeping dirty worktree", self.log.getvalue())
        worktree_mod.prune(self.root, self.log, all=True)
        self.assertFalse(os.path.exists(hand))

    def test_dirty_wt_dir_still_pruned(self):
        # Dirty worktrees under <root>/wt are rep-dbg's own — age rules apply.
        wt = worktree_mod.ensure(self.root, self.repo.sha, self.log)
        with open(os.path.join(wt, "wip.txt"), "w") as f:
            f.write("x")
        old = time.time() - 10 * 86400
        os.utime(os.path.join(self.root, "wt", ".used", self.repo.sha),
                 (old, old))
        worktree_mod.prune(self.root, self.log, older_than_days=3)
        self.assertFalse(os.path.exists(wt))


if __name__ == "__main__":
    unittest.main()
