# Sun386i ("Roadrunner") machine map, recovered from SunOS 4.0.1/4.0.2 binaries

Everything below was measured on 2026-09-16 from the media in
`SunOS 4.0.1 (Sun386i).zip`, not taken from documentation. Sources:

- `ext/install/boot/vmunix` - SunOS 4.0.1 installation/diskless kernel, i386 COFF,
  709,547 bytes, text at 0xfc01b000, 3,689 symbols.
- `ext/upg/vmunix` - SunOS 4.0.2 disk kernel, 829,796 bytes, 4,041 symbols,
  banner `SunOS Release 4.0.2 (SDST386) #1: Mon Aug 7 18:34:12 EDT 1989`,
  built from `/files1/src4.0.1.adam/sys/sun386/SDST386/`.
- `media/app-01`, `media/diag` - bootable floppies, 32-bit standalone code at
  sector 1 (byte offset 0x200).
- `nvram/nvram-150.bin`, `nvram/nvram-250.bin` - 2 KiB NVRAM images of real
  machines (sunhelp.org), used only to confirm the layout below.

## CPU and MMU

The 386i uses the 80386's own paging. There is no exotic Sun MMU to model:

- `setcr3` is `mov cr3, eax`; `mmu_setpte` writes a 32-bit PTE into the page
  table pool `pt_pool` at index (va >> 12) and flushes the TLB with a CR3
  reload; `mmu_getkpteaddr` is `pt_pool + ((va & 0xfff000) >> 12) * 4`.
- The page directory is self-mapped at 0xffc00000 (`mmu_setpte`, `mmu_getpte`,
  `read_hwmap` and both standalones use that window).
- Kernel virtual base 0xfc000000; kernel text 0xfc01b000; `Syslimit` 0xfc80c000.
- The Sun vocabulary (segment maps, "pmg" page map groups, contexts) is a
  software layer: `mmu_setsegmap`, `mmu_setpmg`, `mmu_pmgtoptbl`, `hat_*`.
- 80387 plus optional Weitek 1167 at 0xffc0c000 (`init_wtimers`, `init_20MHz`,
  `restore_weitek`, kernel string `No Weitek floating point present!`).

## Kernel-virtual device windows

| Window | Contents | Evidence |
|---|---|---|
| 0xfceb0000 | AT-style DMA + interrupt block, PC register offsets | `dma_reg[]` table, `init8259`, `dma_cmd_regs`, `dma_mode_regs` |
| 0xfcef0000, +0x2000, +0x4000, +0x6000 | four board/slot config registers, probed with `peek`, type in bits 7:6 of byte 0, config bytes written at +4 and +0x10 | `startup` loop 0xfc02c683..0xfc02c709 |
| 0xfcef8000 | system enable register (16-bit); bits 0x40 and 0x80 set at startup | `startup`, `trap`, `reset_parity`, `wds_reset`, `out2_load_spkr` |
| 0xfcef9000 | diagnostic LED | `led_001`, `ledpat` |
| 0xfcefd000 | 2 KiB NVRAM, MK48T02-style, TOD registers in the last 8 bytes (0xfcefd7f8..0xfcefd7ff) | `readtod`, `resettodr`; confirmed against both NVRAM dumps (BCD date at +0x7f8) |
| 0xfcefe000 | 32-byte IDPROM | `getidprom` copies 32 bytes from 0 - 0x3102000 = 0xfcefe000 |
| 0xfe020000 | kadb (kernel debugger) entry, optional | `call_debug`, `boot`, `trap`, `kbdinput`, `zsa_xsint`, all under `boothowto & 0x40` |

Inside 0xfceb0000 the offsets follow the PC/AT map, which is what the 82380
also uses: DMA channel registers at +0x00..+0x1f and +0xc0..+0xdf, page
registers at +0x80..+0x9f, interrupt controller banks at +0x20, +0x28, +0x30,
+0x38, +0xa0, +0xa8 (`init8259` writes ICWs to all of them).

## Physical map

`kvm_init` loads the device pages with `hat_pteload`, and the page-table
entries it passes give the physical side of every window:

| Virtual | PTE loaded | Physical | Contents |
|---|---|---|---|
| 0xfcef0000 | 0xf0000003 | 0xf0000000 | slot 0 config |
| 0xfcef2000 | 0xf2000003 | 0xf2000000 | slot 1 config |
| 0xfcef4000 | 0xf4000003 | 0xf4000000 | slot 2 config |
| 0xfcef6000 | 0xf6000003 | 0xf6000000 | slot 3 config |
| 0xfcef8000 | 0xf8000003 | 0xf8000000 | system enable register |
| 0xfcef9000 | 0xf9000003, 0xfa000003 | 0xf9000000, 0xfa000000 | LED and neighbour |
| 0xfcefd000 | 0xfd000003 | 0xfd000000 | NVRAM and TOD |
| 0xfcefe000 | 0xfe000003 | 0xfe000000 | IDPROM |
| 0xfceb0000 | 0xb0000003 | 0xb0000000 | DMA and interrupt block (set in `ptinit`) |
| Weitek window | PDE 0xc0000007 | 0xc0000000 | Weitek 1167 |

With the controller and device tables that gives the whole physical side:
RAM at 0, board space at 0xa0000000, the AT block at 0xb0000000, Weitek at
0xc0000000, Ethernet at 0xd0000000, slots and on-board registers from
0xf0000000 to 0xfa000000, SCSI at 0xfb000000, on-board SCC at 0xfc000000,
NVRAM at 0xfd000000, IDPROM at 0xfe000000, boot PROM at 0xfffe0000.

## The two SCCs

Both 8530s are byte registers on a 4-byte stride, and the **channel is bit 3 of
the address**, not the low pair. `zsattach` proves it: it reads and writes with
`addr | 8` for one channel and `addr & ~8` for the other, and then stores
`md_addr | 8` in the zscom entry of channel A and `md_addr & ~8` in channel B.
So the layout of each SCC is

| offset | register |
| ------ | -------- |
| +0 | channel B control |
| +4 | channel B data |
| +8 | channel A control |
| +0xc | channel A data |

zs0 lives at 0xfc000000 (ttya on channel A, ttyb on channel B) and zs1 at
0xa0000020 (keyboard on channel A at 0xa0000028, mouse on channel B).

Getting this backwards costs the console: `consconfig` picks ttya, `zsopen`
reads RR0 of what it believes is channel A and finds no carrier (bit 3 of RR0),
sets ZS_WOPEN and sleeps forever, so `/sbin/init` never runs.

## Boot protocol

`locore` at 0xfc01b000 runs **with paging off**: it writes physical addresses
directly (`mov [0xae004], eax` for `pdir_pool`), so the kernel is loaded at
physical (virtual - 0xfc000000) and entered in 32-bit protected mode.

1. `esp` is set to 0x6800 and 8 KiB at physical 0x5000 is cleared.
2. `dx` and `bx` are saved and later combined as `(dx << 16) | bx`, which is
   the argument passed to `main`.
3. The GDT at physical 0xb0040 and the IDT at 0xb0048 are converted in place
   and loaded; TSS 0x50; `ljmp 0x60:0` after paging is enabled.
4. Memory size comes from `physmem`, and if that is zero from the PROM:
   `[[0xfffe00b8]]` then `[[0xfffe0010]]`, in bytes. If all three are zero the
   kernel panics.
5. `ptinit` identity maps the first 4 MiB into 0xfc000000, points four page
   directory entries at those tables, maps 0xfceb0000 to physical 0xb0000000
   and the u-area pages, then paging is turned on.
6. Before that, `mov eax, cr3` is used to read the page directory the PROM
   left behind: the entries for 0xfe000000 and 0xff000000 upwards are copied
   into the kernel's own directory, which is how the PROM window stays
   reachable afterwards.
7. `[0xfffe00e4]` is stored in `interupt_vectors`, then `init8259` runs.
8. `startup` checks `[0xfffe00a4]` and only patches a v86 handler through
   `[0xfffe00d0]` when that revision is 2 or more.

So an emulated machine needs a stub PROM with five data slots and a page
directory, and nothing else, to bring the kernel up.

## Devices, from the kernel's own configuration tables

`mbcinit[]` (controllers, 0x74-byte entries) and `mbdinit[]` (devices,
0x54-byte entries), decoded with `tools/mbdinit.py`:

| Driver | Physical address | Interrupt | Note |
|---|---|---|---|
| `wds` SCSI host adapter | 0xfb000000 | 0x10 | host adapter SCSI id 7; only in the 4.0.2 disk kernel |
| `fdc` floppy | ports 0x3f2/0x3f4/0x3f5/0x3f7 | 6 | 8272-compatible, the only true port-I/O device in the kernel |
| `ie` Ethernet (Intel 82586) | 0xd0000000 | 0x15 | control register at IDPROM page 0xfcefe002 in `ieattach` |
| `cgthree` colour frame buffer | 0xa0400000 | - | |
| `bwtwo` mono frame buffer | 0xa0200000 | - | |
| `zs` serial (Z8530) | 0xfc000000 | 9 | on-board RS232 |
| `zs` keyboard/mouse (Z8530) | 0xa0000020 | 9 | on the frame buffer board |
| `pp` parallel | port 0x378 | 7 | |

The 4.0.1 `/boot/vmunix` has the same table minus `wds`: it is the floppy and
network kernel used to install, so a first emulation run needs no SCSI at all.

## IDPROM the machine has to present

`setcputype` reads the 32-byte IDPROM and requires:

- byte 0 (format) = 1, otherwise `INVALID FORMAT TYPE IN ID PROM`;
- byte 1 (machine type) = **0x31** for the Sun386i. Values 0x11, 0x12, 0x13,
  0x14, 0x17, 0x18 are Sun-3 types and panic. Type 0x31 sets `cpudelay` = 2;
- bytes 2..7 are the Ethernet address (`ieattach` reads 0xfcefe002).

It then reads three NVRAM bytes: 0xfcefd111 `artwork`, 0xfcefd112
`hardware_rev`, 0xfcefd494 `bootmsg_level`. Both real NVRAM dumps carry
artwork = 3, hardware_rev = 0, bootmsg_level = 2, which is the combination the
kernel treats as a working floppy change line.

## Boot PROM surface

The PROM lives at 0xfffe0000 (128 KiB, matching the Intel D27010 the machines
carry) and is reached through a vector table at that address. Both standalone
programs, the Recovery/Install floppy and the diagnostics floppy, use the same
six slots and nothing else:

| Slot | Use |
|---|---|
| +0x0c | pointer read during init |
| +0x18 | putchar; called from the printf path |
| +0x84 | called by diag only |
| +0x98 | called by the install standalone only |
| +0xb8 | pointer read |
| +0xc4 | abort to the monitor, called after a fatal message |
| +0xf4 | pointer read, four call sites |

The kernel itself never calls the PROM. Its only reference to that area is the
kadb window at 0xfe020000, guarded by `boothowto & 0x40` and by a `peek` that
tolerates a fault, so a machine with no PROM boots.

## Media

43 floppy images of 1,474,560 bytes: `app-01..02` Recovery/Install (bootable,
32-bit code at sector 1), `app-03..13` Core System, `app-14..20` and
`dev-01..07` optional clusters, `diag` bootable diagnostics, `upg-01..07` and
`diskette.1..8` the 4.0.2 upgrade kit. Format is Sun `bar`: 512-byte volume
header, then 512-byte file headers with the name at offset 84 and the size as
octal at offset 24, each file compressed with `compress`. `tools/barx.py`
extracts the lot, including files that span volumes: 3,013 entries from the
installation kit, 374 from the upgrade.
