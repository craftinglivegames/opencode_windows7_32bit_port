#!/usr/bin/env python3
"""Build the self extracting portable package for Windows 7.

    make_package.py <launcher.exe> <payload-dir> <output.exe>

The payload files are deflate compressed and appended to the launcher
together with a small manifest (see src/payload.cpp for the layout).
"""

import os
import struct
import sys
import zlib

TRAILER_MAGIC = b"OCSHPAY2"


def build(launcher: str, payload_dir: str, output: str) -> None:
    data = bytearray(open(launcher, "rb").read())
    base = len(data)

    files = []
    for root, _dirs, names in os.walk(payload_dir):
        for name in sorted(names):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, payload_dir).replace("\\", "/")
            files.append((rel, full))
    files.sort()

    manifest = bytearray()
    manifest += struct.pack("<I", len(files))
    entries = []
    for rel, full in files:
        raw = open(full, "rb").read()
        packed = zlib.compress(raw, 9)
        if len(packed) >= len(raw):
            packed = raw
            method = 0
        else:
            method = 1
        offset = len(data)
        data += packed
        crc = zlib.crc32(raw) & 0xFFFFFFFF
        name = rel.encode("utf-8")
        entries.append((name, offset, len(raw), len(packed), crc, method))
        print(f"  {rel:24s} {len(raw):>10d} -> {len(packed):>10d} bytes")

    for name, offset, size, stored, crc, method in entries:
        manifest += struct.pack("<H", len(name))
        manifest += name
        manifest += struct.pack("<Q", offset)
        manifest += struct.pack("<Q", size)
        manifest += struct.pack("<Q", stored)
        manifest += struct.pack("<I", crc)
        manifest += struct.pack("<B", method)

    manifest_offset = len(data)
    data += manifest
    data += TRAILER_MAGIC
    data += struct.pack("<Q", manifest_offset)
    data += struct.pack("<I", len(manifest))
    data += struct.pack("<I", zlib.crc32(bytes(manifest)) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    with open(output, "wb") as fh:
        fh.write(data)
    print(f"wrote {output} ({len(data)} bytes, launcher {base} bytes)")


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2], sys.argv[3])
