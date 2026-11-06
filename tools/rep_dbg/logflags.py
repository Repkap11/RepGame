"""Limited stdout.log scraping: signals that aren't events by nature
(sanitizer reports, GL error lines, aborts, capture timeouts). The only
log-text parsing in rep-dbg besides the logparse.py old-rev shim."""

import re

PATTERNS = {
    "asan_errors": re.compile(r"(Address|Leak|Memory)Sanitizer"),
    "ubsan_errors": re.compile(r"runtime error:"),
    "gl_errors": re.compile(r"GL Error"),
    "capture_timeouts": re.compile(r"TIMEOUT — loader still busy"),
    "aborts": re.compile(r"Aborted|assertion .* failed", re.IGNORECASE),
}


def scan_lines(lines):
    flags = {name: 0 for name in PATTERNS}
    for line in lines:
        for name, pat in PATTERNS.items():
            if pat.search(line):
                flags[name] += 1
    return flags


def scan_file(path):
    try:
        with open(path, "r", errors="replace") as f:
            return scan_lines(f)
    except OSError:
        return {name: 0 for name in PATTERNS}
