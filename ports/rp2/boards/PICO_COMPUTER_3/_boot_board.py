# Pico Computer 3 board boot hook. Frozen into the firmware and picked up by the
# shared rp2 _boot.py via its generic `import _boot_board` hook, so no PC3-specific
# code lives in the port's shared _boot.py.
#
# It pre-imports the most-used modules into the REPL (__main__) namespace so they
# are available without a manual import, mounts the SD card with hot-swap polling,
# sets the clock from the DS3231 RTC, and brings up the HDMI display + on-screen
# console. _boot runs in its own module namespace, so names are assigned onto
# __main__ explicitly.
import os
import machine
import __main__
import pcshell
import hdmi
import framebuf

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

# Board identity. One firmware image runs on both machines in the family: the
# Pico Computer 3, and the Pico Computer 2 (no Wi-Fi/Bluetooth, LED on GP25, SD
# card on different, bit-banged pins). The C side has already detected which
# this is -- everything below that differs between them asks `board`.
import board

__main__.board = board

# LED, when it is a real GPIO (Pico Computer 2). On the Pico Computer 3 the LED
# hangs off CYW43 GPIO0: building that Pin here would power up the radio on
# every boot, so it is left to the user as Pin("LED", Pin.OUT).
_led = board.led_pin()
if _led is not None:
    __main__.LED = machine.Pin(_led, machine.Pin.OUT, value=0)
del _led

# SD card: mount /sd (if a card is present) and start the background hot-swap
# poll so cards can be inserted/removed while running.
import pcsd

pcsd.start()

# On-screen console: console() mirrors REPL output to the HDMI screen.
import pcconsole

__main__.console = pcconsole.console

# Persistent settings (keyboard layout, HDMI mode/clock) on the flash FS.
import keyboard
import pcconfig

# keymap("UK")/screen(...) apply AND persist; keymaps() just lists.
__main__.keymap = pcconfig.keymap
__main__.keymaps = keyboard.keymaps
# keydown(n): which keys are held right now (MMBasic KEYDOWN) — for games etc.
__main__.keydown = keyboard.keydown
__main__.screen = pcconfig.screen
__main__.palette = pcconfig.palette
try:
    keyboard.keymap(pcconfig.get("keymap", "US"))  # apply saved layout
except Exception:
    pass

# Audio: WAV/MP3/FLAC/MOD playback, tone generator and 4-voice synth over the
# PCM5102 I2S DAC.
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
# Play a short sound when a USB device is plugged in / removed.
keyboard.on_usb_event(pcaudio.system_sound)

# USB multi-touch: touch("X"), touch("SWIPE"), touch("TAP"), ... See the `touch`
# module for the full list of subcommands (incl. gestures).
import touch as _touch

__main__.touch = _touch.query

# USB mouse: mouse("X"), mouse("L"), mouse("W"), ... (MMBasic DEVICE(MOUSE)).
import mouse as _mouse

__main__.mouse = _mouse.query
__main__.mouse_speed = _mouse.speed

# USB gamepad: gamepad("LX"), gamepad("B") & gamepad.A, ... (MMBasic DEVICE(GAMEPAD)).
# Wrap the C module so the one name `gamepad` is both callable (a reader, like
# mouse()) AND carries the button-bit constants and configure()/mask().
import gamepad as _gamepad


class _GamepadProxy:
    def __call__(self, code, chan=0):
        return _gamepad.query(code, chan)

    def __getattr__(self, name):
        return getattr(_gamepad, name)


__main__.gamepad = _GamepadProxy()

# USB serial (CDC host): a USB-serial adapter plugged into the host port is a
# UART-like USBSerial object -- USBSerial(115200); .read()/.write()/.any().
import usbserial

__main__.USBSerial = usbserial.USBSerial

# XMODEM file transfer over the serial console: xrecv("/sd/prog.py") then start
# an XMODEM *send* in the terminal; xsend("/sd/prog.py") then an XMODEM *receive*.
import xmodem

__main__.xrecv = xmodem.recv
__main__.xsend = xmodem.send

# DS3231 hardware RTC: set the system clock from it so the time is right at boot.
# settime()/gettime()/synctime() are exposed for the user.
import ds3231

__main__.settime = ds3231.settime
__main__.gettime = ds3231.gettime
__main__.synctime = ds3231.synctime

# Image loaders: draw_jpg()/draw_bmp()/draw_png() decode onto the HDMI screen.
import pcimage

__main__.draw_jpg = pcimage.draw_jpg
__main__.draw_bmp = pcimage.draw_bmp
__main__.draw_png = pcimage.draw_png
__main__.save_image = pcimage.save_image
__main__.load_image = pcimage.load_image  # image file -> in-memory blit surface

# Turtle graphics: Turtle() draws on the HDMI screen (MMBasic TURTLE).
import pcturtle

__main__.Turtle = pcturtle.Turtle

# On-screen GUI toolkit (MMBasic GUI controls): pcgui.GUI() + control factories.
import pcgui

__main__.pcgui = pcgui

# Mouse pointer overlay (MMBasic GUI CURSOR): pccursor.on()/refresh()/off().
# (Already imported by pcgui; exposing it costs nothing extra.)
import pccursor

__main__.pccursor = pccursor

# Tile maps (MMBasic TILEMAP): TileMap() drawn via the C hdmi.tilemap() helper.
import pctilemap

__main__.TileMap = pctilemap.TileMap

# Maths helpers over ulab (quaternions, vectors, DSP, correl/chi, PID). ulab
# itself (import ulab.numpy as np) covers stats/linalg/FFT/complex.
import pcmath

__main__.pcmath = pcmath

# Game-loop timing (MMBasic SYNC): pcgame.Clock(fps) for drift-free frame timing.
import pcgame

__main__.pcgame = pcgame

# Simple plotting for education: plot(data) / plot(function, range).
import pcplot

__main__.plot = pcplot.plot

# Full-screen file manager (MMBasic FM): fm() browses/runs/plays/views files.
import pcfm

__main__.fm = pcfm.fm

try:
    ds3231.synctime()
except Exception:
    pass  # no DS3231 / dead battery: leave the default clock

# Wi-Fi + NTP time: wifi()/ntpsync()/tz()/auto(). The DS3231 sync above already
# gave a valid time; the optional NTP refresh (boot_sync) runs at the very end,
# after the console is up, so its connect messages are visible.
import pcnet

__main__.wifi = pcnet.wifi
__main__.ntpsync = pcnet.ntpsync
__main__.tz = pcnet.tz

# Bring up the HDMI display (saved mode/clock, default 640x480x8 @ 252) and start
# the on-screen console. A bad saved value falls back safely.
try:
    hdmi.init(pcconfig.get("hdmi_mode", hdmi.RGB640), pcconfig.get("hdmi_clock", 252))
except Exception:
    hdmi.init(hdmi.RGB640)
try:
    pcconfig.apply_palette()  # restore a saved RGB1024 palette (no-op if none)
except Exception:
    pass
pcconsole.console()

# Optional NTP time sync (only if auto() is enabled and credentials are saved).
# Last, so the display/console is already up and the connect messages show; all
# failures are swallowed, so a missing network never blocks boot.
pcnet.boot_sync()

del _name
