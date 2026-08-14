#!/usr/bin/env python3
"""Generate deterministic, dependency-free PNG assets used by bundled projects."""

from pathlib import Path
import argparse
import math
import struct
import zlib


ROOT = Path(__file__).resolve().parents[1]


def chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))


def poster_mark(width: int = 192, height: int = 192) -> bytes:
    rows = bytearray()
    center_x = (width - 1) * 0.5
    center_y = (height - 1) * 0.5
    for y in range(height):
        rows.append(0)  # PNG filter: none
        for x in range(width):
            dx = (x - center_x) / width
            dy = (y - center_y) / height
            radius = (dx * dx + dy * dy) ** 0.5
            angle = math.atan2(dy, dx)
            ring = 0.20 < radius < 0.43
            notch = abs(angle) < 0.30 and x > center_x
            alpha = 235 if ring and not notch else 0
            red = int(95 + 145 * x / (width - 1))
            green = int(225 - 120 * y / (height - 1))
            blue = int(255 - 55 * x / (width - 1))
            rows.extend((red, green, blue, alpha))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="fail if the checked-in asset is absent or stale")
    args = parser.parse_args()
    target = ROOT / "projects/layered-poster/assets/images/microfx-mark.png"
    generated = poster_mark()
    if args.check:
        if not target.is_file() or target.read_bytes() != generated:
            raise SystemExit(f"stale generated asset: {target}")
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(generated)
    print(target)


if __name__ == "__main__":
    main()
