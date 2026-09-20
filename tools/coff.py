#!/usr/bin/env python3
"""Minimal i386 COFF reader for SunOS 4.0.x/386 binaries (vmunix, *.o)."""
import struct
from collections import namedtuple

FILHDR = "<HHIIIHH"
SCNHDR = "<8sIIIIIIHHI"
SYMENT = "<8sIhhBB"

Section = namedtuple("Section", "name paddr vaddr size scnptr relptr lnnoptr nreloc nlnno flags")
Symbol = namedtuple("Symbol", "name value scnum type sclass numaux aux")

C_EXT, C_STAT, C_FILE = 2, 3, 103


class Coff:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.buf = f.read()
        (self.magic, self.nscns, self.timdat, self.symptr,
         self.nsyms, self.opthdr, self.flags) = struct.unpack_from(FILHDR, self.buf, 0)
        if self.magic != 0x014c:
            raise ValueError(f"not i386 COFF: magic {self.magic:#06x}")
        self.aout = None
        if self.opthdr:
            (self.amagic, self.vstamp, self.tsize, self.dsize, self.bsize,
             self.entry, self.text_start, self.data_start) = struct.unpack_from("<HHIIIIII", self.buf, 20)
        off = 20 + self.opthdr
        self.sections = []
        for i in range(self.nscns):
            f = struct.unpack_from(SCNHDR, self.buf, off + i * 40)
            self.sections.append(Section(f[0].split(b"\0")[0].decode(), *f[1:]))
        self._read_symbols()

    def _read_symbols(self):
        self.symbols = []
        if not self.symptr or not self.nsyms:
            self.strtab = b""
            return
        stroff = self.symptr + self.nsyms * 18
        strlen = struct.unpack_from("<I", self.buf, stroff)[0] if stroff + 4 <= len(self.buf) else 4
        self.strtab = self.buf[stroff:stroff + max(strlen, 4)]
        i = 0
        while i < self.nsyms:
            off = self.symptr + i * 18
            raw, value, scnum, typ, sclass, numaux = struct.unpack_from(SYMENT, self.buf, off)
            if raw[:4] == b"\0\0\0\0":
                sof = struct.unpack("<I", raw[4:8])[0]
                end = self.strtab.find(b"\0", sof)
                name = self.strtab[sof:end].decode("ascii", "replace")
            else:
                name = raw.split(b"\0")[0].decode("ascii", "replace")
            aux = [self.buf[off + 18 * (k + 1):off + 18 * (k + 2)] for k in range(numaux)]
            if sclass == C_FILE and aux:
                nm = aux[0].split(b"\0")[0]
                if nm:
                    name = nm.decode("ascii", "replace")
            self.symbols.append(Symbol(name, value, scnum, typ, sclass, numaux, aux))
            i += 1 + numaux

    def section(self, name):
        for s in self.sections:
            if s.name == name:
                return s
        return None

    def data(self, sec):
        return self.buf[sec.scnptr:sec.scnptr + sec.size]

    def read_va(self, va, n):
        for s in self.sections:
            if s.vaddr <= va < s.vaddr + s.size and s.scnptr:
                off = s.scnptr + (va - s.vaddr)
                return self.buf[off:off + n]
        return b""

    def funcs_by_file(self):
        """Group defined symbols by the C_FILE record that precedes them."""
        out, cur = {}, "?"
        for s in self.symbols:
            if s.sclass == C_FILE:
                cur = s.name
                out.setdefault(cur, [])
            elif s.sclass in (C_EXT, C_STAT) and s.scnum > 0:
                out.setdefault(cur, []).append(s)
        return out

    def symtab_by_addr(self):
        t = {}
        for s in self.symbols:
            if s.sclass in (C_EXT, C_STAT) and s.scnum > 0 and s.name:
                t.setdefault(s.value, s.name)
        return t


if __name__ == "__main__":
    import sys
    c = Coff(sys.argv[1])
    print(f"{c.path}: magic={c.magic:#06x} sections={c.nscns} syms={c.nsyms} entry={c.entry:#010x}")
    for s in c.sections:
        print(f"  {s.name:<10} vaddr={s.vaddr:#010x} size={s.size:#08x} off={s.scnptr:#08x} "
              f"reloc={s.nreloc}")
    files = c.funcs_by_file()
    print(f"  source files with symbols: {len(files)}")
