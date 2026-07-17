# Pico Computer 3 emulator boot -- the _boot_board.py of the emulator.
# Run via the pc3emu launcher:  micropython -X heapsize=8m -i -c "import emuboot"
#
# Re-roots the VFS so the directory tree looks exactly like the machine's:
# "/" is the flash (a host directory), "/sd" the SD card (another one).
# Then injects the same REPL names _boot_board.py injects on hardware.

import os
import sys
import vfs

# --- filesystem: <home>/flash becomes /, <home>/sd becomes /sd ------------
_home = os.getenv("PC3EMU_HOME")
if not _home:
    _home = (os.getenv("HOME") or os.getenv("USERPROFILE") or ".") + "/.pc3emu"


def _ensure(path):
    parts = path.split("/")
    p = ""
    for part in parts:
        if not part:
            continue
        p = p + "/" + part if (p or path.startswith("/")) else part
        try:
            os.mkdir(p)
        except OSError:
            pass


_ensure(_home + "/flash")
_ensure(_home + "/sd")

_flash = vfs.VfsPosix(_home + "/flash")
_sd = vfs.VfsPosix(_home + "/sd")
vfs.umount("/")
vfs.mount(_flash, "/")
vfs.mount(_sd, "/sd")
os.chdir("/")

# --- the REPL namespace, exactly as _boot_board.py builds it ---------------
import __main__
import machine
import framebuf
import hdmi
import pcshell

__main__.os = os
__main__.machine = machine
__main__.Pin = machine.Pin
__main__.hdmi = hdmi
__main__.framebuf = framebuf
for _name in pcshell.COMMANDS:
    setattr(__main__, _name, getattr(pcshell, _name))

# Graphics: Display class + the named colour palette (RED, WHITE, ...).
import pcgfx

__main__.Display = pcgfx.Display
for _name in dir(pcgfx):
    if _name.isupper():
        setattr(__main__, _name, getattr(pcgfx, _name))

import keyboard
import pcconfig

__main__.keymap = pcconfig.keymap
__main__.keymaps = keyboard.keymaps
__main__.keydown = keyboard.keydown
__main__.screen = pcconfig.screen
try:
    keyboard.keymap(pcconfig.get("keymap", "US"))
except Exception:
    pass

# Real display build: open the window at boot (saved mode, as the machine
# does), put the on-screen console in it, and route keyboard input from the
# window into the REPL alongside the launching terminal.
if hasattr(hdmi, "blit"):
    import sys
    import io
    import select
    import _emukbd
    import pcconsole

    try:
        hdmi.init(pcconfig.get("hdmi_mode", hdmi.RGB640),
                  pcconfig.get("hdmi_clock", 252))
    except Exception:
        hdmi.init(hdmi.RGB640)

    class _EmuTerm(io.IOBase):
        # The dupterm stream: write mirrors to the on-screen console (when one
        # is up); read BLOCKS, serving the window's keyboard (_emukbd, fed by
        # the firmware's own decoder) and the launching terminal together --
        # so it never falls back to a terminal-only blocking read that would
        # starve window input.
        def __init__(self, con):
            self.con = con
            self._poll = select.poll()
            self._poll.register(sys.stdin, select.POLLIN)

        def write(self, buf):
            if self.con is not None:
                self.con.write(buf)
            return len(buf)

        def read(self, n=1):
            while True:
                b = _emukbd.read()
                if b is not None:
                    return b
                if self._poll.poll(20):
                    c = sys.stdin.buffer.read(1)
                    return c if c else b"\x04"  # terminal EOF -> Ctrl-D

        def readinto(self, buf):
            b = self.read(1)
            if not b:
                return None
            buf[0] = b[0]
            return 1

    _orig_console = pcconsole.console

    def _console(target=True, fg=0xFFFFFF, bg=0x000000):
        con = _orig_console(target, fg, bg)
        os.dupterm(_EmuTerm(con))  # keep window keyboard input on ANY target
        return con

    pcconsole.console = _console
    __main__.console = _console
    _console("both")

print("Pico Computer 3 emulator")
print("flash -> %s/flash   sd -> %s/sd" % (_home, _home))
if hasattr(hdmi, "blit"):
    print("display: window open, console 'both' -- type in the window or here")
    print("exit: Ctrl-D at the prompt (or machine.reset())")
else:
    print("graphics: not built (terminal only) -- install libsdl2-dev and rebuild")
