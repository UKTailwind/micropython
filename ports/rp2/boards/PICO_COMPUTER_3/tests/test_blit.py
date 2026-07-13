# hdmi.blit(): pixel-verified tests in every video mode.
# Covers: basic copy, skip colour, same-buffer overlap (all directions),
# (buffer, w, h) tuple surfaces, and edge clipping.
#
# The checks read pixels back and don't need to be seen. Set WATCH = True (or
# run("test_blit.py") after editing it) to also play a large, slow, on-screen
# demonstration of the same operations after the checks pass.

import hdmi
import testutil as T

WATCH = False

T.quiet()

for mode, name in ((hdmi.RGB320, "RGB320"), (hdmi.RGB640, "RGB640"),
                   (hdmi.RGB512, "RGB512"), (hdmi.RGB1024, "RGB1024")):
    T.section("blit " + name)
    hdmi.deinit()
    hdmi.init(mode)
    fb = hdmi.fb()
    c1 = fb.colour(0xFF0000)  # red-ish in every format
    c2 = fb.colour(0x00FF00)
    c3 = fb.colour(0x0000FF)
    if c1 == c2 or c2 == c3 or c1 == c3:
        T.check(False, "distinct test colours")
        continue

    # Basic copy: 4x4 block from (6,6) to (20,20).
    hdmi.fill(0)
    fb.fill_rect(6, 6, 4, 4, c1)
    hdmi.blit(6, 6, 4, 4, 20, 20)
    T.check(fb.pixel(20, 20) == c1 and fb.pixel(23, 23) == c1, "copy lands")
    T.check(fb.pixel(6, 6) == c1, "source intact")
    T.check(fb.pixel(24, 24) == 0, "no overspill")

    # Skip colour: two-colour block onto a c3 background; c1 must not copy.
    hdmi.fill(0)
    fb.fill_rect(6, 6, 4, 4, c1)
    fb.fill_rect(8, 6, 2, 4, c2)          # right half c2
    fb.fill_rect(30, 30, 4, 4, c3)        # destination background
    hdmi.blit(6, 6, 4, 4, 30, 30, None, None, c1)
    T.check(fb.pixel(30, 30) == c3, "skip colour left destination alone")
    T.check(fb.pixel(32, 30) == c2, "non-skip pixels copied")

    # Overlap, all four directions: a block with a marker pixel keeps its
    # shape when shifted over itself.
    for dx, dy, tag in ((2, 0, "right"), (-2, 0, "left"), (0, 2, "down"), (0, -2, "up")):
        hdmi.fill(0)
        fb.fill_rect(40, 40, 8, 8, c1)
        fb.pixel(41, 41, c2)              # marker near the top-left corner
        hdmi.blit(40, 40, 8, 8, 40 + dx, 40 + dy)
        ok = (fb.pixel(41 + dx, 41 + dy) == c2
              and fb.pixel(40 + dx, 40 + dy) == c1
              and fb.pixel(47 + dx, 47 + dy) == c1)
        T.check(ok, "overlap shift " + tag)

    # Tuple surface round-trip: grab a block into a bytearray, wipe the
    # screen, stamp it back somewhere else.
    hdmi.fill(0)
    fb.fill_rect(10, 10, 4, 4, c2)
    fb.pixel(11, 11, c1)
    img = bytearray(4 * 4 * hdmi.bpp() // 8)
    hdmi.blit(10, 10, 4, 4, 0, 0, "N", (img, 4, 4))
    hdmi.fill(0)
    hdmi.blit(0, 0, 4, 4, 50, 50, (img, 4, 4), "N")
    T.check(fb.pixel(50, 50) == c2 and fb.pixel(51, 51) == c1, "tuple surface round-trip")

    # Clipping: partly off every edge must not wrap or crash.
    W = hdmi.width()
    H = hdmi.height()
    hdmi.fill(0)
    fb.fill_rect(0, 0, 8, 8, c1)
    hdmi.blit(0, 0, 8, 8, -4, -4)                 # top-left
    hdmi.blit(0, 0, 8, 8, W - 4, H - 4)           # bottom-right
    T.check(fb.pixel(0, 0) == c1, "clip: corners survive")
    T.check(fb.pixel(W - 1, H - 1) == c1, "clip: bottom-right partial drawn")
    # A negative source origin shifts the destination in step (MMBasic):
    # source (-4,-4) 8x8 clips to (0,0) 4x4 landing at (104,104).
    hdmi.blit(-4, -4, 8, 8, 100, 100)
    T.check(fb.pixel(104, 104) == c1 and fb.pixel(100, 100) == 0,
            "clip: negative source origin shifts destination")

def _demo():
    # Large, slow, on-screen demonstration of the blitter (RGB320).
    import time

    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    fb = hdmi.fb()
    W = hdmi.width()
    H = hdmi.height()
    WHITE = fb.colour(0xFFFFFF)
    RED = fb.colour(0xFF0000)
    GREEN = fb.colour(0x00FF00)
    BLUE = fb.colour(0x0000FF)
    MAG = fb.colour(0xFF00FF)
    BG = fb.colour(0x000030)

    def scene(msg):
        hdmi.fill(BG)
        fb.fill_rect(0, 0, W, 13, fb.colour(0x303030))
        hdmi.text(msg, 2, 1, WHITE)

    # 1. Copy a rectangle repeatedly across the screen.
    scene("blit: copy a rectangle")
    fb.fill_rect(16, 40, 40, 40, RED)
    fb.fill_rect(24, 48, 24, 24, WHITE)
    time.sleep_ms(800)
    for i in range(1, 7):
        hdmi.blit(16, 40, 40, 40, 16 + i * 42, 40)
        time.sleep_ms(200)
    time.sleep_ms(900)

    # 2. Skip colour: stamp a cut-out sprite over a striped background.
    scene("blit: skip colour (cut-out sprite)")
    fb.fill_rect(16, 30, 40, 40, MAG)          # magenta = the cut-out colour
    fb.fill_rect(26, 40, 20, 20, GREEN)        # the visible 'sprite'
    fb.fill_rect(32, 46, 8, 8, WHITE)
    time.sleep_ms(800)
    for x in range(0, W, 20):
        fb.fill_rect(x, 110, 10, 90, BLUE)     # background stripes
    time.sleep_ms(400)
    for i in range(6):
        hdmi.blit(16, 30, 40, 40, 8 + i * 48, 130, None, None, MAG)
        time.sleep_ms(300)
    time.sleep_ms(1000)

    # 3. Same-buffer overlap: scroll a band sideways over itself.
    scene("blit: scroll a region over itself")
    cols = (RED, GREEN, BLUE, MAG, WHITE)
    for i in range(9):
        fb.fill_rect(16 + i * 32, 60, 24, 90, cols[i % 5])
    time.sleep_ms(800)
    for _ in range(48):
        hdmi.blit(2, 60, W - 2, 90, 0, 60)     # shift the band left 2px
        fb.fill_rect(W - 2, 60, 2, 90, BG)     # blank the exposed edge
        time.sleep_ms(25)
    time.sleep_ms(600)

    # 4. Tuple surface: grab into a bytearray, clear, stamp it back.
    scene("blit: grab to RAM, then stamp back")
    fb.fill_rect(20, 40, 48, 48, GREEN)
    fb.fill_rect(32, 52, 24, 24, RED)
    time.sleep_ms(800)
    img = bytearray(48 * 48 * hdmi.bpp() // 8)
    hdmi.blit(20, 40, 48, 48, 0, 0, "N", (img, 48, 48))
    scene("blit: grab to RAM, then stamp back")
    time.sleep_ms(600)
    for pos in ((100, 60), (200, 100), (60, 150), (220, 170)):
        hdmi.blit(0, 0, 48, 48, pos[0], pos[1], (img, 48, 48), "N")
        time.sleep_ms(400)
    scene("blit demo done")
    time.sleep_ms(1200)


if __name__ == "__main__":
    if WATCH:
        _demo()
    T.restore_screen()
    T.report()
