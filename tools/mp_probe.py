#!/usr/bin/env python3
"""Multiplayer load-in probe: connects as a RepGame client, fires one
CHUNK_DIFF_REQUEST box (optionally a VIEW right after) and reports when
the server's CHUNK_DIFF_RESULT / CHUNK_ROLE frames arrive.

Usage: mp_probe.py <host> <port> [--box minx,miny,minz,sx,sy,sz]
       [--view cx,cy,cz,rxz,ry] [--repeat N] [--duration S]
       [--version N] [--json]

Text output prints one line per frame type (count + first/last arrival
ms relative to the request send). --json prints one JSON object per
repeat instead. Stdlib only; no game imports.
"""

import argparse
import json
import os
import re
import socket
import struct
import sys
import time
import zlib

MAGIC = 0x52475054  # 'TPGR' wire magic (see src/common/net/framed_socket.cpp)
CHUNK_DIFF_REQUEST = 5
CHUNK_DIFF_RESULT = 6
WORLD_TIME = 11
WELCOME = 12
VIEW = 13
CHUNK_ROLE = 14

TYPE_NAMES = {
    CHUNK_DIFF_REQUEST: "CHUNK_DIFF_REQUEST",
    CHUNK_DIFF_RESULT: "CHUNK_DIFF_RESULT",
    WORLD_TIME: "WORLD_TIME",
    WELCOME: "WELCOME",
    VIEW: "VIEW",
    CHUNK_ROLE: "CHUNK_ROLE",
}
# Frame types timed in the report (everything else still counts in bytes).
TIMED_TYPES = (CHUNK_DIFF_RESULT, CHUNK_ROLE)

PACKET_HPP = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "include", "common", "net", "packet.hpp")


def protocol_version(override):
    if override is not None:
        return override
    with open(PACKET_HPP) as f:
        m = re.search(r"NET_PROTOCOL_VERSION\s*=\s*(\d+)", f.read())
    if not m:
        sys.exit("could not parse NET_PROTOCOL_VERSION from %s" % PACKET_HPP)
    return int(m.group(1))


def frame(version, msg_type, payload=b""):
    body = struct.pack("<BBi", version, msg_type, 0) + payload
    rest = struct.pack("<I", len(body)) + body
    return struct.pack("<II", MAGIC, zlib.crc32(rest) & 0xFFFFFFFF) + rest


def probe_once(host, port, version, box, view, duration):
    result = {"connect_ms": None, "bytes": 0}
    for t in TIMED_TYPES:
        name = TYPE_NAMES[t]
        result[name] = {"count": 0, "first_ms": None, "last_ms": None}
    t_conn = time.monotonic()
    try:
        s = socket.create_connection((host, port), timeout=10)
    except OSError as e:
        result["error"] = str(e)
        return result
    result["connect_ms"] = round(1000 * (time.monotonic() - t_conn), 1)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    t_req = time.monotonic()
    s.sendall(frame(version, CHUNK_DIFF_REQUEST, struct.pack("<iiiBBB", *box)))
    if view is not None:
        s.sendall(frame(version, VIEW, struct.pack("<iiiBB", *view)))
    s.settimeout(0.2)
    buf = b""
    while time.monotonic() - t_req < duration:
        try:
            data = s.recv(1 << 20)
        except socket.timeout:
            continue
        if not data:
            result["server_closed"] = True
            break
        now_ms = 1000 * (time.monotonic() - t_req)
        result["bytes"] += len(data)
        buf += data
        while len(buf) >= 12:
            plen = struct.unpack_from("<I", buf, 8)[0]
            if len(buf) < 12 + plen:
                break
            mtype = buf[13]
            buf = buf[12 + plen:]
            if mtype in TIMED_TYPES:
                entry = result[TYPE_NAMES[mtype]]
                entry["count"] += 1
                if entry["first_ms"] is None:
                    entry["first_ms"] = round(now_ms, 1)
                entry["last_ms"] = round(now_ms, 1)
    s.close()
    return result


def parse_ints(text, count, name):
    vals = [int(v) for v in text.split(",")]
    if len(vals) != count:
        sys.exit("--%s needs %d comma-separated ints, got %r" % (name, count, text))
    return vals


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("port", type=int)
    ap.add_argument("--box", default="-9,-4,-8,17,9,17",
                    help="CHUNK_DIFF_REQUEST box: minx,miny,minz,sx,sy,sz")
    ap.add_argument("--view", default=None,
                    help="send a VIEW frame after the request: cx,cy,cz,rxz,ry")
    ap.add_argument("--repeat", type=int, default=1, help="fresh connection per repeat")
    ap.add_argument("--duration", type=float, default=6.0,
                    help="seconds to collect frames per repeat")
    ap.add_argument("--version", type=int, default=None,
                    help="protocol version override (default: parsed from packet.hpp)")
    ap.add_argument("--json", action="store_true",
                    help="one JSON object per repeat on stdout")
    args = ap.parse_args()

    version = protocol_version(args.version)
    box = parse_ints(args.box, 6, "box")
    view = parse_ints(args.view, 5, "view") if args.view else None

    for i in range(args.repeat):
        r = probe_once(args.host, args.port, version, box, view, args.duration)
        r["repeat"] = i
        if args.json:
            print(json.dumps(r))
            continue
        print("repeat %d: connect_ms=%s bytes=%d" % (i, r["connect_ms"], r["bytes"]))
        if "error" in r:
            print("  error: %s" % r["error"])
            break
        names = sorted((TYPE_NAMES[t] for t in TIMED_TYPES),
                       key=lambda n: r[n]["first_ms"] if r[n]["first_ms"] is not None else 1e18)
        for name in names:
            e = r[name]
            print("  %-20s n=%-5d first=%sms last=%sms" % (name, e["count"], e["first_ms"], e["last_ms"]))


if __name__ == "__main__":
    main()
