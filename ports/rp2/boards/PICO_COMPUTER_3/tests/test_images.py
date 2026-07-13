# Image save/load round-trip: draw a known pattern, save_image() to BMP,
# clear, draw_bmp() it back, verify pixels. Optional decode checks for any
# test.jpg/test.png placed next to the tests.

import hdmi
import os
import pcimage
import testutil as T

T.quiet()

TMP = "/bmp_test.bmp"


def _same(a, b):
    # Compare two framebuffer pixel values by DISPLAYED colour. In RGB1024
    # (4bpp) a pixel is a palette INDEX; the save/load round-trip goes
    # index -> RGB888 -> nearest index, so an aliased palette (two entries with
    # the same colour) can legitimately return a different index that shows the
    # identical colour. Comparing indices would flag that visually-perfect
    # round-trip as a failure, so compare the palette colours instead.
    if hdmi.bpp() == 4:
        return hdmi.palette(a) == hdmi.palette(b)
    return a == b


for mode, name in ((hdmi.RGB320, "RGB320"), (hdmi.RGB640, "RGB640"),
                   (hdmi.RGB1024, "RGB1024")):
    T.section("images " + name)
    hdmi.deinit()
    hdmi.init(mode)
    fb = hdmi.fb()
    # Colours that survive the 888 round-trip exactly in every format (each is
    # an exact RGB121 palette entry too). Background is a distinct exact colour.
    bg = fb.colour(0x0000FF)  # blue
    cols = [fb.colour(c) for c in (0xFF0000, 0x00FF00, 0xFFFF00, 0xFFFFFF)]
    hdmi.fill(bg)
    for i, c in enumerate(cols):
        fb.fill_rect(10 + i * 20, 10, 16, 16, c)
    pcimage.save_image(TMP)
    hdmi.fill(0)  # wipe to black — distinct from bg, so a working decode shows
    pcimage.draw_bmp(TMP)
    ok = all(_same(fb.pixel(10 + i * 20 + 8, 18), c) for i, c in enumerate(cols))
    T.check(ok, "BMP save/load round-trip")
    # (5,5) is outside every rect: it must come back as the blue background,
    # which also proves the decode actually ran (black would mean it didn't).
    T.check(_same(fb.pixel(5, 5), bg), "background restored (decode ran)")
    os.remove(TMP)

# Optional: decode sample files if the user provided them.
T.section("images optional assets")
for fname, loader in (("test.jpg", pcimage.draw_jpg), ("test.png", pcimage.draw_png)):
    try:
        os.stat(fname)
    except OSError:
        print("  skip:", fname, "not present")
        continue
    hdmi.deinit()
    hdmi.init(hdmi.RGB320)
    hdmi.fill(0)
    loader(fname)
    fb = hdmi.fb()
    drawn = any(fb.pixel(x, y) for x in range(0, 100, 7) for y in range(0, 100, 7))
    T.check(drawn, fname + " decoded something")

if __name__ == "__main__":
    T.restore_screen()
    T.report()
