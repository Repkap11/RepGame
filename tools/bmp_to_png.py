#!/usr/bin/env python3
"""Convert a 32-bit BGRA BMP to the PNG that RepGame embeds in the binary.

Every channel byte is preserved exactly — including the rgb of fully
transparent texels, which the mip chain's fill_transparent_rgb reads at
texture load. If the alpha channel is uniformly 255 the PNG is written as
RGB; stb_image expands it back to RGBA at decode, so in-game pixels are
identical either way. The output is verified by decoding it back and
comparing pixels to the BMP source.

Usage: python3 tools/bmp_to_png.py <in.bmp> <out.png>
"""

import struct
import sys

from PIL import Image


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, "rb").read()
    if d[0:2] != b"BM":
        sys.exit(f"{src}: not a BMP")
    off = struct.unpack_from("<I", d, 10)[0]
    w = struct.unpack_from("<i", d, 18)[0]
    h = struct.unpack_from("<i", d, 22)[0]
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 32:
        sys.exit(f"{src}: expected 32 bpp, got {bpp}")

    # Rows are bottom-up when h > 0. Pixels are B,G,R,A in memory.
    bottom_up = h > 0
    h = abs(h)
    stride = w * 4
    rgba = bytearray(w * h * 4)
    for y in range(h):
        sy = (h - 1 - y) if bottom_up else y
        row = d[off + sy * stride : off + (sy + 1) * stride]
        out = memoryview(rgba)[y * stride : (y + 1) * stride]
        out[0::4] = row[2::4]  # R
        out[1::4] = row[1::4]  # G
        out[2::4] = row[0::4]  # B
        out[3::4] = row[3::4]  # A

    img = Image.frombytes("RGBA", (w, h), bytes(rgba))
    if not any(rgba[i] != 255 for i in range(3, len(rgba), 4)):
        img = img.convert("RGB")
    img.save(dst, optimize=True)

    back = Image.open(dst).convert("RGBA").tobytes()
    if back != bytes(rgba):
        sys.exit(f"{dst}: round-trip pixel mismatch")
    print(f"{src} -> {dst} ({len(d)} -> {len(open(dst,'rb').read())} bytes)")


if __name__ == "__main__":
    main()
