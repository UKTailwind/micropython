# The CMM2's start-up, as the Pico Computer 3's _boot_board.py: the most-used modules and functions put in the REPL's
# namespace, the HDMI module's display brought up (the saved mode, default RGB640) and the on-screen console started.
# What the CMM2's firmware does itself is left out: the SD card and the USB drive (mounted by main.c), the clock (its
# own), the keyboard's layout (OPTION KEYBOARD); and what it has not: Wi-Fi, USB serial adapters, a DS3231 (ds3231 can
# still be imported, for one on I2C).
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

import board

__main__.board = board

# The clock, as the PC3's DS3231 helpers: settime()/gettime()/synctime() (modules/ds3231.py: the CMM2's DS3231 through
# the firmware's DATE$ and TIME$).
import ds3231

__main__.settime = ds3231.settime
__main__.gettime = ds3231.gettime
__main__.synctime = ds3231.synctime

# Wi-Fi + NTP time, as the PC3's: wifi()/ntpsync()/tz()/auto() (pcnet, unchanged, on the network module: the
# firmware's Wi-Fi). auto() too, which the PC3 book uses at the prompt; its boot_sync runs at the end, below.
import pcnet

__main__.wifi = pcnet.wifi
__main__.ntpsync = pcnet.ntpsync
__main__.tz = pcnet.tz
__main__.auto = pcnet.auto

# On-screen console: console() mirrors REPL output to the HDMI screen.
import pcconsole

__main__.console = pcconsole.console

# Settings (HDMI mode, palette) in A:/settings.json.
import keyboard
import pcconfig

__main__.keymap = pcconfig.keymap
__main__.keymaps = keyboard.keymaps
__main__.keydown = keyboard.keydown
__main__.screen = pcconfig.screen
__main__.palette = pcconfig.palette
__main__.numlock = pcconfig.numlock

# Audio: WAV/MP3/FLAC/MOD playback, tones and the 4-voice synth, through the CMM2's audio out.
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

import touch as _touch

__main__.touch = _touch.query

import mouse as _mouse

__main__.mouse = _mouse.query
__main__.mouse_speed = _mouse.speed

import gamepad as _gamepad


class _GamepadProxy:
    def __call__(self, code, chan=0):
        return _gamepad.query(code, chan)

    def __getattr__(self, name):
        return getattr(_gamepad, name)


__main__.gamepad = _GamepadProxy()

import pcimage

__main__.draw_jpg = pcimage.draw_jpg
__main__.draw_bmp = pcimage.draw_bmp
__main__.draw_png = pcimage.draw_png
__main__.save_image = pcimage.save_image
__main__.load_image = pcimage.load_image

import pcturtle

__main__.Turtle = pcturtle.Turtle

import pcgui

__main__.pcgui = pcgui

import pccursor

__main__.pccursor = pccursor

import pctilemap

__main__.TileMap = pctilemap.TileMap

import pcmath

__main__.pcmath = pcmath

import pcgame

__main__.pcgame = pcgame

import pcplot

__main__.plot = pcplot.plot

import pcfm

__main__.fm = pcfm.fm

# The display (the saved mode, default 640x480 in 256 colours) and the console on it.
try:
    hdmi.init(pcconfig.get("hdmi_mode", hdmi.RGB640))
except Exception:
    hdmi.init(hdmi.RGB640)
try:
    pcconfig.apply_palette()
except Exception:
    pass
pcconsole.console()

# NTP at start-up, as the PC3's: only with auto() on and Wi-Fi credentials saved (pcnet)
pcnet.boot_sync()

del _name
