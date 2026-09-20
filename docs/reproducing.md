# Getting from the diskettes to a running system

None of the SunOS media is here. What follows assumes you have the original
Sun386i diskette set as raw images.

## The disk

The installer expects the geometry in its own `/etc/format.dat`:

    chdman createhd -chs 1022,5,35 -ss 512 -o sd.chd

That is a CDC Wren III hh. The label the installer writes puts partition a at
cylinder 0, g at 60, b (swap) at 318 and h at 498, with 5 heads and 35 sectors,
so 175 blocks to the cylinder. `tools/label.py` reads it back.

## The installation

Boot the first application diskette. The kernel on it autoconfigures the
machine and `/etc/Install` comes up on the serial console. The installer walks
diskettes 3 and 4 for the root file system, 5 to 12 for /usr and 13 for /files,
which is what their volume headers say they hold. Diskettes 14 to 20 are the
Applications Supplement and the Developer's Toolkit is a set of its own; the
installer asks for each by name, so a driving script has to answer with the
right set.

Diskettes are changed while the machine runs. The emulator side of that is a
Lua script that loads an image on request and answers when it has; the driving
script must **wait for that answer before confirming**, not confirm on a timer.

## The first boot

Two things are worth knowing.

The boot parameter block says `sd` unit 2 when there is no diskette in the
drive and `fd` when there is. That is patched over the PROM image at reset, and
it is why an upgrade, which runs with a diskette in the drive, needs to be told
to boot from the disk anyway.

The first time round the machine's setup assistant gives root a password nobody
chose. It can be cleared by editing `/etc/passwd` inside the disk image with
`tools/ffs.py`, keeping the file the same length, and rebuilding the image.

## Upgrading 4.0.1 to 4.0.2

From single user, following the instructions in the kit:

    fsck -y -w
    mount -at 4.2
    mount -at lo
    cd /
    bar xvpfZT /dev/rfd0c ./tmp
    cd /tmp
    install_update

Single user means there is no getty on the serial line, so the console has to
be moved there for the run. The driver reads three environment variables that
exist only for this: one moves romvec slots 0x28 and 0x2c to the byte meaning
ttya, one writes `-s` into argv[1] of the boot parameters, and one forces the
boot device to the disk even with a diskette in the drive.

**Each disk carries its own kernel.** Booting the upgraded disk with the 4.0.1
kernel gets as far as mounting root and then wanders off reading unmapped
addresses, with no clear error. Take `/vmunix` off the upgraded image and use
that.

## A trap in the media

If a volume of a multi volume `bar` set is not exactly 1.44 MB, do not pad it
to make the emulator accept it. A continuation volume records in its own header
how many bytes of the split file are still to come, so the seam can be checked:
add what the previous image holds to that figure and compare with the size the
file header declares. If it comes up short by a round number of blocks, the
image is a truncated dump and the missing bytes are real data. Padding puts
filler into the middle of a compressed stream and the extraction fails with
`bar warning: cannot uncompress`.
