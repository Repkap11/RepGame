#!/usr/bin/env python3
"""Multiplayer end-to-end test driver for RepGame.

Spins up a real RepGameServer plus headless RepGame clients, drives them via
the REPGAME_MP_CTL command file, and asserts on the REPGAME_MP_DUMP JSONL
stream (see AGENTS.md "Multiplayer entity sync" / src/common/mp_test.cpp).

Usage: tools/mp_test.py [-k substr] [--list] [--keep] [--timeout N]
"""

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "out/server/RepGameServer")
CLIENT = os.path.join(ROOT, "out/linux/release/RepGame")

# Chunks are 62x62; the entity view window is +-8 chunks so two clients a
# chunk or three apart both see an entity dropped between them.
POS_A = (10.0, 90.0, 10.0)
POS_B = (260.0, 90.0, 10.0)  # chunk 4: A keeps chunk-0 ownership past the +2 hysteresis
POS_C = (150.0, 90.0, 10.0)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Dump:
    """Tail of a client's JSONL dump file."""

    def __init__(self, path):
        self.path = path
        self.events = []
        self.states = []
        self._off = 0

    def poll(self):
        try:
            with open(self.path, "r") as f:
                f.seek(self._off)
                data = f.read()
                self._off = f.tell()
        except OSError:
            return
        for line in data.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if obj.get("type") == "event":
                self.events.append(obj)
            elif obj.get("type") == "state":
                self.states.append(obj)

    def last_state(self):
        return self.states[-1] if self.states else None

    def has_event(self, name):
        return any(e.get("name") == name for e in self.events)

    def find_event(self, name):
        for e in self.events:
            if e.get("name") == name:
                return e
        return None


class Client:
    def __init__(self, run, name, pos):
        self.run = run
        self.name = name
        self.dir = os.path.join(run.dir, "client_" + name)
        os.makedirs(self.dir)
        self.ctl_path = os.path.join(self.dir, "ctl.txt")
        self.dump = Dump(os.path.join(self.dir, "dump.jsonl"))
        self.log_path = os.path.join(self.dir, "stderr.log")
        open(self.ctl_path, "w").close()
        self.pos = pos
        self.proc = None

    def start(self):
        env = dict(os.environ)
        env.update({
            "HOME": self.dir,
            "SDL_VIDEODRIVER": "offscreen",
            "SDL_AUDIODRIVER": "dummy",
            "REPGAME_PORT": str(self.run.port),
            "REPGAME_MP_CTL": self.ctl_path,
            "REPGAME_MP_DUMP": self.dump.path,
            "REPGAME_MP_DUMP_EVERY": "6",
            "REPGAME_RENDER_DISTANCE": "6",
            # speed 0 pins the camera; goto commands move the pin.
            "REPGAME_FLY": "%s,%s,%s,0,0,0" % self.pos,
        })
        self.log = open(self.log_path, "w")
        self.proc = subprocess.Popen(
            [CLIENT, "World1", "localhost"],
            cwd=self.dir, env=env, stdout=self.log, stderr=subprocess.STDOUT)

    def cmd(self, line):
        with open(self.ctl_path, "a") as f:
            f.write(line + "\n")

    def goto(self, x, y, z):
        self.pos = (x, y, z)
        self.cmd("goto %s %s %s" % (x, y, z))

    def quit(self):
        self.cmd("quit")

    def stop(self):
        if self.proc is None:
            return
        try:
            self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        try:
            self.log.close()
        except Exception:
            pass

    def log_tail(self, n=30):
        try:
            with open(self.log_path) as f:
                return "".join(f.readlines()[-n:])
        except OSError:
            return "<no log>"


class Run:
    def __init__(self, name, keep=False):
        self.name = name
        self.dir = tempfile.mkdtemp(prefix="mptest_%s_" % name)
        self.keep = keep
        self.port = free_port()
        self.server = None
        self.server_log_path = os.path.join(self.dir, "server.log")
        self.clients = {}

    def start_server(self):
        sdir = os.path.join(self.dir, "server_home")
        os.makedirs(sdir, exist_ok=True)
        env = dict(os.environ)
        env["HOME"] = sdir
        self.server_log = open(self.server_log_path, "a")
        self.server = subprocess.Popen(
            ["stdbuf", "-oL", "-eL", SERVER, str(self.port)], cwd=sdir, env=env,
            stdout=self.server_log, stderr=subprocess.STDOUT)

    def restart_server(self):
        rc0 = self.server.poll()
        self.server.send_signal(signal.SIGINT)
        try:
            self.server.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.server.kill()
            self.server.wait(timeout=15)
            print("server did not exit on SIGINT within 15s (pre-kill rc=%s)" % rc0)
        print("server exited rc=%s (pre-signal poll=%s)" % (self.server.returncode, rc0))
        self.start_server()
        time.sleep(1.0)

    def client(self, name, pos):
        c = Client(self, name, pos)
        self.clients[name] = c
        c.start()
        return c

    def cmd_all(self, line):
        for c in self.clients.values():
            c.cmd(line)

    def wait_ready(self, *clients, timeout=120):
        deadline = time.time() + timeout
        pending = {c.name: c for c in clients}
        while pending and time.time() < deadline:
            for c in list(pending.values()):
                c.dump.poll()
                if c.dump.has_event("ready"):
                    del pending[c.name]
                elif c.proc.poll() is not None:
                    raise Fail("client %s exited early\n%s" % (c.name, c.log_tail()))
            if pending:
                time.sleep(0.1)
        if pending:
            raise Fail("clients never became ready: %s" % ",".join(pending))

    def wait(self, desc, pred, timeout=60, period=0.05):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for c in self.clients.values():
                c.dump.poll()
            if pred():
                return True
            time.sleep(period)
        raise Fail("timeout waiting for: %s" % desc)

    def sleep_ticks(self, n, tick_hz=144):
        end = time.time() + n / tick_hz
        while time.time() < end:
            for c in self.clients.values():
                c.dump.poll()
            time.sleep(0.02)

    def teardown(self, failed):
        for c in self.clients.values():
            try:
                c.quit()
            except Exception:
                pass
        time.sleep(1.0)
        for c in self.clients.values():
            c.stop()
        if self.server:
            try:
                self.server.send_signal(signal.SIGINT)
                self.server.wait(timeout=15)
            except Exception:
                self.server.kill()
            try:
                self.server_log.close()
            except Exception:
                pass
        if failed or self.keep:
            print("  artifacts kept: %s" % self.dir)
        else:
            shutil.rmtree(self.dir, ignore_errors=True)

    def server_log_tail(self, n=30):
        try:
            with open(self.server_log_path) as f:
                return "".join(f.readlines()[-n:])
        except OSError:
            return "<no server log>"


class Fail(Exception):
    pass


def items_of(st):
    return st.get("items", []) if st else []


def mobs_of(st):
    # Remote-player avatars (uid != 0) are synced via player packets, not the
    # entity protocol — they legitimately have no net_id.
    return [m for m in (st.get("mobs", []) if st else []) if m.get("uid", 0) == 0]


def bodies_of(st):
    return st.get("bodies", []) if st else []


def find_item(st, net_id=None):
    for it in items_of(st):
        if net_id is None or it["net_id"] == net_id:
            return it
    return None


def find_mob(st, net_id=None, type_name=None):
    for m in mobs_of(st):
        if net_id is not None and m["net_id"] != net_id:
            continue
        if type_name is not None and m["type_name"] != type_name:
            continue
        return m
    return None


def inv_count(st, name):
    return (st.get("inventory", {}) or {}).get(name, 0) if st else 0


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def wait_probe(run, c, x, y, z, timeout=30):
    """Probe until the block event arrives AND its chunk is loaded; returns
    the last event detail."""
    deadline = time.time() + timeout
    n0 = len(c.dump.events)
    detail = None
    while time.time() < deadline:
        c.cmd("probe %d %d %d" % (x, y, z))
        base = len(c.dump.events)
        t1 = time.time() + 10
        while time.time() < t1:
            c.dump.poll()
            hit = [e for e in c.dump.events[base:]
                   if e.get("name") == "probe" and e.get("detail", {}).get("pos") == [x, y, z]]
            if hit:
                detail = hit[-1]["detail"]
                break
            time.sleep(0.1)
        if detail is None:
            # Fall back to any probe event for this pos (older commands).
            for e in c.dump.events[n0:]:
                if e.get("name") == "probe" and e.get("detail", {}).get("pos") == [x, y, z]:
                    detail = e["detail"]
        if detail is not None and detail.get("name") != "unloaded":
            return detail
        time.sleep(0.5)
    if detail is None:
        raise Fail("probe %d %d %d never returned" % (x, y, z))
    return detail


def _is_solid(ev):
    return ev is not None and ev.get("name") not in ("AIR", "unloaded") and ev.get("block", 0) != 0


def find_ground_y(run, c, x, z, y_hi=120, y_lo=-30):
    """Probe a column for the topmost non-air block; returns its top y.

    Two-phase: coarse steps of 4, then a fine scan of the hit band."""
    hit = None
    for y in range(y_hi, y_lo, -4):
        if _is_solid(wait_probe(run, c, x, y, z)):
            hit = y
            break
    if hit is None:
        raise Fail("no ground found at %d,%d" % (x, z))
    for y in range(min(hit + 4, y_hi), hit, -1):
        if _is_solid(wait_probe(run, c, x, y, z)):
            return y + 1
    return hit + 1


def goto_ground(run, c, x, z, eye=2.0):
    gy = find_ground_y(run, c, int(x), int(z))
    c.goto(x, gy + eye, z)
    return gy


PLATFORM_Y = 100  # block layer; top surface at PLATFORM_Y + 1


def platform(run, who, cx, cz):
    """Build a deterministic 5x5 stone pad so scenarios don't depend on the
    heightmap (canopy leaves / unloaded probes made find_ground_y racy).
    Returns the y of the pad's top surface."""
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            who.cmd("setblock %d %d %d STONE" % (cx + dx, PLATFORM_Y, cz + dz))
    return PLATFORM_Y + 1


def settle(run, A, B):
    """Deterministic arenas: a stone pad under each client, cameras pinned
    just above them so chunk ownership is decided horizontally. Returns the
    pad-top y (same for both)."""
    gy = platform(run, A, 10, 10)
    platform(run, B, 260, 10)
    A.goto(10.5, gy + 1.7, 10.5)
    B.goto(260.5, gy + 1.7, 10.5)
    run.sleep_ticks(80)
    return gy


# --------------------------------------------------------------------------
# Scenarios. Each takes (run, clients...) and raises Fail on a bad assertion.
# --------------------------------------------------------------------------

def sc_item_mirror_visuals(run, A, B):
    """The reported bug: only the owner saw the item spin/bob."""
    gy = settle(run, A, B)
    A.cmd("drop DIRT 4 10.5 %d 10.5" % gy)
    A.goto(10.5, gy + 1.7, 16.5)  # out of pickup range, still chunk 0
    run.wait("item visible on B", lambda: find_item(B.dump.last_state()) is not None, timeout=60)
    run.sleep_ticks(30)
    st_a, st_b = A.dump.last_state(), B.dump.last_state()
    ia, ib = find_item(st_a), find_item(st_b)
    if not ia or not ib:
        raise Fail("item missing on a client")
    if ia["net_id"] != ib["net_id"]:
        raise Fail("net_id mismatch %s vs %s" % (ia["net_id"], ib["net_id"]))
    if ib["remote"] != 1 or ia["remote"] != 0:
        raise Fail("role flags wrong: A.remote=%s B.remote=%s" % (ia["remote"], ib["remote"]))
    run.wait("positions converge", lambda: (
        find_item(A.dump.last_state()) and find_item(B.dump.last_state()) and
        dist(find_item(A.dump.last_state())["pos"], find_item(B.dump.last_state())["pos"]) < 0.05),
        timeout=60)
    ib0 = find_item(B.dump.last_state())
    age0, yaw0, bob0 = ib0["age"], ib0["yaw"], ib0["bob"]
    run.sleep_ticks(20)
    ib1 = find_item(B.dump.last_state())
    if ib1["age"] <= age0:
        raise Fail("mirror age stuck: %d -> %d" % (age0, ib1["age"]))
    if not ib1["grounded"]:
        raise Fail("mirror not grounded at rest")
    if abs(ib1["yaw"] - yaw0) < 1e-3:
        raise Fail("mirror yaw frozen (the reported bug)")
    ia1 = find_item(A.dump.last_state())
    # Spin phase identical (same net-seeded spin_seed), yaw within age-skew
    # tolerance; bob amplitude equal within a small band.
    if abs(ib1["spin_seed"] - ia1["spin_seed"]) > 1e-4:
        raise Fail("spin_seed mismatch %f vs %f" % (ib1["spin_seed"], ia1["spin_seed"]))
    if abs(ib1["yaw"] - ia1["yaw"]) > 0.6:
        raise Fail("yaw divergence %f vs %f" % (ib1["yaw"], ia1["yaw"]))
    if abs(ib1["bob"] - ia1["bob"]) > 0.06:
        raise Fail("bob divergence %f vs %f" % (ib1["bob"], ia1["bob"]))


def sc_item_pickup(run, A, B):
    gy = settle(run, A, B)
    A.cmd("drop DIRT 5 10.5 %d 10.5" % gy)
    A.goto(10.5, gy + 1.7, 18.5)  # out of pickup range; B does the pickup
    run.wait("item on B", lambda: find_item(B.dump.last_state()) is not None, timeout=60)
    run.sleep_ticks(30)  # settle + pickup_delay (10 ticks on drop)
    B.goto(10.5, gy + 1.8, 10.5)
    run.wait("B picked it up", lambda: find_item(B.dump.last_state()) is None and
             find_item(A.dump.last_state()) is None, timeout=60)
    # ENTITY_REMOVE lands before PICKUP_RESULT — wait for the grant itself.
    run.wait("pickup_result on B", lambda: B.dump.has_event("pickup_result"), timeout=30)
    run.wait("grant applied", lambda: inv_count(B.dump.last_state(), "DIRT") == 5, timeout=30)
    st_b = B.dump.last_state()
    if inv_count(st_b, "DIRT") != 5:
        raise Fail("B inventory has %d DIRT, want 5" % inv_count(st_b, "DIRT"))
    if inv_count(A.dump.last_state(), "DIRT") != 0:
        raise Fail("A inventory changed")


def sc_pickup_race(run, A, B):
    gy = settle(run, A, B)
    A.cmd("drop STONE 7 10.5 %d 10.5" % gy)
    run.wait("item on B", lambda: find_item(B.dump.last_state()) is not None, timeout=60)
    run.sleep_ticks(30)
    # Both clients jump on it within the same tick window.
    A.goto(10.5, gy + 1.8, 10.5)
    B.goto(10.5, gy + 1.8, 10.5)
    run.wait("item gone both", lambda: find_item(A.dump.last_state()) is None and
             find_item(B.dump.last_state()) is None, timeout=90)
    run.wait("grant applied", lambda: inv_count(A.dump.last_state(), "STONE") +
             inv_count(B.dump.last_state(), "STONE") == 7, timeout=30)
    got = inv_count(A.dump.last_state(), "STONE") + inv_count(B.dump.last_state(), "STONE")
    if got != 7:
        raise Fail("total granted %d, want exactly 7 (dup or loss)" % got)


def role_of(run, c, x, y, z, timeout=30):
    """Issue `role` and wait for the matching event; returns the role int."""
    deadline = time.time() + timeout
    base = len(c.dump.events)
    while time.time() < deadline:
        c.cmd("role %d %d %d" % (x, y, z))
        t1 = time.time() + 5
        while time.time() < t1:
            c.dump.poll()
            hit = [e for e in c.dump.events[base:]
                   if e.get("name") == "role" and e.get("detail", {}).get("chunk") == [x, y, z]]
            if hit:
                return hit[-1]["detail"]["role"]
            time.sleep(0.1)
    raise Fail("role probe %d %d %d never returned" % (x, y, z))


def sc_boundary_rest(run, A, B):
    """Regression for the net_chunk/hysteresis desync: an item resting inside
    the 0.5-block margin of a foreign chunk must keep streaming (server keeps
    it in the old chunk), not freeze as a mirror."""
    gy = settle(run, A, B)
    # The +2-chunk incumbent hysteresis means no adjacent pair has a
    # deterministic A|B split — probe the actual roles and find the boundary:
    # a chunk A owns whose +x neighbour is owned by B.
    roles = {cx: (role_of(run, A, cx, 1, 0), role_of(run, B, cx, 1, 0)) for cx in range(-1, 7)}
    bc = None
    for cx in range(-1, 6):
        if roles[cx][0] == 2 and roles[cx + 1][0] != 2 and roles[cx + 1][1] == 2:
            bc = cx
    if bc is None:
        raise Fail("no A-owned|B-owned chunk boundary found, roles=%s" % (roles,))
    bx = 62 * (bc + 1)  # boundary x between chunk bc (A) and bc+1 (B)
    # Runway across the boundary.
    for dx in range(int(bx) - 4, int(bx) + 10):
        A.cmd("setblock %d %d 10 STONE" % (dx, gy - 1))
    run.sleep_ticks(10)
    # Phase 1: slide to rest inside the B-owned chunk's margin
    # (x in (bx, bx+0.5)). Pre-fix the sim would adopt floor(pos) as
    # net_chunk, see it as foreign, and stop streaming -> frozen mirror.
    A.cmd("drop DIRT 1 %f %d 10.5 0.15 0 0" % (bx - 0.3, gy))
    run.wait("item on both", lambda: find_item(A.dump.last_state()) is not None and
             find_item(B.dump.last_state()) is not None, timeout=60)
    nid = find_item(A.dump.last_state())["net_id"]

    def at_rest(c):
        i = find_item(c.dump.last_state(), nid)
        return i is not None and sum(abs(v) for v in i["vel"]) < 1e-3
    run.wait("item at rest on A", lambda: at_rest(A), timeout=120)
    run.wait("mirror at rest on B", lambda: at_rest(B), timeout=30)
    pa = find_item(A.dump.last_state(), nid)["pos"]
    pb = find_item(B.dump.last_state(), nid)["pos"]
    if not (bx < pa[0] < bx + 0.5):
        raise Fail("rest pos %s not in the boundary margin (%d,%d)" % (pa, bx, bx + 0.5))
    ia, ib = find_item(A.dump.last_state(), nid), find_item(B.dump.last_state(), nid)
    if ia["remote"] != 0 or ib["remote"] != 1:
        raise Fail("margin rest flipped roles: A.remote=%s B.remote=%s" % (ia["remote"], ib["remote"]))
    if dist(pa, pb) > 0.05:
        raise Fail("mirror frozen/diverged at margin rest: %s vs %s" % (pa, pb))
    # Phase 2: blast it deep into the B-owned chunk -> ownership transfers.
    A.cmd("tnt %f %d 10.5" % (bx - 1.5, gy + 1))
    run.wait("B owns / A mirrors", lambda: (
        find_item(B.dump.last_state(), nid) is not None and
        find_item(B.dump.last_state(), nid)["remote"] == 0 and
        find_item(A.dump.last_state(), nid) is not None and
        find_item(A.dump.last_state(), nid)["remote"] == 1), timeout=240)
    run.wait("positions converge", lambda: dist(
        find_item(A.dump.last_state(), nid)["pos"],
        find_item(B.dump.last_state(), nid)["pos"]) < 0.5, timeout=60)
    if find_item(A.dump.last_state(), nid)["pos"][0] < bx + 0.5:
        raise Fail("item did not cross deep into the B-owned chunk")


def sc_item_transfer(run, A, B):
    gy = settle(run, A, B)
    A.cmd("drop STONE 1 10.5 %d 10.5" % gy)
    A.goto(10.5, gy + 1.7, 16.5)
    run.wait("item exists", lambda: find_item(A.dump.last_state()) is not None, timeout=30)
    nid = find_item(A.dump.last_state())["net_id"]
    run.sleep_ticks(40)
    # Ownership = nearest player with +2 hysteresis: B moves onto the item's
    # chunk (tie, A keeps it), then A moves 4 chunks away so B wins outright.
    B.goto(10.5, gy + 1.7, 14.5)  # same chunk, outside the 2-block magnet
    run.sleep_ticks(60)
    A.goto(260.5, gy + 1.7, 10.5)
    # Player-driven ownership flips arrive as CHUNK_ROLE records (not
    # ENTITY_TRANSFER — that message is only for entity-driven crossings).
    run.wait("B owns / A mirrors", lambda: (
        find_item(B.dump.last_state(), nid) is not None and
        find_item(B.dump.last_state(), nid)["remote"] == 0 and
        find_item(A.dump.last_state(), nid) is not None and
        find_item(A.dump.last_state(), nid)["remote"] == 1), timeout=60)
    pa, pb = find_item(A.dump.last_state(), nid)["pos"], find_item(B.dump.last_state(), nid)["pos"]
    if dist(pa, pb) > 0.5:
        raise Fail("positions diverged after transfer: %s vs %s" % (pa, pb))


def sc_ownership_flip_no_dup(run, A, B):
    gy = settle(run, A, B)
    # Test entities live at mid-pad (chunk 2): both pads' views always cover
    # it, and it sits out of either pad's item pickup range.
    A.cmd("drop DIRT 1 135.5 %d 10.5" % gy)
    A.cmd("drop STONE 2 136.5 %d 10.5" % gy)
    A.cmd("mob PIG 135.5 %d 12.5" % gy)
    pre = {m["net_id"] for m in mobs_of(B.dump.last_state())}
    run.wait("entities on B", lambda: len(items_of(B.dump.last_state())) >= 2 and
             any(m["net_id"] not in pre for m in mobs_of(B.dump.last_state())), timeout=60)
    # Ids of the entities this scenario spawned (near x=135).
    def test_ids(c):
        st = c.dump.last_state()
        near = lambda p: 130 <= p[0] < 142
        return {e["net_id"] for e in items_of(st) + mobs_of(st) if near(e["pos"])}
    run.wait("three test entities on A", lambda: len(test_ids(A)) >= 3, timeout=60)
    want = test_ids(A)

    # Band shared by both pads in every hop position: chunks [-2,6] x,
    # [-6,6] z, [-2,4] y — comparisons must be confined to it because the
    # players end on opposite pads (different full views).
    def snapshot(c):
        st = c.dump.last_state()
        band = lambda p: -2 * 62 <= p[0] < 7 * 62 and -6 * 62 <= p[2] < 7 * 62 and -2 * 62 <= p[1] < 5 * 62
        return (sorted(i["net_id"] for i in items_of(st) if band(i["pos"])),
                sorted(m["net_id"] for m in mobs_of(st) if band(m["pos"])))

    # Drag both players across the hysteresis boundary repeatedly (the two
    # pads are 4 chunks apart — each hop is a decisive ownership flip).
    for step in range(3):
        A.goto(10, gy + 1.7, 10)
        B.goto(260, gy + 1.7, 10)
        run.sleep_ticks(60)
        sa, sb = snapshot(A), snapshot(B)
        if sa != sb:
            raise Fail("entity sets differ at hop %d: %s vs %s" % (step, sa, sb))
        A.goto(260, gy + 1.7, 10)
        B.goto(10, gy + 1.7, 10)
        run.sleep_ticks(60)
        sa, sb = snapshot(A), snapshot(B)
        if sa != sb:
            raise Fail("entity sets differ at hop %d: %s vs %s" % (step, sa, sb))
    missing = want - (set(sa[0]) | set(sa[1]))
    if missing:
        raise Fail("test entities lost after flips: %s" % (missing,))


def sc_natural_mobs(run, A, B):
    settle(run, A, B)
    run.wait("mobs present on A", lambda: len(mobs_of(A.dump.last_state())) > 0, timeout=240)

    # Entity visibility is clamped by each client's chunk view (render
    # distance 6 < NET_ENTITY_RADIUS_XZ): A covers chunks [-6,6]x, B covers
    # [-2,10]; z [-6,6] and y [-2,4] for both — compare inside the overlap.
    def mob_ids(c):
        return sorted(m["net_id"] for m in mobs_of(c.dump.last_state())
                      if -2 * 62 <= m["pos"][0] < 7 * 62 and
                         -6 * 62 <= m["pos"][2] < 7 * 62 and
                         -2 * 62 <= m["pos"][1] < 5 * 62)
    run.wait("same mob set", lambda: mob_ids(A) == mob_ids(B) and len(mob_ids(A)) > 0, timeout=120)
    for m in mobs_of(A.dump.last_state()) + mobs_of(B.dump.last_state()):
        if m["net_id"] == "0":
            raise Fail("mob without net_id while connected")
    owned = [m["net_id"] for m in mobs_of(A.dump.last_state())
             if not m["remote"] and -2 * 62 <= m["pos"][0] < 7 * 62]
    mirror_b = [m["net_id"] for m in mobs_of(B.dump.last_state())
                if m["remote"] and -2 * 62 <= m["pos"][0] < 7 * 62]
    if set(owned) != set(mirror_b):
        raise Fail("mob not remote on exactly one client")
    n0 = len(mob_ids(A))
    st0 = B.dump.last_state()
    run.sleep_ticks(100)
    if len(mob_ids(A)) != n0:
        raise Fail("mob count changed after settle: %d -> %d" % (n0, len(mob_ids(A))))
    # Mirror visual: a moving remote mob must advance its walk animation
    # (walk_dist is derived from streamed motion on mirrors).
    m0 = {m["net_id"]: m for m in mobs_of(st0)}
    moved = 0
    for m in mobs_of(B.dump.last_state()):
        old = m0.get(m["net_id"])
        if old and m["remote"] and dist(old["pos"], m["pos"]) > 0.3:
            moved += 1
            if m["walk_dist"] > old["walk_dist"]:
                break
    else:
        if moved:
            raise Fail("mirror mob walk phase not advancing while moving")


def sc_mob_hit_kill(run, A, B):
    gy = settle(run, A, B)
    B.cmd("mode survival")  # survival hits deal damage and spawn drops
    pre = {m["net_id"] for m in mobs_of(A.dump.last_state())}
    A.cmd("mob PIG 10.5 %d 10.5" % gy)
    run.wait("mob spawned on A",
             lambda: any(m["net_id"] not in pre for m in mobs_of(A.dump.last_state())), timeout=60)
    nid = next(m["net_id"] for m in mobs_of(A.dump.last_state()) if m["net_id"] not in pre)
    run.wait("mob on B", lambda: find_mob(B.dump.last_state(), nid) is not None, timeout=60)
    h0 = find_mob(A.dump.last_state(), nid)["health"]
    # Step A away from the mob so the survival drops aren't instantly picked
    # up by A's own player.
    A.goto(10.5, gy + 1.7, 18.5)
    run.sleep_ticks(10)
    for _ in range(10):
        B.cmd("hit %s" % nid)
        run.sleep_ticks(5)
        if find_mob(B.dump.last_state(), nid) is None:
            break
    # Death animation runs ~2s before the owner emits ENTITY_REMOVE(DIED).
    run.wait("mob removed on both",
             lambda: find_mob(A.dump.last_state(), nid) is None and find_mob(B.dump.last_state(), nid) is None,
             timeout=240)
    if h0 <= 0:
        raise Fail("mob spawned dead")
    # Survival kill: drops appear on both clients with identical net ids.
    run.wait("drops on both", lambda: len(items_of(A.dump.last_state())) > 0 and
             sorted(i["net_id"] for i in items_of(A.dump.last_state())) ==
             sorted(i["net_id"] for i in items_of(B.dump.last_state())), timeout=120)


def sc_hostile_targets_remote(run, A, B):
    gy = settle(run, A, B)
    A.cmd("time 140000")  # deep night (day = 172800 ticks; 13000 burned it)
    B.cmd("mode survival")  # only walking-survival players are targetable
    run.sleep_ticks(30)
    # Zombie in A's chunk (A simulates it). B walks next to it — the mob is
    # A-owned, so melee lands as PLAYER_HIT on B.
    A.cmd("mob ZOMBIE 12.5 %d 10.5" % gy)
    run.wait("zombie on A", lambda: find_mob(A.dump.last_state(), type_name="zombie") is not None, timeout=60)
    B.goto(13.5, gy + 1.7, 10.5)
    run.wait("B gets hit", lambda: B.dump.has_event("player_hit_received"), timeout=180)


def sc_falling_block(run, A, B):
    gy = settle(run, A, B)
    A.cmd("fall SAND 12 %d 10" % (gy + 6))
    run.wait("body on B", lambda: len(bodies_of(B.dump.last_state())) > 0, timeout=30)
    run.wait("body lands", lambda: len(bodies_of(A.dump.last_state())) == 0 and
             len(bodies_of(B.dump.last_state())) == 0, timeout=90)
    pa = wait_probe(run, A, 12, gy, 10)
    pb = wait_probe(run, B, 12, gy, 10)
    if pa.get("block") != pb.get("block") or "SAND" not in pa.get("name", ""):
        raise Fail("landed block mismatch: %s vs %s" % (pa, pb))


def sc_tnt_explosion(run, A, B):
    gy = settle(run, A, B)
    # Blast at B's pad: an item B owns sits next to the TNT and must get
    # impulsed when B receives the remote EXPLOSION.
    A.cmd("setblock 262 %d 10 STONE" % gy)
    A.cmd("setblock 263 %d 10 STONE" % gy)
    run.sleep_ticks(10)
    B.cmd("drop GRAVEL 1 264.5 %d 10.5 0 0 0" % (gy + 1))
    run.wait("B-owned item on A", lambda: find_item(A.dump.last_state()) is not None, timeout=60)
    nid = find_item(A.dump.last_state())["net_id"]
    item0 = find_item(B.dump.last_state(), nid)
    A.cmd("tnt 262.5 %d 10.5" % (gy + 1))
    run.wait("body on B", lambda: len(bodies_of(B.dump.last_state())) > 0, timeout=30)
    # Mirror visual: the TNT fuse/blink clock must run locally on the mirror.
    f0 = bodies_of(B.dump.last_state())[0].get("fuse_left")
    run.sleep_ticks(20)
    f1 = bodies_of(B.dump.last_state())[0].get("fuse_left") if bodies_of(B.dump.last_state()) else None
    if f0 is None or f1 is None or f1 >= f0:
        raise Fail("mirror TNT fuse not ticking: %s -> %s" % (f0, f1))
    run.wait("explosion on B", lambda: B.dump.has_event("explosion_received"), timeout=120)
    run.wait("bodies gone", lambda: len(bodies_of(A.dump.last_state())) == 0 and
             len(bodies_of(B.dump.last_state())) == 0, timeout=60)
    pa = wait_probe(run, A, 262, gy, 10)
    pb = wait_probe(run, B, 262, gy, 10)
    if pa.get("block") != pb.get("block"):
        raise Fail("post-blast blocks differ: %s vs %s" % (pa, pb))
    item1 = find_item(B.dump.last_state(), nid)
    if item1 is not None and dist(item0["pos"], item1["pos"]) < 0.1:
        raise Fail("B-owned item not impulsed by remote blast")


def sc_tnt_handover(run, A, B):
    gy = settle(run, A, B)
    B.cmd("setblock 260 %d 10 STONE" % gy)
    run.sleep_ticks(10)
    A.cmd("tnt 260.5 %d 10.5" % (gy + 1))
    run.wait("body on B", lambda: len(bodies_of(B.dump.last_state())) > 0, timeout=30)
    run.sleep_ticks(10)
    A.quit()
    run.wait("B adopts body", lambda: B.dump.has_event("entity_transfer"), timeout=60)
    # B is now the simulating owner: the fuse completes locally on B (no
    # explosion_received — that event is for *other* clients). The body is
    # consumed by the rescheduled TntExplodeEvent.
    run.wait("body gone on B", lambda: len(bodies_of(B.dump.last_state())) == 0, timeout=240)
    p = wait_probe(run, B, 260, gy, 10)
    if "STONE" in p.get("name", ""):
        raise Fail("stone survived adopted TNT: %s" % (p,))
    # Third client joins after the fact and sees the same block.
    C = run.client("C", POS_C)
    run.wait_ready(C)
    pc = wait_probe(run, C, 260, gy, 10)
    if p.get("block") != pc.get("block"):
        raise Fail("late joiner sees different block: %s vs %s" % (p, pc))


def sc_persistence(run, A, B):
    gy = settle(run, A, B)
    A.cmd("drop DIRT 3 10.5 %d 10.5" % gy)
    A.cmd("mob COW 12.5 %d 10.5" % gy)
    A.goto(10.5, gy + 1.7, 18.5)
    run.wait("entities live", lambda: find_item(A.dump.last_state()) is not None and
             find_mob(A.dump.last_state(), type_name="cow") is not None, timeout=60)
    run.sleep_ticks(20)
    for c in list(run.clients.values()):
        c.quit()
    run.sleep_ticks(10)
    run.restart_server()
    C = run.client("C", (30.0, 90.0, 10.0))
    run.wait_ready(C)
    run.wait("restored entities", lambda: find_item(C.dump.last_state()) is not None and
             find_mob(C.dump.last_state(), type_name="cow") is not None, timeout=120)
    it = find_item(C.dump.last_state())
    if it["count"] != 3:
        raise Fail("restored item count %d, want 3" % it["count"])


def sc_disconnect_cleanup(run, A, B):
    gy = settle(run, A, B)
    A.cmd("drop DIRT 1 10.5 %d 10.5" % gy)
    A.goto(10.5, gy + 1.7, 16.5)
    run.wait("item on B", lambda: find_item(B.dump.last_state()) is not None, timeout=60)
    B.quit()
    run.sleep_ticks(60)
    if find_item(A.dump.last_state()) is None:
        raise Fail("item vanished on A after B quit")
    B2 = run.client("B2", POS_B)
    run.wait_ready(B2)
    run.wait("item back on reconnect", lambda: find_item(B2.dump.last_state()) is not None, timeout=120)


SCENARIOS = [
    ("item_mirror_visuals", sc_item_mirror_visuals, 2),
    ("item_pickup", sc_item_pickup, 2),
    ("pickup_race", sc_pickup_race, 2),
    ("item_transfer", sc_item_transfer, 2),
    ("boundary_rest", sc_boundary_rest, 2),
    ("ownership_flip_no_dup", sc_ownership_flip_no_dup, 2),
    ("natural_mobs", sc_natural_mobs, 2),
    ("mob_hit_kill", sc_mob_hit_kill, 2),
    ("hostile_targets_remote", sc_hostile_targets_remote, 2),
    ("falling_block", sc_falling_block, 2),
    ("tnt_explosion", sc_tnt_explosion, 2),
    ("tnt_handover", sc_tnt_handover, 2),
    ("persistence", sc_persistence, 2),
    ("disconnect_cleanup", sc_disconnect_cleanup, 2),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", dest="filt", default=None, help="run scenarios matching substring")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep", action="store_true", help="keep all artifact dirs")
    args = ap.parse_args()

    if args.list:
        for name, _, _ in SCENARIOS:
            print(name)
        return 0
    for p in (SERVER, CLIENT):
        if not os.path.exists(p):
            print("missing binary: %s (run make linux server-test first)" % p)
            return 2

    selected = [(n, f, c) for n, f, c in SCENARIOS if not args.filt or args.filt in n]
    if args.filt and not selected:
        print("no scenarios match -k %r" % args.filt)
        return 2

    fails = []
    for name, fn, nclients in selected:
        print("=== %s" % name, flush=True)
        run = Run(name, keep=args.keep)
        failed = False
        try:
            run.start_server()
            A = run.client("A", POS_A)
            B = run.client("B", POS_B)
            run.wait_ready(A, B)
            fn(run, A, B)
            print("  PASS", flush=True)
        except Fail as e:
            failed = True
            fails.append((name, str(e)))
            print("  FAIL: %s" % e, flush=True)
        except Exception as e:
            failed = True
            fails.append((name, "exception: %r" % e))
            print("  EXC: %r" % e, flush=True)
            import traceback
            traceback.print_exc()
        finally:
            run.teardown(failed)
    if fails:
        print("\n%d scenario(s) failed:" % len(fails))
        for n, e in fails:
            print("  %s: %s" % (n, e))
        return 1
    print("\nAll scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
