#!/usr/bin/env python3
"""Convert a QEMU screendump (PPM) to PNG so it can be viewed.

usage: ppm2png.py <in.ppm> [out.png]
"""
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    # P6 header: magic, width, height, maxval - whitespace separated, '#' comments
    fields = []
    i = 0
    while len(fields) < 4:
        while i < len(data) and data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i] != 0x0A:
                i += 1
            continue
        j = i
        while j < len(data) and not data[j : j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    i += 1
    magic, w, h, maxv = fields[0], int(fields[1]), int(fields[2]), int(fields[3])
    if magic != b"P6":
        raise SystemExit(f"unsupported ppm magic {magic!r}")
    return w, h, data[i : i + w * h * 3]


def write_png(path, w, h, rgb):
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride : (y + 1) * stride]

    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


if __name__ == "__main__":
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
    w, h, rgb = read_ppm(src)
    write_png(dst, w, h, rgb)
    print(f"{src} -> {dst} ({w}x{h})")
