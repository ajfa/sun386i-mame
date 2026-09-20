# What the machine turned out to be doing

Everything here was measured on the running system or read out of its own
binaries. Where a guess is still a guess, it says so.

## How SunOS enters virtual 8086 mode

There is no system call in the way. The kernel maps the process **its own task
state segment at user address 0x110000** (`mapxtss`, and `check_xtss` reads
that address back with `fuibyte`), leaves the nested task flag set and the back
link pointing at it. The DOS emulator then crosses with one instruction:

    0x120501  mov eax, esp
    0x120503  iretd            ; IRET with NT set is a task return
    0x120504  mov esp, eax

The `mov eax,esp` before and `mov esp,eax` after are there because a task
switch saves and reloads `esp` from the TSS. That is what made it fast in 1988.

`gpv86trap` is **not** the way in: it handles the faults that happen once the
task is already in virtual 8086 mode. It opens with
`test dword ptr [esp+0xc], 0x20000`, which is the VM bit of the trapped flags,
then reads the faulting byte at `CS<<4 + EIP` and decodes it. `notv86` leaves
with `ljmp 0x60:0`, a jump to a TSS selector.

In the kernel's descriptor template at `gdt`, selectors 0x50, 0x58, 0x60 and
0x90 are 104 byte task state segments, 0x68 and 0x70 are ring 0 code and data,
0x78 and 0x80 the ring 3 pair, and **0x88 is the task state segment based at
0x110000**: the one mapped into the process.

## How the DOS side talks to SunOS

Four separate mechanisms, and all four are visible from the outside.

**Drives.** From `AUTOEXEC.BAT` on the private C: drive:

    redir
    extend d: h:~ r:
    path h:\pc;r:\etc\dos\msdos;r:\etc\dos\unix;c:\
    d:

`R:` is the Unix root, `H:` the home directory, `D:` the directory the user was
in when they typed `dos`, which is why a fresh DOS window opens at `D:\>`. Long
Unix names are folded into 8.3 with a tilde, seven years before Windows 95 did
the same thing. `C:` is a 21 MB FAT image with a partition table, held as a
sparse file in the user's home.

**Devices**, in `/etc/dos/defaults/setup.pc`, mapping DOS devices to Unix paths
and to Unix *commands*:

    A     /etc/dos/defaults/diskette_a     -> /dev/rfd0c
    C     ~/pc/C:
    COM1  /etc/dos/defaults/com1           -> /dev/ttya
    LPT1  lpr
    LPT2  cat >>~/lpt-2
    LPT3  psfx80 | lpr

Printing from DOS is a pipe into a Unix command.

**Unix commands from the DOS prompt.** `/usr/dos/unix` holds 65 `.com` files,
every one of them a symbolic link to `unix.com`, which reads the name it was
invoked as out of the environment block of its own PSP and hands that to the
Unix side. Type `ls` at the DOS prompt and the SunOS `ls` answers.

**The door is an I/O port that does not exist on a PC.** `QUIT.COM` is sixteen
bytes:

    mov ah, 0x0d
    int 0x21            ; flush the DOS buffers
    mov ax, 0x0011
    mov dx, 0xfce0
    out dx, ax          ; the crossing
    int 0x20

In virtual 8086 mode that OUT faults, `gpv86trap` decodes it and takes the
function number out of AL. `unix.com` uses function 9, and builds a shell
pipeline with temporary files, optionally wrapped in a `cmdtool` window.

## The desktop is started by the user, not by the system

Logging in as root on the screen stops at "One moment please" and nothing
paints. That is not a fault. The window system is started by **the user's own
`.login`**, from the skeleton the machine keeps for new accounts:

    setenv TTY `tty`

    if ( $TTY == /dev/console && -e /dev/fb ) then
            sunview -color 166 208 250
            logout

Root's `.login` is four lines that clear the screen, set the terminal and look
for mail. An account made by hand with an empty home has no `.login` at all and
behaves exactly like root, which is what showed that the account was never the
variable. The blue background of a Sun386i desktop is those three numbers.

## The frame buffers

The monochrome board is a bwtwo at 0xa0200000, 1152x900, one bit per pixel.
Two things about it are not what a Sun programmer would expect, and both were
settled by looking at what the window system itself draws:

- **The leftmost pixel is in the low bit of the byte**, not the high bit. With
  the usual Sun order the text comes out mirrored inside each group of eight.
  It is consistent with the machine being an x86.
- **A set bit is ink.** The kernel fills the screen with ones and writes the
  letters as zeros.

The colour board is a cgthree. `cgthreeprobe` identifies it by reading one byte
at physical **0xf7000000**: 0x80 means 1152x900, 0x81 means 1024x768, anything
else means no board. The frame buffer is a megabyte at **0xa0400000**, one byte
per pixel, through a Brooktree style palette: the index goes to 0xa0000010 and
then three colour bytes to 0xa0000014, which advances the index by itself. The
read mask is at 0xa0000018.

Romvec slot **+0xa0** is `v_fbtype` and says which frame buffer is the console:
`consconfig` compares it against 2 (bwtwo), 3 (cgtwo), 6 (cgthree) and 8
(cgfour).

## The diskette change line depends on the NVRAM

`setcputype` decides whether to trust the change line from what it finds in the
NVRAM. With the contents of a real 386i/150 it reads `artwork 3`, leaves
`change_line_broke` at 0 and takes port 0x3f7 at face value. With a blank NVRAM
it reads 255, sets the flag, and inverts every reading, and then the emulation
has to invert too or the kernel never issues a read at all. Regenerating the
PROM stub without the NVRAM image changes this silently.

## DMA chaining ends when nothing was reloaded

The kernel disarms and rearms the chaining interrupt on **every page it loads**,
so a disarm is not the end of a transfer. It is the end only when the base
registers are still empty from the last reload. And end of count has to reach
the floppy controller on the **last byte the read command can deliver**: the
kernel's own disarm arrives by interrupt, by which time the head has passed the
last sector and the controller answers 40 80, abnormal termination.
