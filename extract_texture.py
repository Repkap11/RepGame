#!/usr/bin/env python3
"""Extract a single texture tile from bitmaps/textures.bmp by index.

The atlas is 384x832 with 16x16 tiles (24 across, 52 down). Tiles are numbered
left-to-right, top-to-bottom starting at 0. The supplied index is a BlockID
enum value (block_definitions.hpp): the game subtracts 1 when meshing
(chunk.cpp), so enum N -> atlas tile N-1. Index 0 (AIR) has no texture.

Usage:
    python3 extract_texture.py <index> [--scale N] [--no-open]
"""

import argparse
import os
import subprocess
import sys
import tempfile

from PIL import Image

TILE_SIZE = 16
ATLAS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bitmaps", "textures.bmp")


def main():
    parser = argparse.ArgumentParser(description="Crop a texture tile from the RepGame atlas.")
    parser.add_argument("index", type=int, help="BlockID enum value (1 = GRASS); 0/AIR has no texture")
    parser.add_argument("--scale", type=int, default=8,
                        help="Integer upscale factor for the saved image (default: 8)")
    parser.add_argument("--no-open", action="store_true", help="Don't open the image after saving")
    args = parser.parse_args()

    atlas = Image.open(ATLAS_PATH)
    tiles_across = atlas.width // TILE_SIZE
    tiles_down = atlas.height // TILE_SIZE
    tile_index = args.index - 1
    if not 0 <= tile_index < tiles_across * tiles_down:
        sys.exit(f"Index {args.index} out of range (1-{tiles_across * tiles_down}; 0 is AIR)")

    col = tile_index % tiles_across
    row = tile_index // tiles_across
    box = (col * TILE_SIZE, row * TILE_SIZE, (col + 1) * TILE_SIZE, (row + 1) * TILE_SIZE)
    tile = atlas.crop(box)
    if args.scale != 1:
        tile = tile.resize((tile.width * args.scale, tile.height * args.scale), Image.NEAREST)

    out_path = os.path.join(tempfile.gettempdir(), "repgame_texture.png")
    tile.save(out_path)
    print(f"Saved tile {tile_index} (row {row}, col {col}) -> {out_path}")

    if not args.no_open:
        subprocess.Popen(["xdg-open", out_path],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
