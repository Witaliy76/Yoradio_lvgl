#!/usr/bin/env python3
"""
Offline helper: PNG/JPEG → LVGL v8.3 .bin (lv_img_header_t + RGB565 LE pixels).
Cover + center crop to WxH. For provisioning data/bg/*.bin (Stage 6.1F-b).
For PNG with transparency (Left Art, logos), use lvgl_png_to_rgb565a_bin.py instead.

Usage:
  python tools/lvgl_png_to_rgb565_bin.py 480 480 bg/main_dark.png data/bg/main_dark.bin
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

# LVGL 8.3: LV_IMG_CF_TRUE_COLOR (16 bpp RGB565)
LV_IMG_CF_TRUE_COLOR = 4


def pack_img_header(w: int, h: int, cf: int = LV_IMG_CF_TRUE_COLOR) -> bytes:
    """Little-endian 4-byte lv_img_header_t (cf 5b, pad 3b, res 2b, w 11b, h 11b)."""
    if w > 2047 or h > 2047:
        raise ValueError("width/height must be <= 2047")
    val = cf | (w << 10) | (h << 21)
    return struct.pack("<I", val)


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def cover_crop(im: Image.Image, tw: int, th: int) -> Image.Image:
    im = im.convert("RGB")
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
            "Usage: lvgl_png_to_rgb565_bin.py <W> <H> <input.png|jpg> <output.bin>",
            file=sys.stderr,
        )
        raise SystemExit(2)
    tw = int(sys.argv[1])
    th = int(sys.argv[2])
    src = Path(sys.argv[3])
    dst = Path(sys.argv[4])
    dst.parent.mkdir(parents=True, exist_ok=True)

    im = Image.open(src)
    im = cover_crop(im, tw, th)
    pixels = im.load()
    out = bytearray()
    out += pack_img_header(tw, th)
    for y in range(th):
        for x in range(tw):
            r, g, b = pixels[x, y]
            u16 = rgb888_to_rgb565(r, g, b)
            out += struct.pack("<H", u16)
    dst.write_bytes(out)
    print(f"Wrote {dst} ({len(out)} bytes) from {src}")


if __name__ == "__main__":
    main()
