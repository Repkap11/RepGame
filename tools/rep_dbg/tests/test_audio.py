import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import ab, build, worktree


class TestAudioCompiled(unittest.TestCase):
    def _cache(self, tree, line=None):
        d = os.path.join(tree, "out", "linux", "cmake-release")
        os.makedirs(d)
        with open(os.path.join(d, "CMakeCache.txt"), "w") as f:
            f.write("REPGAME_LTO:BOOL=1\n")
            if line is not None:
                f.write("%s\n" % line)

    def test_on(self):
        with tempfile.TemporaryDirectory() as t:
            self._cache(t, "REPGAME_AUDIO:BOOL=1")
            self.assertIs(build.audio_compiled(t, "release"), True)

    def test_off(self):
        with tempfile.TemporaryDirectory() as t:
            self._cache(t, "REPGAME_AUDIO:BOOL=0")
            self.assertIs(build.audio_compiled(t, "release"), False)

    def test_missing(self):
        with tempfile.TemporaryDirectory() as t:
            self.assertIsNone(build.audio_compiled(t, "release"))
            self._cache(t)  # cache dir/file absent of the line
            self.assertIsNone(build.audio_compiled(t, "release"))


class TestAudioMismatch(unittest.TestCase):
    def test_differ(self):
        msg = ab.audio_mismatch({"base": False, "new": True})
        self.assertIsNotNone(msg)
        self.assertIn("WARNING", msg)
        self.assertIn("base=off", msg)
        self.assertIn("new=on", msg)

    def test_same(self):
        self.assertIsNone(ab.audio_mismatch({"base": True, "new": True}))
        self.assertIsNone(ab.audio_mismatch({"base": False, "new": False}))

    def test_none_ignored(self):
        self.assertIsNone(ab.audio_mismatch({"base": None, "new": True}))
        self.assertIsNone(ab.audio_mismatch({"base": None, "new": None}))
        self.assertIsNone(ab.audio_mismatch({}))


class TestWorktreeBackfill(unittest.TestCase):
    def test_reuse_links_missing_sounds(self):
        """ensure() on an existing wt/<sha> backfills a missing sounds
        symlink without calling git (the reuse path never reaches the
        worktree-add subprocess)."""
        repo = tempfile.mkdtemp()
        root = tempfile.mkdtemp()
        sha = "deadbeef" * 5
        dest = os.path.join(worktree.wt_root(root), sha)
        os.makedirs(dest)
        for d in ("sounds", "sqlite_build"):
            os.makedirs(os.path.join(repo, d))
        orig = build.REPO
        build.REPO = repo
        try:
            out = worktree.ensure(root, sha, io.StringIO())
        finally:
            build.REPO = orig
        self.assertEqual(out, dest)
        for d in ("sounds", "sqlite_build"):
            link = os.path.join(dest, d)
            self.assertTrue(os.path.islink(link), link)
            self.assertEqual(os.readlink(link), os.path.join(repo, d))
        # A dir absent from the main repo is not linked.
        self.assertFalse(os.path.lexists(os.path.join(dest, "imgui_build")))


if __name__ == "__main__":
    unittest.main()
