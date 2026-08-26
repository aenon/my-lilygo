#!/usr/bin/env python3
"""Convert images to 4bpp raw e-paper format for the LilyGo T5 E-Paper S3 Pro.

Takes a source image (SVG via Inkscape, or PNG/JPG), resizes it to the target
dimensions, converts to 16-grayscale, and packs into 4bpp raw format (2 pixels
per byte, left pixel in low nibble, right pixel in high nibble) matching the
epdiy framebuffer convention used by epd_get_pixel / epd_draw_rotated_image.

Usage:
    python3 scripts/svg-to-epaper.py input.svg output.epd
    python3 scripts/svg-to-epaper.py input.png output.epd --width 540 --height 960

Defaults to 540x960 (portrait, USB-C at bottom) for the toy phone.
"""

import argparse
import subprocess
import sys
import tempfile
import os
from pathlib import Path

from PIL import Image


def render_svg(svg_path: str, width: int, height: int, out_png: str) -> None:
    subprocess.run(
        ["inkscape", svg_path, f"--export-filename={out_png}",
         f"--export-width={width}", f"--export-height={height}"],
        check=True,
    )


def convert_to_epd(src_path: str, out_path: str, width: int, height: int) -> None:
    # If SVG, render to PNG first
    if src_path.lower().endswith(".svg"):
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp:
            tmp_png = tmp.name
        render_svg(src_path, width, height, tmp_png)
        src = tmp_png
    else:
        src = src_path

    img = Image.open(src)
    img = img.convert("L")  # 8-bit grayscale

    # Resize to target dimensions (thumbnail preserves aspect ratio with padding;
    # here we want exact fit, so use resize)
    if img.size != (width, height):
        img = img.resize((width, height), Image.LANCZOS)

    # Quantize to 16 levels (4-bit): map 0-255 to 0-15
    pixels = img.load()
    fb_width_bytes = width // 2 + width % 2
    buf = bytearray(fb_width_bytes * height)

    for y in range(height):
        for x in range(width):
            gray = pixels[x, y]
            val = gray >> 4  # 8-bit -> 4-bit (0-15)
            byte_idx = y * fb_width_bytes + x // 2
            if x % 2 == 0:
                # left pixel -> low nibble
                buf[byte_idx] = (buf[byte_idx] & 0xF0) | (val & 0x0F)
            else:
                # right pixel -> high nibble
                buf[byte_idx] = (buf[byte_idx] & 0x0F) | ((val & 0x0F) << 4)

    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(buf)

    print(f"  {out_path}: {len(buf)} bytes ({width}x{height} 4bpp)")

    # Cleanup temp PNG
    if src_path.lower().endswith(".svg") and os.path.exists(tmp_png):
        os.unlink(tmp_png)


def main():
    parser = argparse.ArgumentParser(description="Convert images to 4bpp e-paper format")
    parser.add_argument("input", help="Source image (SVG, PNG, JPG)")
    parser.add_argument("output", help="Output .epd file path")
    parser.add_argument("--width", type=int, default=540, help="Output width (default: 540 portrait)")
    parser.add_argument("--height", type=int, default=960, help="Output height (default: 960 portrait)")
    args = parser.parse_args()

    convert_to_epd(args.input, args.output, args.width, args.height)


if __name__ == "__main__":
    main()
