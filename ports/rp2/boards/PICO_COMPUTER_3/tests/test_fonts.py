# MMBasic bitmap fonts: metrics + rendering checks, plus an on-screen show.
#
# The checks read pixels back and don't need to be seen. Set WATCH = True (or
# run("test_fonts.py") after editing it) to also display every font, at scale
# 1 and 2, after the checks pass.

import time

import hdmi
import testutil as T

WATCH = False

# Expected (number, width, height) for the built-in font table (see fonts.h).
EXPECT = (
    (1, 8, 12), (2, 12, 20), (3, 16, 24), (4, 10, 16), (5, 24, 32),
    (6, 32, 50), (7, 6, 8), (8, 4, 6), (9, 8, 10),
)


def _count_set(fb, x, y, w, h, bg):
    """Number of pixels in the box (x,y,w,h) that differ from bg."""
    n = 0
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if fb.pixel(xx, yy) != bg:
                n += 1
    return n


def _wait():
    import keyboard

    t0 = time.ticks_ms()
    while keyboard.keydown(0) == 0:
        if time.ticks_diff(time.ticks_ms(), t0) > 2500:
            return
        time.sleep_ms(20)
    while keyboard.keydown(0):  # wait for release
        time.sleep_ms(20)


def show():
    """Display every font (scale 1 and 2) on the HDMI screen."""
    import pcconsole

    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    time.sleep(3)  # let the monitor re-lock before drawing
    pcconsole.console("serial")
    fb = hdmi.fb()
    white = fb.colour(0xFFFFFF)
    cyan = fb.colour(0x00FFFF)
    yellow = fb.colour(0xFFFF00)
    sample = "The quick brown fox 0123456789"
    try:
        for (num, w, h) in EXPECT:
            hdmi.fill(0)
            fb.text("Font %d  %dx%d" % (num, w, h), 4, 2, yellow, font=1)
            s = "0123456789:" if num == 6 else sample
            y = 20
            fb.text(s, 4, y, cyan, font=num)
            y += h + 6
            if y + h * 2 < hdmi.height():
                fb.text(s, 4, y, white, font=num, scale=2)
            print("Font", num, "- press a key")
            _wait()
        hdmi.fill(0)
        fb.text("Fonts complete", 4, 100, white, font=3)
        _wait()
    finally:
        pcconsole.console("both")


# --- checks (run at import, like the other automatic suites) ----------------
T.quiet()
hdmi.deinit()
hdmi.init(hdmi.RGB320)
_fb = hdmi.fb()
_white = _fb.colour(0xFFFFFF)

T.section("font table")
_fonts = hdmi.fonts()
T.check(len(_fonts) == len(EXPECT), "9 fonts reported")
for (_num, _w, _h) in EXPECT:
    _e = _fonts[_num - 1]
    T.check(_e[0] == _num and _e[1] == _w and _e[2] == _h,
            "font %d is %dx%d" % (_num, _w, _h))

T.section("rendering")
for (_num, _w, _h) in EXPECT:
    # Font 6 (ArialNum) only has digits, so probe with '5'; others use 'A'.
    _ch = "5" if _num == 6 else "A"
    hdmi.fill(0)
    _end = _fb.text(_ch, 4, 4, _white, font=_num)
    T.check(_count_set(_fb, 4, 4, _w, _h, 0) > 0, "font %d draws '%s'" % (_num, _ch))
    T.check(_end == 4 + _w, "font %d advance = width" % _num)

T.section("scale + transparency")
hdmi.fill(0)
_end = _fb.text("AB", 4, 4, _white, font=1, scale=2)
T.check(_end == 4 + 8 * 2 * 2, "scale 2 advance (2 chars x 8 x 2)")
T.check(_count_set(_fb, 4, 4, 16, 24, 0) > 0, "scaled glyph draws")
hdmi.fill(0)
_fb.text(" ", 4, 4, _white, font=1)  # space + transparent bg -> nothing drawn
T.check(_count_set(_fb, 4, 4, 8, 12, 0) == 0, "space is blank (transparent)")

T.section("robustness")
hdmi.fill(0)
try:
    _fb.text(chr(1), 4, 4, _white, font=3)     # char below the font's range
    _fb.text("A", 4, 4, _white, font=99)       # bad font -> falls back to 1
    _fb.text("A", -50, -50, _white, font=5)    # fully clipped off-screen
    T.check(True, "bad char/font/offscreen survive")
except Exception:
    T.check(False, "bad char/font/offscreen survive")


if __name__ == "__main__":
    if WATCH:
        show()
    T.restore_screen()
    T.report()
