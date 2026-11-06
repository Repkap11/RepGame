import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from rep_dbg import logparse

LOG = """\
src/common/game/game_tick.cpp:514:tick():FLYMET t=20 ph=1 rd=6 p=(10.5,80.0,-3.2) cl=1 j=2 p=3 c=4 d=5 x=6 bfs=7 pri=8 stv=9 s=10 stl=11 occ=12/40 | loop=1 aj=2 pq=3/4 jobs=5 | us: fin=9(lfin=4[fill=2 casc=1 bscan=1 border=1]) remesh=3
src/common/game/game_tick.cpp:425:tick():Fly mode: settled, flight begins at tick 40
src/common/game/game_tick.cpp:499:tick():Capture view 2: shot pending after 61 ticks
src/common/game/game_tick.cpp:499:tick():Capture view 3: shot pending after 600 ticks (TIMEOUT — loader still busy)
some other line
"""


class TestLogparse(unittest.TestCase):
    def test_flymet(self):
        recs = logparse.parse_lines(LOG.splitlines(), git="abc")
        self.assertEqual(recs[0]["type"], "header")
        self.assertEqual(recs[0]["source"], "log-shim")
        self.assertEqual(recs[0]["git"], "abc")
        m = recs[1]
        self.assertEqual(m["type"], "metric")
        self.assertEqual(m["tick"], 20)
        self.assertEqual(m["fly_phase"], 1)
        self.assertEqual(m["rd"], 6)
        self.assertEqual(m["pos"], [10.5, 80.0, -3.2])
        self.assertEqual(m["loader"]["light_jobs"], 2)
        self.assertEqual(m["loader"]["light_pending"], 3)
        self.assertEqual(m["loader"]["chunks_loading"], 1)
        self.assertEqual(m["loader"]["chunks_occluded"], 12)
        self.assertEqual(m["loader"]["chunks_drawable"], 40)
        self.assertEqual(m["light"]["loop"], 1)
        self.assertEqual(m["light"]["pq"], "3/4")
        self.assertEqual(m["light"]["us_fin"], 9)
        self.assertEqual(m["light"]["us_lfin"], 4)
        self.assertEqual(m["light"]["us_remesh"], 3)

    def test_events(self):
        recs = logparse.parse_lines(LOG.splitlines())
        events = [r for r in recs if r["type"] == "event"]
        self.assertEqual(events[0]["name"], "fly_settled")
        self.assertEqual(events[0]["tick"], 40)
        self.assertEqual(events[1]["name"], "capture_shot")
        self.assertEqual(events[1]["detail"]["view"], 2)
        self.assertEqual(events[1]["detail"]["timeout"], 0)
        self.assertEqual(events[2]["detail"]["timeout"], 1)


if __name__ == "__main__":
    unittest.main()
