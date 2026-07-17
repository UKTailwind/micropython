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

print("Pico Computer 3 emulator")
print("flash -> %s/flash   sd -> %s/sd" % (_home, _home))
if hasattr(hdmi, "blit"):
    print("graphics: real hdmi + SDL -- hdmi.init() opens the display window,")
    print("          hdmi.test() shows the colour bars")
else:
    print("graphics: not built (terminal only) -- install libsdl2-dev and rebuild")
