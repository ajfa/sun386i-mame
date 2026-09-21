#!/usr/bin/env python3
"""Drive the machine over its serial line, with no window and no display.

The emulator is started headless, the serial line comes out on a pseudo
terminal whose name the driver prints into error.log, and this script logs in
and types a list of commands, waiting for the prompt after each one.

    SUN386I_DISK=sd.chd SUN386I_KERNEL=vmunix SUN386I_CMDS=steps.txt \\
        python3 tools/console.py

Environment:
    SUN386I_DISK     disk image                          (required)
    SUN386I_KERNEL   the vmunix that belongs to it       (required)
    SUN386I_CMDS     file of commands, one per line      (required)
    SUN386I_RUN      working directory                   (default ./run)
    SUN386I_MAME     the emulator                        (default ./sun386i)
    SUN386I_LUA      script for snapshots                (optional)
    SUN386I_USER     who to log in as                    (default root)
    SUN386I_SNAP     seconds between snapshots           (default 25)
    SUN386I_SHOTS    how many to take                    (default 60)

Once a session is open the line runs at seven bits with even parity, so
everything typed after the login carries the parity in the eighth bit.
"""
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import termios
import time

RUN = os.environ.get("SUN386I_RUN", "run")
MAME = os.environ.get("SUN386I_MAME", "./sun386i")
USER = os.environ.get("SUN386I_USER", "root")
PROMPT = os.environ.get("SUN386I_PROMPT", "SUPERUSER")
BOOT_TIMEOUT = int(os.environ.get("SUN386I_BOOT_TIMEOUT", "900"))
STEP_TIMEOUT = int(os.environ.get("SUN386I_STEP_TIMEOUT", "600"))


def need(name):
    v = os.environ.get(name)
    if not v:
        sys.exit("%s is not set" % name)
    return os.path.abspath(v)


def setup():
    if os.path.exists(RUN):
        shutil.rmtree(RUN)
    for d in ("cfg", "nvram", "snap", "diff"):
        os.makedirs(os.path.join(RUN, d))
    here = os.path.dirname(os.path.abspath(MAME))
    for d in ("plugins", "roms", "hash", "artwork", "language", "bgfx",
              "ctrlr", "keymaps", "samples"):
        src = os.path.join(here, d)
        if os.path.isdir(src):
            os.symlink(src, os.path.join(RUN, d))
    os.symlink(os.path.abspath(MAME), os.path.join(RUN, "sun386i"))


def start(disk, kernel):
    env = dict(os.environ)
    env["SDL_AUDIODRIVER"] = "dummy"
    env.setdefault("SUN386I_SNAP", "25")
    env.setdefault("SUN386I_SHOTS", "60")

    window = os.environ.get("SUN386I_WINDOW") and os.environ.get("DISPLAY")
    if window:
        # a window to watch it in, never full screen
        video = ["-window", "-nomaximize"]
    else:
        # no window at all: -video none on its own still brings one up through
        # SDL, so the display has to go out of the environment as well
        for v in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE"):
            env.pop(v, None)
        env["SDL_VIDEODRIVER"] = "dummy"
        video = ["-video", "none"]

    # only the colour screen: the mono one stays blank all the way
    video += ["-view", "Screen 1 Pixel Aspect"]
    cmd = ["./sun386i", "sun386i"] + video + ["-sound", "none",
           "-quickload", kernel, "-hard", disk,
           "-ttya", "pty", "-nothrottle", "-log", "-skip_gameinfo"]
    lua = os.environ.get("SUN386I_LUA")
    if lua:
        cmd += ["-autoboot_script", os.path.abspath(lua), "-autoboot_delay", "0"]
    return subprocess.Popen(cmd, cwd=RUN, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class Line:
    def __init__(self, fd, path):
        self.fd = fd
        self.out = open(path, "w", errors="replace")
        self.text = ""

    def pump(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        data = os.read(self.fd, 4096)
        if not data:
            return False
        s = "".join(chr(b & 0x7f) for b in data)
        self.text += s
        self.out.write(s)
        self.out.flush()
        return True

    def wait(self, needle, start, timeout):
        end = time.time() + timeout
        while time.time() < end:
            at = self.text.find(needle, start)
            if at >= 0:
                return at
            self.pump(1.0)
        return -1

    def type(self, s, parity=False):
        for ch in s + chr(13):
            b = ord(ch) & 0x7f
            if parity and bin(b).count("1") % 2:
                b |= 0x80
            os.write(self.fd, bytes([b]))
            time.sleep(0.12)


def pty_name(log, timeout):
    end = time.time() + timeout
    while time.time() < end:
        try:
            with open(log, errors="replace") as f:
                m = re.search(r"ttya on (\S+)", f.read())
            if m:
                return m.group(1)
        except FileNotFoundError:
            pass
        time.sleep(0.5)
    return None


def main():
    disk, kernel = need("SUN386I_DISK"), need("SUN386I_KERNEL")
    with open(need("SUN386I_CMDS")) as f:
        commands = [l.rstrip("\n") for l in f
                    if l.strip() and not l.startswith("#")]

    setup()
    proc = start(disk, kernel)
    try:
        path = pty_name(os.path.join(RUN, "error.log"), 120)
        if not path:
            print("FAIL: the emulator never reported its serial line")
            return 1
        print("serial line on", path, flush=True)

        fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(fd)
        a[0] = a[1] = a[3] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, a)
        line = Line(fd, os.path.join(RUN, "console.log"))

        if line.wait("login:", 0, BOOT_TIMEOUT) < 0:
            print("FAIL: no login prompt", flush=True)
            return 1
        time.sleep(1.0)
        line.type(USER)
        # a .login that runs tset on an unknown terminal stops to ask for the
        # terminal type, and nobody is there to press Return
        at, asked, end = -1, False, time.time() + 300
        while time.time() < end:
            at = line.text.find(PROMPT)
            if at >= 0:
                break
            if not asked and "TERM = (" in line.text:
                asked = True
                time.sleep(1.0)
                line.type("", parity=True)
            line.pump(1.0)
        if at < 0:
            print("FAIL: could not log in", flush=True)
            print(line.text[-600:], flush=True)
            return 1
        print("logged in as %s" % USER, flush=True)

        for cmd in commands:
            # a marker rather than a command: tell whatever is driving the
            # screen that the machine is up, so it does not have to guess at
            # how long this host takes to boot
            if cmd == "@ready":
                with open(os.path.join(RUN, "ready"), "w") as f:
                    f.write("go\n")
                print("ok: the screen side can go ahead now", flush=True)
                continue
            mark = len(line.text)
            time.sleep(1.0)
            line.type(cmd, parity=True)
            if line.wait(PROMPT, mark + len(cmd), STEP_TIMEOUT) < 0:
                print("FAIL: %r gave no prompt back" % cmd, flush=True)
                print(line.text[-600:], flush=True)
                return 1
            print("ok: %s" % cmd, flush=True)

        mark = len(line.text)
        time.sleep(1.0)
        line.type("/etc/halt", parity=True)
        line.wait("halt", mark, 240)
        for _ in range(12):
            line.pump(1.0)
        print("---- the tail of the session ----", flush=True)
        print(line.text[-2000:], flush=True)
        return 0
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()


sys.exit(main())
