"""Sanitizer env + report collection for rep-dbg --build asan|tsan.

The sanitizers write `log_path`-prefixed files (asan.N, tsan.N, ubsan.N)
inside the run dir; we count and summarize them after the run rather than
scraping stdout. Suppression files live in tools/sanitizers/.
"""

import glob
import os
import re

SUPP_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "sanitizers")


def sanitizer_env(build, run_dir):
    """Env vars for a sanitizer run into `run_dir`. Empty dict otherwise."""
    if build == "asan":
        return {
            # use_sigaltstack=0: without it, worker-thread teardown hits an
            # ASan-internal CHECK (UnsetAlternateSignalStack munmap EINVAL)
            # on GL/driver threads — teardown noise that kills the process.
            "ASAN_OPTIONS": "log_path=%s/asan:detect_leaks=1:abort_on_error=0:halt_on_error=0:use_sigaltstack=0" % run_dir,
            "LSAN_OPTIONS": "suppressions=%s" % os.path.join(SUPP_DIR, "lsan.supp"),
            "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=0:log_path=%s/ubsan" % run_dir,
        }
    if build == "tsan":
        return {
            "TSAN_OPTIONS": "log_path=%s/tsan:suppressions=%s:second_deadlock_stack=1"
                            % (run_dir, os.path.join(SUPP_DIR, "tsan.supp")),
        }
    return {}


REPORT_KIND = [
    ("asan_errors", re.compile(r"ERROR: AddressSanitizer")),
    ("ubsan_errors", re.compile(r"runtime error:")),
    ("tsan_warnings", re.compile(r"WARNING: ThreadSanitizer")),
    ("leaks", re.compile(r"ERROR: LeakSanitizer")),
]

# First stack line that points into the repo's sources. Absolute paths must
# live under the repo root; relative paths must have whitespace (not a '/')
# before "src/"/"include/" so /usr/src/mesa-*/... doesn't count.
_REPO_ABS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_REPO_FRAME = re.compile(
    r"#\d+\s+.*(?:%s/|\s)(?:src|include)/[\w./-]+:\d+" % re.escape(_REPO_ABS))


def parse_report_file(path):
    """One report file -> {kind, summary_line, first_repgame_frame} entries.

    A file may hold multiple reports (UBSan logs one per line); each
    `ERROR:`/`WARNING:`/`runtime error:` marker starts a new entry.
    """
    entries = []
    kind = None
    summary = None
    frame = None
    try:
        with open(path, "r", errors="replace") as f:
            for line in f:
                line = line.rstrip("\n")
                for name, pat in REPORT_KIND:
                    if pat.search(line):
                        if kind is not None:
                            entries.append((kind, summary, frame))
                        kind, summary, frame = name, line.strip(), None
                        break
                else:
                    if kind is not None and frame is None and _REPO_FRAME.search(line):
                        frame = line.strip()
    except OSError:
        return []
    if kind is not None:
        entries.append((kind, summary, frame))
    return entries


def scan_run_dir(run_dir):
    """Collect sanitizer report files under run_dir -> (counts, top, files)."""
    files = sorted(glob.glob(os.path.join(run_dir, "asan.*")) +
                   glob.glob(os.path.join(run_dir, "tsan.*")) +
                   glob.glob(os.path.join(run_dir, "ubsan.*")))
    counts = {name: 0 for name, _ in REPORT_KIND}
    top = []
    for path in files:
        for kind, summary, frame in parse_report_file(path):
            counts[kind] += 1
            top.append({"kind": kind, "summary_line": summary,
                        "first_repgame_frame": frame})
    return counts, top, [os.path.basename(f) for f in files]


def postprocess(run_dir, result, log):
    """Fill result.json.sanitizer + artifacts.sanitizer; fail on findings."""
    counts, top, files = scan_run_dir(run_dir)
    if not files:
        return
    result["sanitizer"] = dict(counts)
    result["sanitizer"]["top"] = top[:20]
    art = result.setdefault("artifacts", {})
    art["sanitizer"] = files
    total = sum(counts.values())
    print("rep-dbg: sanitizer reports: %d (%s) in %s"
          % (total, ", ".join("%s=%d" % kv for kv in counts.items() if kv[1]), run_dir),
          file=log)
    if total and result.get("status") == "ok":
        result["status"] = "failed"
