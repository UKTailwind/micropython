# Automatic checks for the smaller subsystems: RTC, settings, shell helpers,
# input-device queries, audio state machine (no listening required — the
# audible test is in test_audio.py).

import testutil as T

T.quiet()

T.section("rtc")
import ds3231

try:
    t = ds3231.gettime()
    T.check(2025 <= t[0] <= 2100, "gettime() year plausible (%d)" % t[0])
    T.check(1 <= t[1] <= 12 and 1 <= t[2] <= 31, "gettime() date fields")
except OSError:
    print("  skip: no DS3231 responding")

T.section("settings")
import pcconfig

v = pcconfig.get("keymap", "US")
T.check(isinstance(v, str), "saved keymap is a string (%s)" % v)

T.section("shell")
import pcshell

import os

for name in ("ls", "run", "edit", "pwd", "cd", "cat", "cp", "mv", "rm"):
    T.check(hasattr(pcshell, name), "pcshell." + name + " exists")
cwd = os.getcwd()
T.check(isinstance(cwd, str) and cwd.startswith("/"), "cwd is an absolute path")
# pwd() is an interactive convenience: it PRINTS the path and returns None
# (returning it too would double-print at the REPL). Just check it runs.
try:
    pcshell.pwd()
    T.check(True, "pwd() runs without error")
except Exception:
    T.check(False, "pwd() runs without error")

T.section("input devices")
import keyboard
import mouse
import touch

T.check(len(keyboard.keymaps()) >= 6, "keymaps() lists layouts")
T.check(keyboard.keydown(0) >= 0, "keydown(0) returns a count")
T.check(0 <= keyboard.keydown(8) <= 7, "lock bitmap in range")
T.check_raises(ValueError, keyboard.keydown, "keydown(9) rejected", 9)
T.check(mouse.query("PRESENT") in (0, 1), "mouse PRESENT query")
T.check(touch.query("PRESENT") in (0, 1), "touch PRESENT query")

T.section("audio state (silent checks)")
import pcaudio
import audio

pcaudio.stop()
T.check(not pcaudio.is_playing(), "idle after stop()")
T.check_raises(ValueError, audio.mod_sample, "mod_sample with no MOD raises",
               1, 1, 64, 16000)
T.check_raises(ValueError, audio.tone_start, "tone > 22 kHz rejected",
               30000, 30000, None, True)
vol = pcaudio.volume()
T.check(0 <= vol <= 100, "volume() in range")
pcaudio.volume(vol)

T.section("sd card")
import machine

try:
    sd = machine.SDCard()
    if sd.present():
        cap, bs = sd.info()
        T.check(cap > 0 and bs == 512, "SD info plausible")
    else:
        print("  skip: no SD card inserted")
except Exception as e:
    print("  skip: SDCard:", e)

if __name__ == "__main__":
    T.restore_screen()
    T.report()
