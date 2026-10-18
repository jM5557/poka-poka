#!/usr/bin/env python3
"""Writes the launcher icon: a generic paw print on a warm tile, as a PNG.

Original artwork drawn here with the standard library only; no game assets.
Usage: make_icon.py <output.png> [size] [--foreground | --round]
  --foreground  the paw alone on transparency, inside Android's adaptive-icon
                safe zone (the background colour comes from resources)
  --round       the tile as a circle (Android's legacy round icon)
"""

import struct
import sys
import zlib

SIZE = 256
BACKGROUND = (0xE8, 0x9A, 0x3C)
EDGE = (0xB8, 0x6A, 0x1E)
PAW = (0xFF, 0xF4, 0xE0)


def inside_round_rect(x, y, margin=12, radius=48):
    lo, hi = margin, SIZE - 1 - margin
    if not (lo <= x <= hi and lo <= y <= hi):
        return False
    cx = min(max(x, lo + radius), hi - radius)
    cy = min(max(y, lo + radius), hi - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius**2


def inside_ellipse(x, y, cx, cy, rx, ry):
    return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0


PADS = [
    (128, 160, 54, 44),  # main pad
    (70, 104, 20, 26),
    (106, 72, 20, 27),
    (150, 72, 20, 27),
    (186, 104, 20, 26),
]


def on_paw(x, y, scale=1.0):
    # Scale about the centre: the adaptive foreground keeps to the middle 61%.
    sx = (x - SIZE / 2) / scale + SIZE / 2
    sy = (y - SIZE / 2) / scale + SIZE / 2
    return any(inside_ellipse(sx, sy, *pad) for pad in PADS)


def pixel(x, y, mode):
    if mode == "foreground":
        return PAW + (255,) if on_paw(x, y, 0.61) else (0, 0, 0, 0)
    if mode == "round":
        r2 = (x - SIZE / 2) ** 2 + (y - SIZE / 2) ** 2
        if r2 > (SIZE / 2 - 8) ** 2:
            return (0, 0, 0, 0)
        if r2 > (SIZE / 2 - 16) ** 2:
            return EDGE + (255,)
    else:
        if not inside_round_rect(x, y):
            return (0, 0, 0, 0)
        if not inside_round_rect(x, y, margin=20, radius=40):
            return EDGE + (255,)
    return PAW + (255,) if on_paw(x, y) else BACKGROUND + (255,)


def main():
    out = sys.argv[1]
    size = int(sys.argv[2]) if len(sys.argv) > 2 else SIZE
    mode = "foreground" if "--foreground" in sys.argv else "round" if "--round" in sys.argv else "tile"
    # Drawn at 256, sampled down (4x4 supersampling per output pixel).
    sub = 4
    rows = bytearray()
    for oy in range(size):
        rows.append(0)
        for ox in range(size):
            acc = [0, 0, 0, 0]
            for j in range(sub):
                for i in range(sub):
                    x = (ox + (i + 0.5) / sub) * SIZE / size
                    y = (oy + (j + 0.5) / sub) * SIZE / size
                    p = pixel(x, y, mode)
                    a = p[3]
                    acc[0] += p[0] * a
                    acc[1] += p[1] * a
                    acc[2] += p[2] * a
                    acc[3] += a
            n = sub * sub
            alpha = acc[3] / n
            if acc[3]:
                rows.extend((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(alpha)))
            else:
                rows.extend((0, 0, 0, 0))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    with open(out, "wb") as handle:
        handle.write(png)


if __name__ == "__main__":
    main()
