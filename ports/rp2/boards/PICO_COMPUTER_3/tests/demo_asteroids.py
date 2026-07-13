# Demo of load_image() + sprite sheets + double buffering with the buffer
# primitives (hdmi.create / hdmi.copy). Spinning, bouncing asteroids.
#
# Uses a 440x464 PNG sheet of 64 asteroid frames in an 8x8 grid (55x58 each):
#   /sd/asteroid-sprite-440x464.png
#
# Copy this file to the SD card and run it:
#   run("/sd/demo_asteroids.py")
#
# Shows: load_image() loading a sheet into memory once; Image.cell() blitting
# frames out of it; and DOUBLE BUFFERING done in Python -- compose the whole
# frame into the off-screen F buffer, then hdmi.vsync() + hdmi.copy("F","N")
# flips it onto the screen in one fast copy, so the animation never tears.
# Press any (USB) key to stop, or it ends after ~30 s.

import random
import time

import hdmi
import keyboard
from pcimage import load_image

SHEET = "/sd/asteroid-sprite-440x464.png"
CW, CH, COLS, FRAMES = 55, 58, 8, 64   # 8x8 grid of 55x58 cells
NROCKS = 6


def run():
    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock to the new resolution
    w, h = hdmi.width(), hdmi.height()

    # Off-screen back buffer (F, in PSRAM) + a Display over it to draw scenery.
    try:
        hdmi.create()
    except ValueError:
        pass  # F already exists (e.g. a previous run) — reuse it
    hdmi.write("F")
    back = hdmi.fb()
    hdmi.write("N")
    clear = back.colour(0xFF00FF)   # the sheet's transparent colour (skip)
    black = back.colour(0x000000)
    starc = back.colour(0xB0B0C0)

    # Load the whole sheet into memory ONCE, in the current (RGB565) format.
    # transparent= pre-fills it so the PNG's transparent (alpha) areas become
    # magenta, which we skip when blitting. If the sheet has no alpha and the
    # asteroids show a box, set `clear` to its real background colour.
    sheet = load_image(SHEET, transparent=clear)

    stars = [(random.getrandbits(9) % w, random.getrandbits(8) % h)
             for _ in range(80)]
    rocks = []
    for _ in range(NROCKS):
        rocks.append([
            random.getrandbits(8) % (w - CW),   # x
            random.getrandbits(8) % (h - CH),   # y
            random.choice((-2, -1, 1, 2)),      # vx
            random.choice((-2, -1, 1, 2)),      # vy
            random.getrandbits(6) % FRAMES,     # frame
            random.choice((-1, 1)),             # spin
        ])

    frames = 0
    while keyboard.keydown(0) == 0 and frames < 1800:
        # 1. Compose the whole frame in the OFF-SCREEN back buffer (F).
        back.fill(black)
        for sx, sy in stars:
            back.pixel(sx, sy, starc)
        for r in rocks:
            r[0] += r[2]
            r[1] += r[3]
            if r[0] < 0 or r[0] + CW > w:
                r[2] = -r[2]
            if r[1] < 0 or r[1] + CH > h:
                r[3] = -r[3]
            r[4] = (r[4] + r[5]) % FRAMES
            # blit this rotation frame out of the sheet, magenta = transparent
            sheet.cell(r[4] % COLS, r[4] // COLS, CW, CH, r[0], r[1],
                       dst="F", skip=clear)
        # 2. Flip: copy the finished frame to the screen during vblank -> no tear.
        hdmi.vsync()
        hdmi.copy("F", "N")
        frames += 1

    hdmi.close("F")
    print("demo stopped")


run()
