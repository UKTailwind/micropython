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

# The graphics/game stack, injected as _boot_board.py does on the machine.
import pcturtle
import pctilemap
import pcgame
import pcgui
import pccursor
import pcplot
import pcimage
import mouse as _mouse
import touch as _touch
import gamepad as _gamepad

__main__.Turtle = pcturtle.Turtle
__main__.TileMap = pctilemap.TileMap
__main__.pcgame = pcgame
__main__.pcgui = pcgui
__main__.pccursor = pccursor
__main__.plot = pcplot.plot
__main__.draw_jpg = pcimage.draw_jpg
__main__.draw_bmp = pcimage.draw_bmp
__main__.draw_png = pcimage.draw_png
__main__.save_image = pcimage.save_image
__main__.load_image = pcimage.load_image
__main__.mouse = _mouse.query
__main__.mouse_speed = _mouse.speed
__main__.touch = _touch.query


class _GamepadProxy:
    def __call__(self, code, chan=0):
        return _gamepad.query(code, chan)

    def __getattr__(self, name):
        return getattr(_gamepad, name)


__main__.gamepad = _GamepadProxy()

# Full-screen file manager.
import pcfm

__main__.fm = pcfm.fm

# DS3231 real-time clock (register-level emulation over the PC clock).
import ds3231

__main__.settime = ds3231.settime
__main__.gettime = ds3231.gettime
__main__.synctime = ds3231.synctime
try:
    ds3231.synctime()
except Exception:
    pass

# Maths helpers over ulab, when ulab is in the build.
try:
    import pcmath

    __main__.pcmath = pcmath
except ImportError:
    pass

# Audio: playback, tone generator and 4-voice synth (SDL out when built).
try:
    import pcaudio

    __main__.play = pcaudio.play
    __main__.volume = pcaudio.volume
    __main__.beep = pcaudio.beep
    __main__.stop = pcaudio.stop
    __main__.is_playing = pcaudio.is_playing
    __main__.tone = pcaudio.tone
    __main__.sound = pcaudio.sound
    __main__.mod_sample = pcaudio.mod_sample
    __main__.pause = pcaudio.pause
    __main__.resume = pcaudio.resume
except ImportError:
    pass  # audio C modules not in this build

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

    # fd 0 becomes the machine console: a pipe merging the window keyboard
    # and the launching terminal, so EVERYTHING that reads stdin -- the REPL,
    # autosave(), pye -- sees both, exactly as sys.stdin is the one console
    # on the machine. (Tests set PC3EMU_TEST to keep the ring buffer
    # inspectable instead.)
    if not os.getenv("PC3EMU_TEST"):
        _emukbd.console_pipe()

    _stdin_poll = select.poll()
    _stdin_poll.register(sys.stdin, select.POLLIN)

    class _EmuTerm(io.IOBase):
        # The dupterm stream: write mirrors to the on-screen console (when
        # one is up); read serves the merged console on fd 0 through a poll
        # loop, so waiting for a key keeps pumping events -- audio callbacks,
        # timers -- exactly as the machine's stdin wait loop does.
        def __init__(self, con):
            self.con = con

        def write(self, buf):
            if self.con is not None:
                self.con.write(buf)
            return len(buf)

        def read(self, n=1):
            # 5 ms slices: each timeout re-enters the VM, which pumps the
            # event hook -- pcaudio's refill callbacks need ~4 kB served
            # every 23 ms while music plays at an idle prompt.
            while True:
                if _stdin_poll.poll(5):
                    return sys.stdin.buffer.read(1)

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
    print("exit: Ctrl-D at the prompt; paste into the window: Ctrl-V")
else:
    print("graphics: not built (terminal only) -- install libsdl2-dev and rebuild")
