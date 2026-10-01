"""Generates src/resources/visor.ico (16-256 px PNG entries, no dependencies).

The mark is a dark rounded tile with a bright bar across the top: a status bar.
Re-run after editing the drawing code:  py -3 etc/make-icon.py
"""
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "src" / "resources" / "visor.ico"
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SS = 4  # supersampling factor for anti-aliasing

TILE = (0x1E, 0x1E, 0x28)
BAR = (0x8A, 0xB4, 0xFF)


def inside_rounded(x, y, x0, y0, x1, y1, r):
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def pixel(u, v):
    """Colour at normalised coords (0..1), or None for transparent."""
    if not inside_rounded(u, v, 0.03, 0.03, 0.97, 0.97, 0.2):
        return None
    if inside_rounded(u, v, 0.16, 0.18, 0.84, 0.36, 0.09):
        return BAR
    return TILE


def render(size):
    n = size * SS
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(SS):
                for sx in range(SS):
                    c = pixel((x * SS + sx + 0.5) / n, (y * SS + sy + 0.5) / n)
                    if c:
                        acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 1
            a = acc[3]
            if a:
                row += bytes((acc[0] // a, acc[1] // a, acc[2] // a, 255 * a // (SS * SS)))
            else:
                row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    images = [png(s, render(s)) for s in SIZES]
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for s, img in zip(SIZES, images):
        d = 0 if s >= 256 else s
        entries += struct.pack("<BBBBHHII", d, d, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + entries + b"".join(images))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
