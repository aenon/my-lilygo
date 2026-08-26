#!/usr/bin/env python3
"""Generate 240x240 square pre-rotated thumbnails from full-size source PNGs.

Takes images from data/images/ (540x960 PNGs), center-crops to square,
resizes to 240x240, and packs to 4bpp raw with pre-rotation for
EPD_ROT_INVERTED_PORTRAIT.

Output: data/images/thumb_<name>.epd (28,800 bytes each)

Usage:
    python3 scripts/gen-thumbnails.py
"""

import os
from pathlib import Path
from PIL import Image

THUMB_W = 240
THUMB_H = 240
# Physical layout for EPD_ROT_INVERTED_PORTRAIT: logical (lx,ly) -> physical (ly, 239-lx)
# For a square 240x240 thumbnail, the physical block is also 240x240.

def make_thumbnail(src_png: str, out_epd: str) -> None:
    img = Image.open(src_png).convert("L")
    # Center-crop to square
    w, h = img.size
    s = min(w, h)
    left = (w - s) // 2
    top = (h - s) // 2
    img = img.crop((left, top, left + s, top + s))
    img = img.resize((THUMB_W, THUMB_H), Image.LANCZOS)

    px = img.load()
    fb_w = THUMB_W // 2 + THUMB_W % 2  # 120
    buf = bytearray(fb_w * THUMB_H)

    for ly in range(THUMB_H):
        for lx in range(THUMB_W):
            phys_x = ly
            phys_y = (THUMB_W - 1) - lx
            val = px[lx, ly] >> 4  # 8-bit -> 4-bit
            idx = phys_y * fb_w + phys_x // 2
            if phys_x % 2 == 0:
                buf[idx] = (buf[idx] & 0xF0) | (val & 0x0F)
            else:
                buf[idx] = (buf[idx] & 0x0F) | ((val & 0x0F) << 4)

    with open(out_epd, "wb") as f:
        f.write(buf)
    print(f"  {os.path.basename(out_epd)}: {len(buf)} bytes")


def main():
    base = Path("data/images")
    if not base.is_dir():
        print(f"Error: {base} not found.  Run from the project root.")
        return

    count = 0
    for f in sorted(base.iterdir()):
        if f.suffix != ".png":
            continue
        name = f.stem
        out = base / f"thumb_{name}.epd"
        make_thumbnail(str(f), str(out))
        count += 1

    print(f"Generated {count} thumbnails")


if __name__ == "__main__":
    main()
