"""Renders a wallpaper for each shipped theme (no dependencies).

Each src/bar/config/themes/<name>.theme gets a <name>.png beside it: a soft
diagonal gradient of the theme's background colour (its Terminal scheme's
background, or Windows' dark/light surface) with a glow of its accent
(ColorizationColor) in one corner, so every theme has a wallpaper of its own
without shipping anyone else's images. Ordered dithering keeps the gradients
free of banding. Re-run after adding a theme or changing its colours:
    py -3 etc/make-wallpapers.py [name...]
"""
import json
import math
import struct
import sys
import zlib
from pathlib import Path

THEMES = Path(__file__).resolve().parent.parent / "src" / "bar" / "config" / "themes"
WIDTH, HEIGHT = 1920, 1080

# 8x8 Bayer matrix, as thresholds in -0.5..0.5.
BAYER = [
    [0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22], [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21],
]
BAYER = [[v / 64.0 - 0.5 for v in row] for row in BAYER]


def rgb(hex6):
    hex6 = hex6.lstrip("#")[-6:]
    return tuple(int(hex6[i:i + 2], 16) for i in (0, 2, 4))


def read_theme(theme_file):
    """Background, accent and mode of a .theme (plus its Terminal scheme)."""
    values = {}
    for raw in theme_file.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith(";") or "=" not in line:
            continue
        key, value = (s.strip() for s in line.split("=", 1))
        values[key.lower()] = value
    light = values.get("systemmode", "Light").lower() == "light"
    accent = rgb(values.get("colorizationcolor", "0XC40078D4"))
    scheme = theme_file.with_name(theme_file.stem + ".terminal.json")
    if scheme.exists():
        background = rgb(json.loads(scheme.read_text(encoding="utf-8"))["background"])
    else:
        background = (0xF3, 0xF3, 0xF3) if light else (0x20, 0x20, 0x20)
    return {"background": background, "accent": accent, "light": light}


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def render(colours):
    bg = colours["background"]
    accent = colours["accent"]
    # Dark themes fade towards black, light ones towards white.
    far = mix(bg, (255, 255, 255) if colours["light"] else (0, 0, 0), 0.22)
    near = mix(bg, accent, 0.10)
    glow_x, glow_y, glow_r = WIDTH * 0.8, HEIGHT * 0.95, WIDTH * 0.62

    rows = []
    for y in range(HEIGHT):
        row = bytearray(1 + WIDTH * 3)  # filter byte 0, then RGB
        bayer_row = BAYER[y & 7]
        fy = y / HEIGHT
        dy = (y - glow_y) / glow_r
        for x in range(WIDTH):
            t = (x / WIDTH) * 0.6 + fy * 0.4
            dx = (x - glow_x) / glow_r
            d = math.sqrt(dx * dx + dy * dy)
            glow = 0.0 if d >= 1.0 else 0.26 * (1.0 - d) ** 2
            dither = bayer_row[x & 7]
            o = 1 + x * 3
            for c in range(3):
                v = near[c] + (far[c] - near[c]) * t
                v += (accent[c] - v) * glow
                v = int(v + dither + 0.5)
                row[o + c] = 0 if v < 0 else 255 if v > 255 else v
        rows.append(bytes(row))
    return b"".join(rows)


def png(width, height, rgb_rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit RGB
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rgb_rows, 9))
            + chunk(b"IEND", b""))


def main():
    only = set(sys.argv[1:])
    for theme in sorted(THEMES.glob("*.theme")):
        if only and theme.stem not in only:
            continue
        out = theme.with_suffix(".png")
        out.write_bytes(png(WIDTH, HEIGHT, render(read_theme(theme))))
        print(f"{out.relative_to(THEMES.parent.parent.parent.parent)}  {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
