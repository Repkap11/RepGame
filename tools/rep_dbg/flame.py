"""Folded stacks from `perf script` text, and a dependency-free flame-graph
SVG (rects + <title> tooltips, no JS)."""

import re
import xml.sax.saxutils as xu

_FRAME = re.compile(r"^\s+(?:[0-9a-fA-F]+\s+)?(\S.*?)\s*\((.*)\)\s*$")
_SAMPLE = re.compile(r"^(\S+)\s+(\d+)\s+(\[[^\]]*\])?\s*[\d.]+:\s+")


def collapse(script_text):
    """perf script -> ({stack: count}, {comm: {stack: count}}).

    perf script prints each sample's call chain leaf-first, so frames are
    reversed to get root;...;leaf."""
    stacks, by_thread = {}, {}
    comm, frames = None, []

    def flush():
        if frames:
            stack = ";".join(reversed(frames))
            stacks[stack] = stacks.get(stack, 0) + 1
            if comm is not None:
                t = by_thread.setdefault(comm, {})
                t[stack] = t.get(stack, 0) + 1
    for line in script_text.splitlines():
        if not line.strip():
            flush()
            comm, frames = None, []
            continue
        m = _FRAME.match(line)
        if m:
            frames.append(m.group(1))
            continue
        s = _SAMPLE.match(line)
        if s:
            flush()
            comm, frames = s.group(1), []
    flush()
    return stacks, by_thread


def _color(name):
    h = 0
    for ch in name:
        h = (h * 131 + ord(ch)) & 0xFFFFFFFF
    return "hsl(%d,60%%,65%%)" % (h % 360)


def write_svg(collapsed, path, title="flame", width=1200, row_h=16):
    """Icicle/flame SVG: x=position along the root, one row per depth."""
    tree = {"name": title, "value": 0, "children": {}}
    for stack, w in collapsed.items():
        node = tree
        node["value"] += w
        for part in stack.split(";"):
            node = node["children"].setdefault(part, {"name": part, "value": 0, "children": {}})
            node["value"] += w
    rects = []
    max_depth = [0]

    def walk(node, x, depth):
        max_depth[0] = max(max_depth[0], depth)
        rects.append((x, depth, node["value"], node["name"]))
        cx = x
        for child in sorted(node["children"].values(), key=lambda c: -c["value"]):
            walk(child, cx, depth + 1)
            cx += child["value"]

    walk(tree, 0.0, 0)
    total = tree["value"] or 1
    height = (max_depth[0] + 2) * row_h + 30
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d">' % (width, height),
           '<text x="10" y="16" font-size="12" font-family="sans-serif">%s (%d samples)</text>'
           % (xu.escape(title), tree["value"])]
    for x, depth, val, name in rects:
        px = 10 + x / total * (width - 20)
        pw = val / total * (width - 20)
        if pw < 0.5:
            continue
        label = xu.escape(name)
        out.append('<rect x="%.2f" y="%.1f" width="%.2f" height="%d" fill="%s" '
                   'stroke="rgb(0,0,0)" stroke-width="0.5">'
                   '<title>%s — %d samples (%.1f%%)</title></rect>'
                   % (px, 24 + depth * row_h, pw, row_h - 1, _color(name),
                      label, val, val / total * 100))
        if pw > 40:
            out.append('<text x="%.2f" y="%.1f" font-size="10" font-family="monospace">%s</text>'
                       % (px + 2, 24 + depth * row_h + row_h - 5,
                          label[: int(pw / 6)]))
    out.append("</svg>")
    with open(path, "w") as f:
        f.write("\n".join(out))
    return path
