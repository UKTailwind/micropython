# pccursor: pixel-verified tests of the mouse pointer overlay in every video
# mode. The pointer is steered with move() so no mouse is needed; each check
# reads pixels back (hot point drawn, screen saved/restored, hide/show, shape
# and colour changes, partial off-screen clipping). With a mouse connected
# the pointer follows it, so the pixel checks are skipped for a smoke run.

import hdmi
import mouse
import pccursor
import testutil as T

T.quiet()

for mode, name in ((hdmi.RGB320, "RGB320"), (hdmi.RGB640, "RGB640"),
                   (hdmi.RGB512, "RGB512"), (hdmi.RGB1024, "RGB1024")):
    T.section("cursor " + name)
    hdmi.deinit()
    hdmi.init(mode)
    fb = hdmi.fb()
    bgc = fb.colour(0x0000FF)
    white = fb.colour(0xFFFFFF)
    red = fb.colour(0xFF0000)
    hdmi.fill(bgc)

    if mouse.query("PRESENT"):
        # A live mouse owns the position, so the pixel checks can't aim.
        # Just prove the lifecycle runs, wherever the pointer happens to be.
        print("  skip pixel checks: a mouse is connected and owns the pointer")
        pccursor.on()
        pccursor.refresh()
        pccursor.hide()
        pccursor.show()
        pccursor.off()
        T.check(not pccursor.active(), "on/refresh/hide/show/off lifecycle runs")
        continue

    # Arrow: hot point at the tip -> the pixel AT (x, y) is the cursor colour.
    pccursor.on(pccursor.ARROW, colour=0xFFFFFF, x=50, y=40)
    T.check(pccursor.pos() == (50, 40), "pos() reports the placed point")
    T.check(fb.pixel(50, 40) == white, "arrow tip drawn at the hot point")
    T.check(fb.pixel(51, 40) == bgc, "transparent pixels leave background")

    # The pixels underneath come back when the pointer moves away.
    pccursor.move(120, 90)
    T.check(fb.pixel(50, 40) == bgc, "old position restored after move")
    T.check(fb.pixel(120, 90) == white, "new position drawn")

    # hide()/show()
    pccursor.hide()
    T.check(fb.pixel(120, 90) == bgc, "hide() restores the screen")
    pccursor.show()
    T.check(fb.pixel(120, 90) == white, "show() repaints")

    # erase() lifts it; the next refresh() repaints.
    pccursor.erase()
    T.check(fb.pixel(120, 90) == bgc, "erase() restores the screen")
    pccursor.refresh()
    T.check(fb.pixel(120, 90) == white, "refresh() repaints after erase()")

    # Cross: hot point centred, recoloured; on() again swaps live.
    pccursor.on(pccursor.CROSS, colour=0xFF0000)
    T.check(fb.pixel(120, 90) == red, "cross centre drawn in new colour")
    T.check(fb.pixel(113, 90) == red, "cross arm reaches the edge")
    T.check(fb.pixel(113, 83) == bgc, "cross corners transparent")

    # Clipping: park the hot point in every corner; must not crash, and
    # moving away must restore the background exactly.
    W = hdmi.width()
    H = hdmi.height()
    corners = ((0, 0), (W - 1, 0), (0, H - 1), (W - 1, H - 1))
    for cx, cy in corners:
        pccursor.move(cx, cy)
    pccursor.move(120, 90)
    T.check(all(fb.pixel(cx, cy) == bgc for cx, cy in corners),
            "corner visits restore all four corners")

    pccursor.off()
    T.check(fb.pixel(120, 90) == bgc, "off() restores the screen")
    T.check(not pccursor.active(), "inactive after off()")

hdmi.fill(0)
if __name__ == "__main__":
    T.restore_screen()
    T.report()
