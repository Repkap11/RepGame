import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import flame


SCRIPT = """\
RepGame  1234 [001] 100.0: cycles:
        7f00 leaf_fn (/bin/game)
        7f01 mid_fn (/bin/game)
        7f02 main (/bin/game)

RepGame  1234 [001] 100.1: cycles:
        7f00 leaf_fn (/bin/game)
        7f01 mid_fn (/bin/game)
        7f02 main (/bin/game)

loader-1  1235 [002] 100.2: cycles:
        7f03 work (/bin/game)
        7f04 loop (/bin/game)

"""


class TestCollapse(unittest.TestCase):
    def test_stacks(self):
        stacks, by_thread = flame.collapse(SCRIPT)
        self.assertEqual(stacks.get("main;mid_fn;leaf_fn"), 2)
        self.assertEqual(stacks.get("loop;work"), 1)
        self.assertIn("loader-1", by_thread)
        self.assertEqual(by_thread["loader-1"].get("loop;work"), 1)


class TestSvg(unittest.TestCase):
    def test_valid_svg(self):
        stacks, _ = flame.collapse(SCRIPT)
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "f.svg")
            flame.write_svg(stacks, out, title="t")
            root = ET.parse(out).getroot()
            self.assertTrue(root.tag.endswith("svg"))
            rects = [e for e in root.iter() if e.tag.endswith("rect")]
            self.assertGreaterEqual(len(rects), 4)


if __name__ == "__main__":
    unittest.main()
