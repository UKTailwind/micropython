# Image save/load round-trip: draw a known pattern, save_image() to BMP,
# clear, draw_bmp() it back, verify pixels. Optional decode checks for any
# test.jpg/test.png placed next to the tests.

import hdmi
import os
import pcimage
import testutil as T

T.quiet()

TMP = "/bmp_test.bmp"

for mode, name in ((hdmi.RGB320, "RGB320"), (hdmi.RGB640, "RGB640"),
                   (hdmi.RGB1024, "RGB1024")):
    T.section("images " + name)
    hdmi.deinit()
    hdmi.init(mode)
    fb = hdmi.fb()
    # Colours chosen to survive the 888 round-trip exactly in every format.
    cols = [fb.colour(c) for c in (0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF)]
    hdmi.fill(0)
    for i, c in enumerate(cols):
        fb.fill_rect(10 + i * 20, 10, 16, 16, c)
    pcimage.save_image(TMP)
    hdmi.fill(0)
    pcimage.draw_bmp(TMP)
    ok = all(fb.pixel(10 + i * 20 + 8, 18) == c for i, c in enumerate(cols))
    T.check(ok, "BMP save/load round-trip")
    T.check(fb.pixel(5, 5) == 0, "background stayed black")
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
