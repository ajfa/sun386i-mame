#!/usr/bin/env python3
"""Extract Sun 'bar' multi-volume floppy sets (SunOS 4.0.x, Sun386i).

Each volume is a raw 1.44 MB image: block 0 is the volume header, then
tar-like 512-byte file headers followed by file data.  Data is normally
compress(1)-ed (magic 1f 9d) because the media was written with 'bar cZ'.
Files may span volumes; a continuation volume starts again with its own
block-0 volume header.
"""
import os, re, subprocess, sys

BS = 512


def octal(field):
    field = field.replace(b"\0", b" ").strip()
    if not field:
        return 0
    try:
        return int(field, 8)
    except ValueError:
        return -1


class VolumeSet:
    """Concatenated data area of an ordered list of volumes."""

    def __init__(self, paths):
        self.paths = paths
        self.vols = []
        for p in paths:
            with open(p, "rb") as f:
                self.vols.append(f.read())
        self.cur = 0
        self.pos = BS  # skip volume header of first volume

    def eof(self):
        return self.cur >= len(self.vols)

    def _advance(self):
        self.cur += 1
        self.pos = BS

    def read(self, n):
        out = bytearray()
        while n > 0 and not self.eof():
            avail = len(self.vols[self.cur]) - self.pos
            if avail <= 0:
                self._advance()
                continue
            take = min(n, avail)
            out += self.vols[self.cur][self.pos:self.pos + take]
            self.pos += take
            n -= take
        return bytes(out)

    def where(self):
        return (os.path.basename(self.paths[self.cur]), self.pos) if not self.eof() else ("EOF", 0)


def parse_header(blk):
    if len(blk) < BS or blk[:BS] == b"\0" * BS:
        return None
    name = blk[84:184].split(b"\0")[0]
    if not name or not re.match(rb"^[A-Za-z0-9./][A-Za-z0-9._/+-]*$", name):
        return None
    try:
        name = name.decode("ascii")
    except UnicodeDecodeError:
        return None
    mode = octal(blk[0:8])
    size = octal(blk[24:36])
    if size < 0 or mode < 0:
        return None
    return {"name": name, "mode": mode, "size": size,
            "uid": octal(blk[8:16]), "gid": octal(blk[16:24]),
            "mtime": octal(blk[36:48])}


def ensure_dir(path):
    """Create path, moving aside any plain file that occupies part of it.

    The media name whole clusters ('boot', 'base_devel') with the same strings
    it later uses as directories, so both can collide inside one set.
    """
    parts = path.split(os.sep)
    for i in range(1, len(parts) + 1):
        step = os.sep.join(parts[:i])
        if step and os.path.isfile(step):
            os.rename(step, step + ".file")
    os.makedirs(path, exist_ok=True)


def extract(paths, outdir, listing):
    vs = VolumeSet(paths)
    n_ok = n_raw = 0
    while not vs.eof():
        blk = vs.read(BS)
        if len(blk) < BS:
            break
        h = parse_header(blk)
        if h is None:
            continue
        vol, off = vs.where()
        data = vs.read(h["size"])
        pad = (-h["size"]) % BS
        if pad:
            vs.read(pad)
        rel = (h["name"].lstrip("./") or "_root").rstrip("/")
        dst = os.path.join(outdir, rel)
        if h["name"].endswith("/") or h["size"] == 0:
            ensure_dir(dst)
            listing.write(f"{h['name']}\t{h['size']}\t{h['mode']:o}\tdir\n")
            continue
        ensure_dir(os.path.dirname(dst))
        if os.path.isdir(dst):          # same name used for a directory earlier
            dst += ".file"
        status = "raw"
        if data[:2] == b"\x1f\x9d":
            try:
                out = subprocess.run(["gzip", "-dc"], input=data,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, check=True).stdout
                with open(dst, "wb") as f:
                    f.write(out)
                status = f"Z->{len(out)}"
                n_ok += 1
            except subprocess.CalledProcessError:
                with open(dst + ".Z", "wb") as f:
                    f.write(data)
                status = "Z-BAD"
                n_raw += 1
        else:
            with open(dst, "wb") as f:
                f.write(data)
            n_raw += 1
        listing.write(f"{h['name']}\t{h['size']}\t{h['mode']:o}\t{status}\t{vol}+{off}\n")
    return n_ok, n_raw


def main():
    outdir, listfile = sys.argv[1], sys.argv[2]
    paths = sys.argv[3:]
    os.makedirs(outdir, exist_ok=True)
    with open(listfile, "w") as listing:
        ok, raw = extract(paths, outdir, listing)
    print(f"{outdir}: {ok} decompressed, {raw} stored raw")


if __name__ == "__main__":
    main()
