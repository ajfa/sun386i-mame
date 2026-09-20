#!/bin/sh
# Run the machine with a window, for looking at it.
#
#   SUN386I_DISK=sd.chd SUN386I_KERNEL=vmunix tools/run.sh
#
# The kernel is handed to the emulator with -quickload because the stub PROM
# has no disk bootstrap of its own: it sets the boot parameter block and the
# romvec, and the loader puts the COFF sections where the kernel expects them.
set -e
: "${SUN386I_DISK:?set SUN386I_DISK to a disk image}"
: "${SUN386I_KERNEL:?set SUN386I_KERNEL to the matching vmunix}"
: "${MAME:=./sun386i}"

exec "$MAME" sun386i \
    -window -nomaximize \
    -quickload "$SUN386I_KERNEL" \
    -hard "$SUN386I_DISK" \
    ${SUN386I_FLOPPY:+-flop1 "$SUN386I_FLOPPY"} \
    -ttya pty \
    "$@"
