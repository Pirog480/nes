#!/usr/bin/env python3
"""Convert binary PPM (P6) to PNG using only the standard library."""
import struct
import sys
import zlib


def ppm_to_png(src: str, dst: str) -> None:
    with open(src, "rb") as f:
        data = f.read()

    # parse P6 header (magic, width, height, maxval — comments allowed)
    parts, i = [], 0
    while len(parts) < 4:
        while i < len(data) and data[i] in b" \t\r\n":
            i += 1
        if i < len(data) and data[i] == ord("#"):
            while i < len(data) and data[i] not in b"\r\n":
                i += 1
            continue
        j = i
        while j < len(data) and data[j] not in b" \t\r\n":
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1  # single whitespace after maxval
    w, h = int(parts[1]), int(parts[2])
    pixels = data[i : i + w * h * 3]
    if len(pixels) < w * h * 3:
        raise ValueError("truncated PPM")

    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: none
        row = pixels[y * w * 3 : (y + 1) * w * 3]
        raw.extend(row)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit RGB
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + chunk(b"IEND", b"")
    )
    with open(dst, "wb") as f:
        f.write(png)
    print(f"{dst}: {w}x{h}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("usage: ppm2png.py in.ppm out.png")
        sys.exit(1)
    ppm_to_png(sys.argv[1], sys.argv[2])
