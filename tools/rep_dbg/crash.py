"""rep-dbg --gdb: wrap the game in batch gdb and triage crash.txt.

gdb runs the inferior with stdout/stderr redirected into the run dir's
stdout.log (`run > stdout.log`), while gdb's own output (signal notice +
`thread apply all bt full`) goes to crash.txt. On timeout the runner sends
SIGINT: in batch mode gdb interrupts the inferior and then executes the
queued bt command, so even a hung run leaves useful stacks.
"""

import os
import re

GDB_WRAP = [
    "gdb", "-q", "-batch", "-nx",
    "-ex", "set pagination off",
    "-ex", "handle SIGPIPE nostop noprint",
    # Inferior output -> stdout.log; gdb's own output -> proc stdout
    # (the runner points that at crash.txt).
    "-ex", "run > stdout.log",
    # The crashing thread first — `thread apply all` prints it last and
    # `bt full` of 30 threads can take tens of seconds to emit.
    "-ex", "bt full",
    "-ex", "thread apply all bt full",
    "-ex", "quit",
    "--args",
]

# gdb prints either "Program received signal X" or
# 'Thread N "name" received signal X'; ignore interrupt/terminate signals —
# a timeout also lands in crash.txt via SIGINT and must not look crashed.
_SIGNAL = re.compile(r"(?:Thread\s+(\d+)\s+)?\"?\w*\"?\s*received signal (SIG\w+)")
_FATAL_SIGNALS = {"SIGSEGV", "SIGABRT", "SIGBUS", "SIGFPE", "SIGILL", "SIGTRAP"}
_THREAD = re.compile(r"Thread\s+(\d+)")
# `#2 0x00007f.. in func(args) at file:line` — also `from file` / no file.
_FRAME = re.compile(r"#(\d+)\s+0x[0-9a-fA-F]+\s+in\s+(\S+?)\s*(?:\(.*?\))?\s*(?:(?:at|from)\s+(\S+))?$", re.M)
_EXITED = re.compile(r"exited normally|Inferior \d+ .* will be killed")


def gdb_wrap():
    return list(GDB_WRAP)


_SIGNAL_FRAME = re.compile(r"0x[0-9a-fA-F]+\s+in\s+(\S+?)\s*(?:\(.*?\))?\s*(?:(?:at|from)\s+(\S+))?$", re.M)


def _frame_key(func, loc):
    return "%s (%s)" % (func, loc or "?")


def parse_gdb_log(text):
    """crash.txt text -> {signal, thread, top_frames} | {"no_crash": True}."""
    m = None
    for cand in _SIGNAL.finditer(text):
        if cand.group(2) in _FATAL_SIGNALS:
            m = cand
            break
    if m is None:
        return {"no_crash": True}
    out = {"signal": m.group(2), "thread": None, "top_frames": []}
    if m.group(1):
        out["thread"] = int(m.group(1))
    # Right after the signal line gdb prints the faulting frame, then the
    # current (crashing) thread's `bt full` frames — collected until the
    # first `thread apply all` Thread header.
    pos = m.end()
    sf = _SIGNAL_FRAME.search(text, pos)
    if sf:
        out["top_frames"].append(_frame_key(sf.group(1), sf.group(2)))
        pos = sf.end()
    for line in text[pos:].splitlines():
        if _THREAD.search(line):
            break
        fm = _FRAME.search(line)
        if fm:
            out["top_frames"].append(_frame_key(fm.group(2), fm.group(3)))
        if len(out["top_frames"]) >= 8:
            break
    return out


def postprocess(run_dir, result, log):
    """Fill result.json.crash from crash.txt; keep status honest."""
    path = os.path.join(run_dir, "crash.txt")
    try:
        text = open(path, "r", errors="replace").read()
    except OSError:
        return
    crash = parse_gdb_log(text)
    result["crash"] = crash
    art = result.setdefault("artifacts", {})
    art["crash"] = "crash.txt"
    if crash.get("no_crash"):
        result["crash"] = {"signal": None, "note": "no_crash"}
    elif crash.get("signal"):
        # A fatal signal beats ok/timeout: the crash is the salient result.
        result["status"] = "crashed"
