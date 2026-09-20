#!/usr/bin/env python3
"""Print the Sun disk label of a raw image and the offset of each partition."""
import struct, sys

with open(sys.argv[1], "rb") as f:
    d = f.read(512)
magic, = struct.unpack_from("<H", d, 508)
print("label:", d[:60].split(b"\0")[0].decode("ascii", "replace"))
print("magic: %04x" % magic)
ncyl, acyl, nhead, nsect = struct.unpack_from("<4H", d, 436)
print("ncyl %d acyl %d heads %d sectors %d" % (ncyl, acyl, nhead, nsect))
bpc = nhead * nsect
for i in range(8):
    cyl, nblk = struct.unpack_from("<2i", d, 444 + i * 8)
    if nblk:
        print("  %s: cylinder %5d  %8d blocks  offset %10d  size %6.1f MB"
              % ("abcdefgh"[i], cyl, nblk, cyl * bpc * 512, nblk * 512 / 1048576))
