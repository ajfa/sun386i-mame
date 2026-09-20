#!/usr/bin/env python3
"""Read a 4.2BSD/SunOS FFS image: list the tree or extract a file.

Usage: ffs.py image [path-to-extract output]
"""
import struct, sys

SBOFF = 8192
MAGIC = 0x011954


class Ffs:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.d = f.read()
        self.end = "<"
        if struct.unpack_from("<I", self.d, SBOFF + 0x55c)[0] != MAGIC:
            if struct.unpack_from(">I", self.d, SBOFF + 0x55c)[0] == MAGIC:
                self.end = ">"
            else:
                raise ValueError("no FFS superblock")
        u = lambda off: struct.unpack_from(self.end + "I", self.d, SBOFF + off)[0]
        self.iblkno = u(0x10)
        self.cgoffset = u(0x18)
        self.cgmask = u(0x1c)
        self.bsize = u(0x30)
        self.fsize = u(0x34)
        self.frag = u(0x38)
        self.fsbtodb = u(0x64)
        self.inopb = u(0x78)
        self.ipg = u(0xb8)
        self.fpg = u(0xbc)
        self.ncg = u(0x2c)

    def cgstart(self, c):
        return c * self.fpg + (self.cgoffset * (c & ~self.cgmask))

    def inode(self, ino):
        cg = ino // self.ipg
        base = (self.cgstart(cg) + self.iblkno) * self.fsize
        off = base + (ino % self.ipg) * 128
        raw = self.d[off:off + 128]
        e = self.end
        mode, nlink, uid, gid = struct.unpack_from(e + "HhHh", raw, 0)
        size = struct.unpack_from(e + "I", raw, 0x08)[0]     # low half of the quad
        db = struct.unpack_from(e + "12I", raw, 0x28)
        ib = struct.unpack_from(e + "3I", raw, 0x58)
        return {"mode": mode, "nlink": nlink, "uid": uid, "gid": gid,
                "size": size, "db": db, "ib": ib}

    def blocks(self, ino):
        out = []
        out.extend(ino["db"])
        e = self.end
        if ino["ib"][0]:
            base = ino["ib"][0] * self.fsize
            n = self.bsize // 4
            out.extend(struct.unpack_from(e + f"{n}I", self.d, base))
        if ino["ib"][1]:
            base = ino["ib"][1] * self.fsize
            n = self.bsize // 4
            for b in struct.unpack_from(e + f"{n}I", self.d, base):
                if b:
                    out.extend(struct.unpack_from(e + f"{n}I", self.d, b * self.fsize))
        return out

    def read(self, ino):
        data = bytearray()
        for b in self.blocks(ino):
            if len(data) >= ino["size"]:
                break
            if b == 0:
                data.extend(b"\0" * self.bsize)
                continue
            data.extend(self.d[b * self.fsize: b * self.fsize + self.bsize])
        return bytes(data[:ino["size"]])

    def readdir(self, ino):
        data = self.read(ino)
        e = self.end
        out, off = [], 0
        while off + 8 <= len(data):
            d_ino, reclen, namlen = struct.unpack_from(e + "IHH", data, off)
            if reclen == 0:
                break
            name = data[off + 8: off + 8 + namlen].decode("ascii", "replace")
            if d_ino:
                out.append((name, d_ino))
            off += reclen
        return out

    def walk(self, ino=2, path="", depth=0, limit=400):
        node = self.inode(ino)
        if (node["mode"] & 0xf000) != 0x4000:
            return
        for name, child in self.readdir(node):
            if name in (".", ".."):
                continue
            c = self.inode(child)
            kind = "d" if (c["mode"] & 0xf000) == 0x4000 else \
                   ("l" if (c["mode"] & 0xf000) == 0xa000 else "-")
            print(f"{kind} {c['size']:>9} {path}/{name}")
            if kind == "d" and depth < 6:
                self.walk(child, path + "/" + name, depth + 1)

    def lookup(self, path):
        ino = 2
        for part in path.strip("/").split("/"):
            if not part:
                continue
            entries = dict(self.readdir(self.inode(ino)))
            if part not in entries:
                raise KeyError(path)
            ino = entries[part]
        return ino


if __name__ == "__main__":
    fs = Ffs(sys.argv[1])
    print(f"# endian {fs.end} bsize {fs.bsize} fsize {fs.fsize} ipg {fs.ipg} "
          f"fpg {fs.fpg} ncg {fs.ncg} iblkno {fs.iblkno}")
    if len(sys.argv) > 3:
        ino = fs.lookup(sys.argv[2])
        with open(sys.argv[3], "wb") as f:
            f.write(fs.read(fs.inode(ino)))
        print(f"extracted {sys.argv[2]} -> {sys.argv[3]}")
    else:
        fs.walk()
