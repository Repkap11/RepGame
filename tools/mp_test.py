#!/usr/bin/env python3
"""Multiplayer end-to-end test driver for RepGame.

Spins up a real RepGameServer plus headless RepGame clients, drives them via
the REPGAME_MP_CTL command file, and asserts on the REPGAME_MP_DUMP JSONL
stream (see docs/entities.md "Multiplayer entity sync", the mp-test skill
and src/common/mp_test.cpp).

Usage: tools/mp_test.py [-k substr] [--list] [--keep]
"""

import argparse
import json
import math
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
    def __init__(self, run, name, pos, fly=True):
        self.run = run
        self.name = name
        self.fly = fly
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
        })
        if self.fly:
            # speed 0 pins the camera; goto commands move the pin.
            env["REPGAME_FLY"] = "%s,%s,%s,0,0,0" % self.pos
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
        scratch = os.path.join(tempfile.gettempdir(), "repgame")
        os.makedirs(scratch, exist_ok=True)
        self.dir = tempfile.mkdtemp(prefix="mptest_%s_" % name, dir=scratch)
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

    def client(self, name, pos, fly=True):
        c = Client(self, name, pos, fly=fly)
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


def arrows_of(st):
    return st.get("arrows", []) if st else []


def find_arrow(st, net_id=None):
    for a in arrows_of(st):
        if net_id is None or a["net_id"] == net_id:
            return a
    return None


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


def cprobe(c, kind, x, y, z, timeout=8):
    """Issue one cprobe and poll for the matching event; returns the newest
    detail for this kind+pos, or None on timeout."""
    base = len(c.dump.events)
    c.cmd("cprobe %s %d %d %d" % (kind, x, y, z))
    end = time.time() + timeout
    while time.time() < end:
        c.dump.poll()
        hits = [e for e in c.dump.events[base:]
                if e.get("name") == "cprobe"
                and e.get("detail", {}).get("kind") == kind
                and e.get("detail", {}).get("pos") == [x, y, z]]
        if hits:
            return hits[-1]["detail"]
        time.sleep(0.05)
    return None


def cslot(detail, slot):
    """The {"slot","name","count"} entry for `slot` in a cprobe detail."""
    for s in (detail or {}).get("slots", []):
        if s["slot"] == slot:
            return s
    return None


def wait_cslot(run, c, kind, pos, slot, name=None, count=None, timeout=40, desc=None):
    """Re-probe until the container's `slot` holds `name` x `count`
    (name=None means empty). Returns the satisfying cprobe detail."""
    deadline = time.time() + timeout
    detail = None
    while time.time() < deadline:
        detail = cprobe(c, kind, *pos, timeout=8)
        if detail is not None:
            s = cslot(detail, slot)
            if name is None and s is None:
                return detail
            if s is not None and s["name"] == name and (count is None or s["count"] == count):
                return detail
        time.sleep(0.4)
    raise Fail("timeout waiting for %s %s slot %d = %s x%s (last=%s)"
               % (desc or "container", kind, slot, name, count, detail))


def signprobe(c, x, y, z, timeout=8):
    """Issue one signprobe and poll for the matching event; returns the
    newest detail for this pos, or None on timeout."""
    base = len(c.dump.events)
    c.cmd("signprobe %d %d %d" % (x, y, z))
    end = time.time() + timeout
    while time.time() < end:
        c.dump.poll()
        hits = [e for e in c.dump.events[base:]
                if e.get("name") == "signprobe"
                and e.get("detail", {}).get("pos") == [x, y, z]]
        if hits:
            return hits[-1]["detail"]
        time.sleep(0.05)
    return None


def wait_sign(run, c, pos, lines, timeout=40, desc=None):
    """Re-probe until the sign's stored lines equal `lines` ('|'-joined;
    "" means no record or an all-empty one). Returns the detail."""
    # signprobe always joins all four lines, so pad expected with empties.
    want = "|".join((lines.split("|") + ["", "", "", ""])[:4]) if lines else ""
    deadline = time.time() + timeout
    detail = None
    while time.time() < deadline:
        detail = signprobe(c, *pos, timeout=8)
        if detail is not None:
            got = detail.get("lines", "") if detail.get("has", 0) else ""
            if got.rstrip("|") == want.rstrip("|"):
                return detail
        time.sleep(0.4)
    raise Fail("timeout waiting for sign %s lines = %r (last=%s)"
               % (desc or "", lines, detail))


def wait_block(run, c, x, y, z, name, timeout=60):
    """Re-probe until the block at pos reports `name`."""
    deadline = time.time() + timeout
    got = None
    while time.time() < deadline:
        got = wait_probe(run, c, x, y, z, timeout=10).get("name")
        if got == name:
            return
    raise Fail("timeout waiting for block %s at %d %d %d (got %s)" % (name, x, y, z, got))


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
    # Role grants apply only once the local chunk is CHUNK_LIVE. Now that the
    # first VIEW is already the pinned position (no bogus origin advertisement
    # warming the shared band first), the far edge of each client's torus —
    # exactly where the other player stands — loads last. Wait until every
    # advertised chunk's grant has landed on both clients: rx=rd=6, ry=3.
    full = (2 * 6 + 1) * (2 * 3 + 1) * (2 * 6 + 1)
    run.wait("full role coverage", lambda: all(
        c.dump.last_state() is not None and
        sum(c.dump.last_state()["roles"].values()) >= full
        for c in (A, B)), timeout=300)
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


def sc_first_view_correct(run, A, B):
    """Regression: the first advertised VIEW must be the real player chunk,
    not the pre-spawn default camera at (0,0,0)."""
    chunk_of = lambda p: [int(math.floor(v / 62)) for v in p]

    def first_view(c):
        for e in c.dump.events:
            if e.get("name") == "view_sent":
                return e
        return None

    # (a) Fly clients: the first VIEW is already the pin's chunk.
    run.wait("view_sent on A", lambda: first_view(A) is not None, timeout=60)
    run.wait("view_sent on B", lambda: first_view(B) is not None, timeout=60)
    for c, pos in ((A, POS_A), (B, POS_B)):
        center = first_view(c)["detail"]["center"]
        want = chunk_of(pos)
        if center != want:
            raise Fail("%s first VIEW center %s, want pin chunk %s" % (c.name, center, want))

    # (b) Non-fly client on a fresh HOME (spawn_pending path): the first VIEW
    # must come after the spawn column resolves — its center equals the chunk
    # of the camera pos in the state at/after that tick.
    C = run.client("C", POS_A, fly=False)
    run.wait("view_sent on C", lambda: first_view(C) is not None, timeout=180)
    ev = first_view(C)
    st = None
    deadline = time.time() + 30
    while st is None and time.time() < deadline:
        C.dump.poll()
        st = next((s for s in C.dump.states if s["tick"] >= ev["tick"]), None)
        if st is None:
            time.sleep(0.1)
    if st is None:
        raise Fail("no state dump at/after C's first VIEW (tick %d)" % ev["tick"])
    center = ev["detail"]["center"]
    cam_chunk = chunk_of(st["pos"])
    if center != cam_chunk:
        raise Fail("C first VIEW center %s != camera chunk %s (pos %s)" % (center, cam_chunk, st["pos"]))


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
    # The transfer wait above already proves the item sat >=0.5 deep inside
    # B's chunk; where it settles afterwards (it can slide back inside the
    # margin once the crater forms) doesn't matter — hysteresis keeps it B's.
    if find_item(A.dump.last_state(), nid)["pos"][0] <= bx:
        raise Fail("item slid back out of the B-owned chunk")


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
    # Natural mobs spawn/despawn inside the band between the two clients'
    # dump instants, so compare via a wait instead of one-shot snapshots.
    sa = None
    for step in range(3):
        for ax, bx in ((10, 260), (260, 10)):
            A.goto(ax, gy + 1.7, 10)
            B.goto(bx, gy + 1.7, 10)
            run.sleep_ticks(60)
            run.wait("entity sets match at hop %d" % step,
                     lambda: snapshot(A) == snapshot(B), timeout=60)
            sa = snapshot(A)
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
    B.cmd("mode survival")  # survival + walking so the avatar is a real player
    B.cmd("peaceful off")   # only non-peaceful players are targetable
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


def sc_arrow_shoot(run, A, B):
    """Bow arrow: the shooter simulates, the peer mirrors — spawn relay,
    block sticking, anchor release and owner walk-over pickup."""
    gy = settle(run, A, B)
    A.cmd("mode survival")  # pickup feeds the survival inventory
    # Wall at x=20 plus a runway row so the released arrow lands on stone.
    for x in range(13, 22):
        A.cmd("setblock %d %d 10 STONE" % (x, gy - 1))
    for y in range(gy, gy + 3):
        A.cmd("setblock 20 %d 10 STONE" % y)
    run.sleep_ticks(10)
    A.cmd("shoot 14.5 %f 10.5 1 0 0" % (gy + 1.0))
    run.wait("arrow on A", lambda: find_arrow(A.dump.last_state()) is not None, timeout=30)
    run.wait("arrow mirror on B", lambda: find_arrow(B.dump.last_state()) is not None, timeout=60)
    run.wait("arrow stuck on both", lambda: (
        (find_arrow(A.dump.last_state()) or {}).get("stuck") == 1 and
        (find_arrow(B.dump.last_state()) or {}).get("stuck") == 1), timeout=120)
    a, b = find_arrow(A.dump.last_state()), find_arrow(B.dump.last_state())
    if a["net_id"] != b["net_id"]:
        raise Fail("net_id mismatch %s vs %s" % (a["net_id"], b["net_id"]))
    if a["remote"] != 0 or b["remote"] != 1:
        raise Fail("role flags wrong: A.remote=%s B.remote=%s" % (a["remote"], b["remote"]))
    # The mirror eases toward the streamed pose (0.35/tick), so the first
    # dump showing stuck=1 can still lag the final pos — wait out the ease
    # rather than asserting on the first sighting.
    def mirror_converged():
        aa = find_arrow(A.dump.last_state())
        bb = find_arrow(B.dump.last_state())
        return aa is not None and bb is not None and dist(aa["pos"], bb["pos"]) <= 0.5
    run.wait("stuck mirror converged", mirror_converged, timeout=60)
    nid = a["net_id"]
    # Breaking the anchor cell releases the arrow; it drops and re-sticks
    # on the runway top on both clients.
    A.cmd("setblock %d %d %d AIR" % tuple(a["stuck_cell"]))
    run.wait("arrow drops to runway", lambda: (
        find_arrow(A.dump.last_state(), nid) is not None and
        find_arrow(A.dump.last_state(), nid)["pos"][1] < gy + 0.5), timeout=120)
    run.wait("re-stuck on both", lambda: (
        (find_arrow(A.dump.last_state(), nid) or {}).get("stuck") == 1 and
        (find_arrow(B.dump.last_state(), nid) or {}).get("stuck") == 1), timeout=60)
    # Only the sim owner picks up: A walks over the shaft, the mirror dies
    # via ENTITY_REMOVE and an ARROW lands in A's inventory.
    p = find_arrow(A.dump.last_state(), nid)["pos"]
    A.goto(p[0], gy + 1.7, p[2])
    run.wait("arrow picked up on both", lambda: (
        find_arrow(A.dump.last_state(), nid) is None and
        find_arrow(B.dump.last_state(), nid) is None), timeout=60)
    run.wait("A inventory has ARROW", lambda: inv_count(A.dump.last_state(), "ARROW") >= 1, timeout=30)


def sc_arrow_mob_hit(run, A, B):
    """A viewer's arrow hitting an owner-side mob: the sim raycasts the
    mirror, ENTITY_ACTION lands the damage on the owner, the hurt state
    streams back."""
    gy = settle(run, A, B)
    B.cmd("mode survival")  # ARROW_DAMAGE (9) wounds the 10hp pig, no kill
    pre = {m["net_id"] for m in mobs_of(A.dump.last_state())}
    A.cmd("mob PIG 14.5 %d 10.5" % gy)
    run.wait("pig on A", lambda: any(
        m["net_id"] not in pre for m in mobs_of(A.dump.last_state())), timeout=60)
    nid = next(m["net_id"] for m in mobs_of(A.dump.last_state()) if m["net_id"] not in pre)
    run.wait("pig on B", lambda: find_mob(B.dump.last_state(), nid) is not None, timeout=60)
    h0 = find_mob(A.dump.last_state(), nid)["health"]

    def pig_hurt():
        ma = find_mob(A.dump.last_state(), nid)
        mb = find_mob(B.dump.last_state(), nid)
        return ma is not None and mb is not None and ma["health"] < h0 and mb["health"] < h0

    # Pigs wander — re-aim at the streamed position each attempt.
    for _ in range(5):
        m = find_mob(B.dump.last_state(), nid)
        if m is None:
            break
        B.cmd("shoot %f %f %f -1 0.02 0" % (m["pos"][0] + 4.0, m["pos"][1] + 0.5, m["pos"][2]))
        try:
            run.wait("pig hurt on both", pig_hurt, timeout=90)
            break
        except Fail:
            continue
    else:
        raise Fail("arrow never hit the pig")
    run.wait("arrow consumed", lambda: len(arrows_of(A.dump.last_state())) == 0 and
             len(arrows_of(B.dump.last_state())) == 0, timeout=60)


def sc_arrow_handover(run, A, B):
    """Sim disconnect: the stuck arrow's chunk owner adopts it via
    ENTITY_TRANSFER; a late joiner mirrors the parked state."""
    gy = settle(run, A, B)
    for y in range(gy, gy + 3):
        A.cmd("setblock 20 %d 10 STONE" % y)
    run.sleep_ticks(10)
    A.cmd("shoot 14.5 %f 10.5 1 0 0" % (gy + 1.0))
    run.wait("arrow stuck on both", lambda: (
        (find_arrow(A.dump.last_state()) or {}).get("stuck") == 1 and
        (find_arrow(B.dump.last_state()) or {}).get("stuck") == 1), timeout=120)
    nid = find_arrow(A.dump.last_state())["net_id"]
    A.quit()
    run.wait("B adopts arrow", lambda: (
        find_arrow(B.dump.last_state(), nid) is not None and
        find_arrow(B.dump.last_state(), nid)["remote"] == 0), timeout=90)
    b = find_arrow(B.dump.last_state(), nid)
    if b["stuck"] != 1:
        raise Fail("adopted arrow lost stuck state: %s" % (b,))
    # A late joiner mirrors the same parked arrow.
    C = run.client("C", POS_C)
    run.wait_ready(C)
    run.wait("C mirrors arrow", lambda: (
        find_arrow(C.dump.last_state(), nid) is not None and
        find_arrow(C.dump.last_state(), nid)["remote"] == 1 and
        find_arrow(C.dump.last_state(), nid)["stuck"] == 1), timeout=120)


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


def sc_natural_persist(run, A, B):
    NATURAL = 1 << 9

    def naturals(c):
        return [m for m in mobs_of(c.dump.last_state()) if m["net_flags"] & NATURAL]

    def mobs_files():
        d = os.path.join(run.dir, "server_home", ".repgame", "Server1")
        return sorted(f for f in os.listdir(d) if f.startswith("mobs_")) if os.path.isdir(d) else []

    def natural_home(nid):
        v = int(nid)

        def se(bits, n):
            bits &= (1 << n) - 1
            m = 1 << (n - 1)
            return (bits ^ m) - m
        return (se(v >> 35, 21), se(v >> 6, 8), se(v >> 14, 21))

    # No settle(): its stone pads are player edits, and any natural mob
    # ticking in a player-edited chunk is legitimately pen-rule TOUCHED —
    # so pristine persistence can only be asserted where nothing was ever
    # edited. Pin the cameras over untouched terrain and take settle()'s
    # coverage wait instead.
    A.goto(10.5, 101.7, 10.5)
    B.goto(260.5, 101.7, 10.5)
    full = (2 * 6 + 1) * (2 * 3 + 1) * (2 * 6 + 1)
    run.wait("full role coverage", lambda: all(
        c.dump.last_state() is not None and
        sum(c.dump.last_state()["roles"].values()) >= full
        for c in (A, B)), timeout=300)
    run.sleep_ticks(80)
    run.wait("natural mobs on A", lambda: len(naturals(A)) > 0, timeout=240)
    # Pristine naturals are recreated by the deterministic roll — last
    # client disconnect flushes every covered chunk and must leave no
    # mobs_ file behind.
    for c in list(run.clients.values()):
        c.quit()
    run.sleep_ticks(60)
    time.sleep(2)
    if mobs_files():
        raise Fail("pristine naturals left mobs_ files: %s" % mobs_files()[:8])

    # Rejoin and hit a natural mob: the hit marks it TOUCHED, so its home
    # chunk must gain a mobs_ file (record + tombstoned roll index) once
    # the client disconnects and the server flushes.
    C = run.client("C", POS_A)
    run.wait_ready(C)
    run.wait("natural mobs on C", lambda: len(naturals(C)) > 0, timeout=240)
    victim = naturals(C)[0]
    nid = victim["net_id"]
    hx, hy, hz = natural_home(nid)
    C.cmd("mode survival")  # a real 4-damage hit, not the creative insta-kill
    C.cmd("hit %s" % nid)
    run.sleep_ticks(40)  # ENTITY_STATE carries TOUCHED to the server
    C.quit()
    want = "mobs_%d_%d_%d" % (hx, hy, hz)
    run.wait("mobs file for home chunk", lambda: want in mobs_files(), timeout=30)


def sc_containers(run, A, B):
    """Unified CONTAINER_* sync: viewers see each other's slot writes with a
    bumped revision, a write to a container the client never opened is
    corrected by the authoritative snapshot, breaking the chest loots the
    breaker, the furnace ticks server-side (LIT_FURNACE swap + progress
    aux), and contents persist across a server restart."""
    gy = settle(run, A, B)
    cx, cy, cz = 135, gy, 10  # chunk 2 — inside both clients' view radii

    A.cmd("setblock %d %d %d STONE" % (cx, cy - 1, cz))
    A.cmd("setblock %d %d %d CHEST" % (cx, cy, cz))
    wait_block(run, A, cx, cy, cz, "CHEST")
    wait_block(run, B, cx, cy, cz, "CHEST")

    # Both subscribe; each gets the authoritative CONTENTS snapshot.
    A.cmd("copen chest %d %d %d" % (cx, cy, cz))
    B.cmd("copen chest %d %d %d" % (cx, cy, cz))
    run.sleep_ticks(20)

    # A's optimistic write lands locally and relays to B (rev bumped).
    A.cmd("cset chest %d %d %d 3 STONE 7" % (cx, cy, cz))
    wait_cslot(run, A, "chest", (cx, cy, cz), 3, "STONE", 7, desc="A optimistic write")
    db = wait_cslot(run, B, "chest", (cx, cy, cz), 3, "STONE", 7, desc="B relayed write")
    if db.get("rev", 0) < 1:
        raise Fail("B saw no revision bump: %s" % db)

    # B edits a different slot; A sees it through the same relay.
    B.cmd("cset chest %d %d %d 5 GOLD_BLOCK 3" % (cx, cy, cz))
    wait_cslot(run, A, "chest", (cx, cy, cz), 5, "GOLD_BLOCK", 3, desc="A sees B's write")

    # Unauthorized write: C writes to a chest it never opened — the server
    # rejects it and pushes the authoritative snapshot; C's optimistic copy
    # self-heals back to empty.
    fx, fy, fz = 135, gy, 12
    A.cmd("setblock %d %d %d STONE" % (fx, fy - 1, fz))
    A.cmd("setblock %d %d %d CHEST" % (fx, fy, fz))
    C = run.client("C", (140.0, 90.0, 12.0))
    run.wait_ready(C)
    wait_block(run, C, fx, fy, fz, "CHEST")
    C.cmd("cset chest %d %d %d 0 GOLD_BLOCK 99" % (fx, fy, fz))
    wait_cslot(run, C, "chest", (fx, fy, fz), 0, None, desc="unauthorized write corrected")

    # Loot-on-break: A breaks the chest; the CONTENTS loot snapshot credits
    # the breaker through the pending_container_loot marker.
    before = inv_count(A.dump.last_state(), "STONE")
    A.cmd("setblock %d %d %d AIR" % (cx, cy, cz))
    run.wait("loot snapshot credited",
             lambda: inv_count(A.dump.last_state(), "STONE") >= before + 7, timeout=60)

    # Furnace: place, open, load a smelt — the server ticks it, swaps the
    # block to LIT_FURNACE for everyone, and streams progress aux to viewers.
    A.cmd("setblock %d %d %d FURNACE" % (cx, cy, cz))
    wait_block(run, A, cx, cy, cz, "FURNACE")
    A.cmd("copen furnace %d %d %d" % (cx, cy, cz))
    A.cmd("cset furnace %d %d %d 0 RAW_IRON 1" % (cx, cy, cz))  # input
    A.cmd("cset furnace %d %d %d 1 COAL 1" % (cx, cy, cz))      # fuel
    wait_block(run, A, cx, cy, cz, "LIT_FURNACE", timeout=90)
    wait_block(run, B, cx, cy, cz, "LIT_FURNACE", timeout=60)
    df = wait_cslot(run, A, "furnace", (cx, cy, cz), 0, "RAW_IRON", 1, desc="burning input")
    if not df.get("aux") or df["aux"][1] <= 0:
        raise Fail("furnace not burning on client: aux=%s" % df.get("aux"))

    # Persistence: a server restart flushes the sidecars; a fresh client
    # reopening the furnace sees it still burning.
    for c in list(run.clients.values()):
        c.quit()
    run.sleep_ticks(10)
    run.restart_server()
    D = run.client("D", (140.0, 90.0, 10.0))
    run.wait_ready(D)
    wait_probe(run, D, cx, cy, cz)
    D.cmd("copen furnace %d %d %d" % (cx, cy, cz))
    dd = wait_cslot(run, D, "furnace", (cx, cy, cz), 0, "RAW_IRON", 1, desc="persisted input")
    if not dd.get("aux") or dd["aux"][1] <= 0:
        raise Fail("persisted furnace lost its burn: aux=%s" % dd.get("aux"))


def sc_sign_text(run, A, B):
    """Sign front text: an editor commit on one client relays through the
    server to nearby viewers and to late joiners via the chunk-request
    sync; breaking the sign clears the record everywhere."""
    gy = settle(run, A, B)
    sx, sy, sz = 135, gy, 10  # chunk 2 — inside both clients' view radii

    A.cmd("setblock %d %d %d STONE" % (sx, sy - 1, sz))
    A.cmd("setblock %d %d %d STANDING_SIGN" % (sx, sy, sz))
    wait_block(run, A, sx, sy, sz, "STANDING_SIGN")
    wait_block(run, B, sx, sy, sz, "STANDING_SIGN")

    # A edits through the real open/type/commit path; the server accepts,
    # stores, and broadcasts — B applies the echo (no record until then).
    A.cmd("signedit %d %d %d HELLO WORLD|FROM CLIENT A" % (sx, sy, sz))
    wait_sign(run, A, (sx, sy, sz), "HELLO WORLD|FROM CLIENT A",
              desc="A committed text")
    wait_sign(run, B, (sx, sy, sz), "HELLO WORLD|FROM CLIENT A",
              desc="B sees the broadcast")

    # A late joiner gets the stored text with its chunk-request sync.
    C = run.client("C", (140.0, 90.0, 10.0))
    run.wait_ready(C)
    wait_block(run, C, sx, sy, sz, "STANDING_SIGN")
    wait_sign(run, C, (sx, sy, sz), "HELLO WORLD|FROM CLIENT A",
              desc="late joiner gets stored text")

    # Editing again overwrites on every client.
    B.cmd("signedit %d %d %d SECOND|EDIT|BY B" % (sx, sy, sz))
    wait_sign(run, A, (sx, sy, sz), "SECOND|EDIT|BY B", desc="A sees B's edit")

    # Breaking the sign clears the record on other clients too.
    A.cmd("setblock %d %d %d AIR" % (sx, sy, sz))
    wait_block(run, B, sx, sy, sz, "AIR")
    wait_sign(run, B, (sx, sy, sz), "", desc="break clears text")


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


def avatar_mobs_of(st):
    """Remote-player avatars (uid != 0) — the inverse of mobs_of()."""
    return [m for m in (st.get("mobs", []) if st else []) if m.get("uid", 0) != 0]


def cmd_health(run, c, cmd, event_name, timeout=30):
    """Send a health command until its *_result event reports applied=1 —
    the shared i-frame window (72 ticks) can refuse a `damage` that lands
    too soon after the previous hit, so retry with a >0.5s gap."""
    deadline = time.time() + timeout
    n0 = len(c.dump.events)
    while time.time() < deadline:
        c.cmd(cmd)
        time.sleep(0.7)
        c.dump.poll()
        hits = [e for e in c.dump.events[n0:]
                if e.get("name") == event_name and e.get("detail", {}).get("applied") == 1]
        if hits:
            return hits[-1]["detail"]
    raise Fail("%s never applied on %s" % (cmd, c.name))


def sc_health(run, A, B):
    """Player health: the shared damage funnel, instant food healing,
    fire at the feet cell, death + respawn, and the remote avatar hurt
    flash a health drop triggers on the peer."""
    gy = settle(run, A, B)
    A.cmd("mode survival")
    A.cmd("peaceful off")  # deterministic health — no regen (no mobs spawned)
    run.wait("A at full health",
             lambda: (A.dump.last_state() or {}).get("health") == 20.0, timeout=30)

    # Shared damage funnel (survival only — gated by i-frames).
    cmd_health(run, A, "damage 6", "damage_result")
    run.wait("A damaged", lambda: (A.dump.last_state() or {}).get("health") == 14.0, timeout=30)

    # The drop rides the player-location payload: B's avatar for A flashes.
    # The flash lives ~0.3s, so scan the state history — the check starts
    # after the applied-wait, by which time the window may have passed.
    run.wait("B saw avatar hurt flash",
             lambda: any(m.get("hurt_ticks", 0) > 0
                         for st in B.dump.states for m in avatar_mobs_of(st)),
             timeout=60)

    # Instant eat (beta-style): apple heals 4 half-hearts, one is consumed.
    A.cmd("give APPLE 4")
    run.wait("apples held", lambda: inv_count(A.dump.last_state(), "APPLE") >= 4, timeout=30)
    A.cmd("eat")
    run.wait("eat applied", lambda: any(
        e.get("name") == "eat_result" and e.get("detail", {}).get("applied") == 1
        for e in A.dump.events), timeout=30)
    run.wait("eat healed + consumed",
             lambda: (A.dump.last_state() or {}).get("health") == 18.0 and
                     inv_count(A.dump.last_state(), "APPLE") == 3, timeout=30)

    # Environmental damage: fire in the feet cell burns per i-frame window.
    A.cmd("setblock 10 %d 10 FIRE" % gy)
    run.wait("fire burns A",
             lambda: 0.0 < (A.dump.last_state() or {}).get("health", 20.0) < 18.0, timeout=60)
    A.cmd("setblock 10 %d 10 AIR" % gy)

    # Death: health pins at 0, dead raises the (locked) death screen.
    cmd_health(run, A, "damage 40", "damage_result")
    run.wait("A dead", lambda: (A.dump.last_state() or {}).get("dead") == 1 and
             (A.dump.last_state() or {}).get("health") == 0.0, timeout=30)

    # Respawn clears the death state with full vitals. (The REPGAME_FLY pin
    # re-asserts the camera every tick, so the spawn teleport itself isn't
    # observable here — rep_tests covers repgame_respawn's position reset.)
    A.cmd("respawn")
    run.wait("respawn applied", lambda: A.dump.has_event("respawn_result"), timeout=30)
    run.wait("A alive again", lambda: (A.dump.last_state() or {}).get("health") == 20.0 and
             (A.dump.last_state() or {}).get("dead") == 0, timeout=30)


def sc_avatar_at_mob_cap(run, A, B):
    """A full mob list must not drop a late joiner's avatar (MAX_MOBS gates
    locally created mobs, not storage)."""
    gy = settle(run, A, B)
    x, z = int(A.pos[0]), int(A.pos[2])
    for i in range(270):
        A.cmd("mob pig %d %d %d" % (x + (i % 20) - 10, gy, z + (i // 20) - 7))
    run.wait("A at mob cap",
             lambda: len((A.dump.last_state() or {}).get("mobs", [])) >= 256, timeout=120, period=0.2)
    # Late joiner near A's pad (inside the shared view band).
    C = run.client("C", (30.0, 90.0, 10.0))
    run.wait_ready(C)
    run.sleep_ticks(60)

    def avatar_near(c, pos, tol=15.0):
        return any(dist(m["pos"], pos) < tol for m in avatar_mobs_of(c.dump.last_state()))

    run.wait("A sees C's avatar", lambda: avatar_near(A, C.pos), timeout=60)
    run.wait("C sees A's avatar", lambda: avatar_near(C, A.pos), timeout=60)


SCENARIOS = [
    ("item_mirror_visuals", sc_item_mirror_visuals),
    ("item_pickup", sc_item_pickup),
    ("pickup_race", sc_pickup_race),
    ("item_transfer", sc_item_transfer),
    ("boundary_rest", sc_boundary_rest),
    ("ownership_flip_no_dup", sc_ownership_flip_no_dup),
    ("natural_mobs", sc_natural_mobs),
    ("natural_persist", sc_natural_persist),
    ("mob_hit_kill", sc_mob_hit_kill),
    ("hostile_targets_remote", sc_hostile_targets_remote),
    ("health", sc_health),
    ("falling_block", sc_falling_block),
    ("tnt_explosion", sc_tnt_explosion),
    ("tnt_handover", sc_tnt_handover),
    ("arrow_shoot", sc_arrow_shoot),
    ("arrow_mob_hit", sc_arrow_mob_hit),
    ("arrow_handover", sc_arrow_handover),
    ("persistence", sc_persistence),
    ("disconnect_cleanup", sc_disconnect_cleanup),
    ("first_view_correct", sc_first_view_correct),
    ("avatar_at_mob_cap", sc_avatar_at_mob_cap),
    ("containers", sc_containers),
    ("sign_text", sc_sign_text),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", dest="filt", default=None, help="run scenarios matching substring")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--keep", action="store_true", help="keep all artifact dirs")
    args = ap.parse_args()

    if args.list:
        for name, _ in SCENARIOS:
            print(name)
        return 0
    for p in (SERVER, CLIENT):
        if not os.path.exists(p):
            print("missing binary: %s (run make linux server first, or make mp-test)" % p)
            return 2

    selected = [(n, f) for n, f in SCENARIOS if not args.filt or args.filt in n]
    if args.filt and not selected:
        print("no scenarios match -k %r" % args.filt)
        return 2

    fails = []
    for name, fn in selected:
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
