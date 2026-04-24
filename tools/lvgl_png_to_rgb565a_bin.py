#!/usr/bin/env python3
"""
PNG (RGBA) → LVGL v8.3 .bin: lv_img_header_t + TRUE_COLOR_ALPHA (16-bit + alpha per pixel).

16-bit + alpha layout (see LVGL docs / Images):
  Byte 0–1: RGB565 little-endian (same packing as tools/lvgl_png_to_rgb565_bin.py)
  Byte 2:   alpha (0 = transparent, 255 = opaque; maps from PNG A)

Input: PNG with transparency — transparent pixels keep alpha=0; RGB is still written.

Usage:
  python tools/lvgl_png_to_rgb565a_bin.py 120 120 in.png data/logo/art_dummy.bin
Requires: pillow
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError as e:
    print("Install Pillow: pip install pillow", file=sys.stderr)
    raise SystemExit(1) from e

# lv_img.h: must match firmware LVGL (8.x)
LV_IMG_CF_TRUE_COLOR_ALPHA = 5


def pack_img_header(w: int, h: int, cf: int = LV_IMG_CF_TRUE_COLOR_ALPHA) -> bytes:
    """Little-endian 4-byte lv_img_header_t (cf 5b, pad 3b, res 2b, w 11b, h 11b)."""
    if w > 2047 or h > 2047:
        raise ValueError("width/height must be <= 2047")
    val = cf | (w << 10) | (h << 21)
    return struct.pack("<I", val)


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def cover_crop_rgba(im: Image.Image, tw: int, th: int) -> Image.Image:
    im = im.convert("RGBA")
    iw, ih = im.size
    scale = max(tw / iw, th / ih)
    nw = max(1, int(round(iw * scale)))
    nh = max(1, int(round(ih * scale)))
    im = im.resize((nw, nh), Image.Resampling.LANCZOS)
    left = (nw - tw) // 2
    top = (nh - th) // 2
    return im.crop((left, top, left + tw, top + th))


def main() -> None:
    if len(sys.argv) != 5:
        print(
            "Usage: lvgl_png_to_rgb565a_bin.py <W> <H> <input.png> <output.bin>",
            file=sys.stderr,
        )
        raise SystemExit(2)
    tw = int(sys.argv[1])
    th = int(sys.argv[2])
    src = Path(sys.argv[3])
    dst = Path(sys.argv[4])
    dst.parent.mkdir(parents=True, exist_ok=True)

    im = Image.open(src)
    im = cover_crop_rgba(im, tw, th)
    pixels = im.load()
    out = bytearray()
    out += pack_img_header(tw, th)
    for y in range(th):
        for x in range(tw):
            r, g, b, a = pixels[x, y]
            u16 = rgb888_to_rgb565(r, g, b)
            out += struct.pack("<H", u16)
            out += struct.pack("B", a)
    dst.write_bytes(out)
    print(f"Wrote {dst} ({len(out)} bytes) TRUE_COLOR_ALPHA, from {src}")


if __name__ == "__main__":
    main()
