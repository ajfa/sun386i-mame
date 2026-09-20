#!/usr/bin/env python3
"""Build the Sun386i stub boot PROM and IDPROM images for the MAME driver.

The stub occupies the 128 KiB window at 0xfffe0000. It holds the handful of
romvec slots the SunOS kernel reads, a page directory description of the PROM
window, and the reset code that enters 32-bit protected mode and jumps to the
kernel loaded at physical 0x1b000.
"""
import os, struct, sys

BASE = 0xfffe0000
SIZE = 0x20000
KERNEL_ENTRY = 0x1b000
ENTRY_SLOT = 0x3f4000               # the loader leaves the entry point here
PAGING_SLOT = 0x3f4004              # and says whether to start with paging on
TABLE_SLOT = 0x3f4008               # and where the page tables may live


# What the real PROM leaves mapped high, as used by the standalone programs
PD_VIRT = 0xfff9f000                # the standalone reads the page directory here
SCC_VIRT = 0xfff18000
SCC_PHYS = 0xfc000000
MEMSIZE = 8 << 20

rom = bytearray(b"\xff" * SIZE)


def put(off, data):
    rom[off:off + len(data)] = data


def putd(off, value):
    put(off, struct.pack("<I", value))


# ---------------------------------------------------------------- romvec
OFF_MEMSIZE = 0x0200
OFF_SPARE = 0x0210
OFF_FBTYPE = 0x0220
OFF_VECTORS = 0x0240
OFF_PUTCHAR = 0x0300
OFF_ABORT = 0x0320
OFF_RET0 = 0x0330
OFF_RETM1 = 0x0340
OFF_GETCHAR = 0x0360
OFF_MAYGET = 0x0380
OFF_ZERO = 0x0400
OFF_ONE = 0x03a0
OFF_PM = 0x0100
OFF_REBOOT = 0x03c0

# Slots the SunOS kernels reach, from tools/promrefs.py. Data slots point at a
# zeroed area, called slots at a routine that returns something harmless.
putd(0x0c, BASE + OFF_ZERO)         # boot string, read by bootflags/getblockdev
putd(0x10, BASE + OFF_MEMSIZE)      # pointer to memory size in bytes
putd(0x18, BASE + OFF_PUTCHAR)      # putchar
putd(0x1c, BASE + OFF_RETM1)        # getchar
putd(0x20, BASE + OFF_RET0)         # output poll
CONSOLE = OFF_ZERO if os.environ.get("SUN386I_FBCONSOLE") else OFF_ONE
putd(0x28, BASE + CONSOLE)          # console in: 0 is the screen, 1 is ttya
putd(0x2c, BASE + CONSOLE)          # console out
putd(0x34, BASE + OFF_RETM1)        # non-blocking getchar
putd(0x54, BASE + OFF_ZERO)
putd(0x5c, BASE + OFF_RET0)
putd(0x60, BASE + OFF_REBOOT)       # reboot: the emulator reloads and resets
putd(0x84, BASE + OFF_ABORT)        # enter monitor
putd(0x98, BASE + OFF_RET0)         # called by the standalone at startup
putd(0xa0, BASE + OFF_FBTYPE)   # which frame buffer is the console:
                                    # consconfig compares it with 2 for bwtwo,
                                    # 3 for cgtwo and 6 for the next one along
putd(0xa4, 1)                       # PROM revision, 2 or more patches v_handler
putd(0xb8, BASE + OFF_MEMSIZE)      # pointer to memory size in bytes
putd(0xc4, BASE + OFF_ABORT)        # abort to monitor
putd(0xd0, BASE + OFF_SPARE)
putd(0xe4, BASE + OFF_RET0)         # the PROM routine the kernel puts in its vector table
putd(0xec, BASE + OFF_RET0)         # called from main and mbconfig
putd(0xf0, BASE + OFF_RET0)

# The boot parameter block, in the layout Sun's PROMs use: argv pointers, a
# string area, then the boot device. The INSTALL kernel's ramdisk() compares
# bootparam+0x84 with "fd", and bootflags() takes its argument string from
# bootparam+4.
OFF_BPPTR = 0x0600
OFF_BOOTPARAM = 0x0c00

bp = bytearray(0x120)
struct.pack_into("<I", bp, 0x00, BASE + OFF_BOOTPARAM + 0x20)   # argv[0]
bp[0x20:0x28] = b"vmunix\0\0"                                   # bp_strings
if os.environ.get("SUN386I_SINGLE"):
    # a second argument, which bootflags() reads: single user
    bp[0x28:0x2c] = b"-s" + bytes(2)
    struct.pack_into("<I", bp, 0x04, BASE + OFF_BOOTPARAM + 0x28)
bp[0x84:0x86] = b"fd"                                           # bp_dev
struct.pack_into("<I", bp, 0x88, 0)                             # bp_ctlr
struct.pack_into("<I", bp, 0x8c, 0)                             # bp_unit
struct.pack_into("<I", bp, 0x90, 0)                             # bp_part
put(OFF_BOOTPARAM, bytes(bp))
putd(OFF_BPPTR, BASE + OFF_BOOTPARAM)
putd(0x0c, BASE + OFF_BPPTR)

putd(OFF_MEMSIZE, MEMSIZE)
putd(OFF_FBTYPE, int(os.environ.get("SUN386I_FBTYPE", "2")))
                                    # 2 is the bwtwo, 6 the cgthree
putd(OFF_SPARE, 0)
put(OFF_VECTORS, bytes(0x40))
put(OFF_ZERO, bytes(0x100))
putd(OFF_ONE, 1)
put(OFF_RET0, bytes([0x31, 0xc0, 0xc3]))                        # xor eax, eax; ret
put(OFF_RETM1, bytes([0xb8, 0xff, 0xff, 0xff, 0xff, 0xc3]))     # mov eax, -1; ret

putchar = bytearray()
putchar += bytes([0x50])                            # push eax
putchar += bytes([0x52])                            # push edx
putchar += bytes([0x8a, 0x44, 0x24, 0x0c])          # mov al, [esp+12]
putchar += bytes([0x66, 0xba, 0x00, 0x80])          # mov dx, 0x8000
putchar += bytes([0xee])                            # out dx, al
putchar += bytes([0x5a])                            # pop edx
putchar += bytes([0x58])                            # pop eax
putchar += bytes([0xc3])                            # ret
put(OFF_PUTCHAR, bytes(putchar))
put(OFF_ABORT, bytes([0xfa, 0xf4, 0xeb, 0xfd]))     # cli; hlt; jmp $-1

# Reboot: there is no disk loader in the stub, so the emulator is asked to put
# the kernel back where it was and reset the machine.
put(OFF_REBOOT, bytes([
    0xb8, 0x01, 0x00, 0x00, 0x00,                   # mov eax, 1
    0xba, 0x04, 0x80, 0x00, 0x00,                   # mov edx, 0x8004
    0xef,                                           # out dx, eax
    0xfa, 0xf4, 0xeb, 0xfd]))                       # cli; hlt; jmp $-1

# Keyboard input comes from the emulator through port 0x8010: zero means
# nothing typed. The blocking form is what cngetc and gets use.
getchar = bytes([0x66, 0xba, 0x10, 0x80,            # mov dx, 0x8010
                 0x31, 0xc0,                        # xor eax, eax
                 0xec,                              # in al, dx
                 0x84, 0xc0,                        # test al, al
                 0x74, 0xf5,                        # je back to the start
                 0xc3])                             # ret
mayget = bytes([0x66, 0xba, 0x14, 0x80,
                0x31, 0xc0,
                0xec,
                0x84, 0xc0,
                0x75, 0x05,                         # jne done
                0xb8, 0xff, 0xff, 0xff, 0xff,       # mov eax, -1
                0xc3])
put(OFF_GETCHAR, getchar)
put(OFF_MAYGET, mayget)
putd(0x1c, BASE + OFF_MAYGET)   # gets() drains input until this returns -1
putd(0x34, BASE + OFF_MAYGET)

# ------------------------------------------------- trap reporting for the stub
OFF_IDT = 0x0800
OFF_IDTR = 0x0900
OFF_HANDLERS = 0x0a00
OFF_TRAP = 0x0b00

trap = bytearray()
trap += bytes([0x58])                                   # pop eax, the vector
trap += bytes([0x66, 0xba, 0x04, 0x80])                 # mov dx, 0x8004
trap += bytes([0xef])                                   # out dx, eax
trap += bytes([0x58])                                   # pop eax, error code or eip
trap += bytes([0x66, 0xba, 0x08, 0x80])                 # mov dx, 0x8008
trap += bytes([0xef])                                   # out dx, eax
trap += bytes([0x58])                                   # pop eax, eip or cs
trap += bytes([0xef])                                   # out dx, eax
trap += bytes([0x0f, 0x20, 0xd0])                       # mov eax, cr2
trap += bytes([0x66, 0xba, 0x0c, 0x80])                 # mov dx, 0x800c
trap += bytes([0xef])                                   # out dx, eax
trap += bytes([0xfa, 0xf4, 0xeb, 0xfd])                 # cli; hlt; jmp $-1
put(OFF_TRAP, bytes(trap))

for vec in range(32):
    here = OFF_HANDLERS + vec * 8
    rel = OFF_TRAP - (here + 7)
    put(here, bytes([0x6a, vec, 0xe9]) + struct.pack("<i", rel))
    gate = struct.pack("<HHBBH", (BASE + here) & 0xffff, 0x08, 0, 0x8e,
                       ((BASE + here) >> 16) & 0xffff)
    put(OFF_IDT + vec * 8, gate)
put(OFF_IDTR, struct.pack("<HI", 32 * 8 - 1, BASE + OFF_IDT))

# ------------------------------------------------------- protected mode entry
# the console speed lives in the chip: zsgetspeed reads the time constant back
# out of RR12 and RR13 and the driver keeps whatever it finds
OFF_SCCTAB = 0x0500
SCC_LINES = ((0xfc000008, 0x000e), (0xfc000000, 0x000e),     # ttya, ttyb 9600
             (0xa0000028, 0x007e), (0xa0000020, 0x007e))     # keyboard, mouse 1200
put(OFF_SCCTAB, b"".join(struct.pack("<II", p, t) for p, t in SCC_LINES) + b"\0" * 4)

import stub_pm
put(OFF_PM, stub_pm.build(BASE, ENTRY_SLOT, PAGING_SLOT, TABLE_SLOT,
                          BASE + OFF_IDTR, SCC_VIRT, SCC_PHYS, PD_VIRT,
                          BASE + OFF_SCCTAB))

# --------------------------------------------------------------- GDT and reset
OFF_GDT = 0x1ff00
OFF_GDTR = 0x1ff20
OFF_RESET16 = 0x1ff30
OFF_RESET = 0x1fff0

gdt = b"\x00" * 8
gdt += bytes([0xff, 0xff, 0x00, 0x00, 0x00, 0x9b, 0xcf, 0x00])   # 0x08 code32 flat
gdt += bytes([0xff, 0xff, 0x00, 0x00, 0x00, 0x93, 0xcf, 0x00])   # 0x10 data32 flat
put(OFF_GDT, gdt)
put(OFF_GDTR, struct.pack("<HI", len(gdt) - 1, BASE + OFF_GDT))

r16 = bytearray()
r16 += bytes([0xfa, 0xfc])                                       # cli; cld
r16 += bytes([0x66, 0x2e, 0x0f, 0x01, 0x16]) + struct.pack("<H", (OFF_GDTR - 0x10000) & 0xffff)
r16 += bytes([0x0f, 0x20, 0xc0])                                 # mov eax, cr0
r16 += bytes([0x0c, 0x11])                                       # or al, 0x11
# bit 4 of CR0 is ET, and on a 386 the board sets it when a 387 is fitted.
# initfp reads it and nothing else: with it clear the kernel decides there is
# no coprocessor, never runs fninit, and every floating point result is rubbish.
r16 += bytes([0x0f, 0x22, 0xc0])                                 # mov cr0, eax
r16 += bytes([0x66, 0xea]) + struct.pack("<I", BASE + OFF_PM) + struct.pack("<H", 0x08)
put(OFF_RESET16, bytes(r16))

# at the reset vector, jump back to the 16-bit init a few hundred bytes earlier
rel = (OFF_RESET16 - (OFF_RESET + 3)) & 0xffff
put(OFF_RESET, bytes([0xe9]) + struct.pack("<H", rel))

# ------------------------------------------------------------------- IDPROM
idprom = bytearray(32)
idprom[0] = 0x01                    # format
idprom[1] = 0x31                    # Sun386i
idprom[2:8] = bytes([0x08, 0x00, 0x20, 0x01, 0x02, 0x03])
idprom[8:12] = struct.pack(">I", 0x21000000)
idprom[12:15] = bytes([0x01, 0x02, 0x03])
idprom[15] = 0
chk = 0
for b in idprom[0:15]:
    chk ^= b
idprom[15] = chk


def carray(name, data):
    out = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 12):
        row = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        out.append(f"\t{row},")
    out.append("};")
    return "\n".join(out)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as f:
        f.write(rom)
    with open(sys.argv[2], "wb") as f:
        f.write(idprom)
    # the stub is sparse: emit it as a list of (offset, bytes) runs
    runs = []
    i = 0
    while i < SIZE:
        if rom[i] != 0xff:
            j = i
            while j < SIZE and not (rom[j] == 0xff and rom[j:j + 8] == b"\xff" * 8):
                j += 1
            runs.append((i, bytes(rom[i:j])))
            i = j
        else:
            i += 1
    print(f"// generated by tools/gen_stub.py, {len(runs)} runs")
    for n, (off, data) in enumerate(runs):
        print(f"// run {n} at {off:#07x} ({len(data)} bytes)")
    with open(sys.argv[3], "w") as f:
        f.write("// generated by tools/gen_stub.py -- do not edit\n")
        for n, (off, data) in enumerate(runs):
            f.write(carray(f"s_prom_run{n}", data) + "\n")
        f.write("static const struct { uint32_t offset; const uint8_t *data; uint32_t size; } "
                "s_prom_runs[] = {\n")
        for n, (off, data) in enumerate(runs):
            f.write(f"\t{{ {off:#07x}, s_prom_run{n}, {len(data)} }},\n")
        f.write("};\n")
        f.write(carray("s_idprom", idprom) + "\n")
        # the machine's own NVRAM, from a real 386i/150, so that the fields the
        # kernel reads (artwork, hardware revision, boot message level) hold what
        # a working machine has; the clock registers at the end are left out
        try:
            with open("../nvram/nvram-150.bin", "rb") as nv:
                f.write(carray("s_nvram", nv.read()[:0x7f8]) + "\n")
        except FileNotFoundError:
            f.write("static const uint8_t s_nvram[1] = { 0 };\n")
    print(f"wrote {sys.argv[3]}")
