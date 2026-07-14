# Tile-map demo: builds a tileset in memory (no external asset), fills a map,
# and scrolls the viewport smoothly using the off-screen F buffer. Visual demo;
# press any USB key to stop (or it ends after a while).
#
#   run("/sd/tests/test_tilemap.py")

import time

import framebuf
import hdmi
import keyboard
import pcgfx
from pctilemap import TileMap

TW = TH = 16          # tile size
TPR = 4               # tiles per row in the sheet (4x4 = 16 tiles)


def _build_tileset():
    """Draw a small tileset (grass / water / wall / tree) into an RGB565 buffer
    and return a (buffer, w, h) surface."""
    sw, sh = TPR * TW, TPR * TH
    buf = bytearray(sw * sh * 2)                 # RGB565, 2 bytes/pixel
    ts = pcgfx.Display(buf, sw, sh, framebuf.RGB565)

    def cell(i):
        return ((i - 1) % TPR) * TW, ((i - 1) // TPR) * TH

    # tile 1: grass (green with a few darker specks)
    x, y = cell(1)
    ts.fill_rect(x, y, TW, TH, ts.colour(0x2E7D32))
    for dx, dy in ((3, 4), (9, 7), (6, 12), (12, 3)):
        ts.pixel(x + dx, y + dy, ts.colour(0x1B5E20))
    # tile 2: water (blue with a wave line)
    x, y = cell(2)
    ts.fill_rect(x, y, TW, TH, ts.colour(0x1565C0))
    ts.hline(x + 2, y + 6, TW - 4, ts.colour(0x64B5F6))
    ts.hline(x + 2, y + 11, TW - 4, ts.colour(0x64B5F6))
    # tile 3: wall (grey brick)
    x, y = cell(3)
    ts.fill_rect(x, y, TW, TH, ts.colour(0x9E9E9E))
    ts.rect(x, y, TW, TH, ts.colour(0x616161))
    ts.hline(x, y + 8, TW, ts.colour(0x616161))
    ts.vline(x + 8, y, 8, ts.colour(0x616161))
    ts.vline(x + 4, y + 8, 8, ts.colour(0x616161))
    # tile 4: tree (green canopy + brown trunk on grass)
    x, y = cell(4)
    ts.fill_rect(x, y, TW, TH, ts.colour(0x2E7D32))
    ts.fill_rect(x + 6, y + 9, 4, 5, ts.colour(0x6D4C41))
    ts.ellipse(x + 8, y + 6, 6, 5, ts.colour(0x1B5E20), True)
    return (buf, sw, sh)


def _build_map(cols, rows):
    tm = TileMap(_build_tileset(), TW, TH, cols, rows, tiles_per_row=TPR)
    tm.set_attr(2, 1)          # water is "solid" (bit 0)
    tm.set_attr(3, 1)          # wall is "solid"
    for r in range(rows):
        for c in range(cols):
            if c == 0 or r == 0 or c == cols - 1 or r == rows - 1:
                tm.set(c, r, 3)        # wall border
            else:
                tm.set(c, r, 1)        # grass
    for r in range(5, 11):             # a pond
        for c in range(7, 16):
            tm.set(c, r, 2)
    for c, r in ((20, 8), (24, 12), (30, 6), (22, 20), (34, 24), (12, 26)):
        tm.set(c, r, 4)                # scattered trees
    return tm


def run():
    import pcconsole

    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock
    pcconsole.console("serial")
    w, h = hdmi.width(), hdmi.height()

    cols, rows = 48, 36
    tm = _build_map(cols, rows)
    world_w, world_h = cols * TW, rows * TH

    try:
        hdmi.create()          # off-screen F buffer
    except ValueError:
        pass                   # already exists

    vx = vy = 0.0
    dx, dy = 1.2, 0.8
    print("tilemap demo scrolling -- press a key to stop")
    frames = 0
    try:
        while keyboard.keydown(0) == 0 and frames < 3000:
            vx += dx
            vy += dy
            if vx <= 0 or vx >= world_w - w:
                dx = -dx
            if vy <= 0 or vy >= world_h - h:
                dy = -dy
            tm.view(int(vx), int(vy))
            hdmi.write("F")
            hdmi.fill(0)
            tm.draw()                     # renders the viewport into F
            hdmi.write("N")
            hdmi.vsync()
            hdmi.copy("F", "N")           # flip -> tear-free
            frames += 1
    finally:
        hdmi.write("N")
        pcconsole.console("both")
        import testutil as T

        T.restore_screen()
    print("tilemap demo finished")


if __name__ == "__main__":
    run()
