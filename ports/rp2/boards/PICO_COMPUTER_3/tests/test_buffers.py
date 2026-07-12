# Framebuffer targets (N/L/F): layer lifecycle, write-target switching,
# copies, and mode-change teardown — all pixel- or state-verified.

import hdmi
import testutil as T

T.quiet()

# --- RGB320: the layer mode ---
T.section("buffers RGB320 layer")
hdmi.deinit()
hdmi.init(hdmi.RGB320)
T.check(hdmi.transparent() == -1, "no layer at init")
T.check_raises(ValueError, hdmi.write, "write('L') before layer raises", "L")
hdmi.layer()  # default transparent = black
T.check(hdmi.transparent() == 0, "layer transparent reads back as 0")
T.check_raises(ValueError, hdmi.layer, "second layer() raises")

fbN = hdmi.fb()
c1 = fbN.colour(0xFF0000)
c2 = fbN.colour(0x00FF00)

# Draw on N, then on L; each buffer holds its own pixels.
fbN.fill_rect(10, 10, 4, 4, c1)
hdmi.write("L")
T.check(hdmi.write() == "L", "write() getter reports L")
fbL = hdmi.fb()
fbL.fill_rect(10, 10, 4, 4, c2)
T.check(fbL.pixel(10, 10) == c2, "layer holds its pixels")
hdmi.write("N")
T.check(fbN.pixel(10, 10) == c1, "display pixels untouched by layer draw")

# framebuffer() size is mode-sized for every target.
T.check(len(hdmi.framebuffer()) == 320 * 240 * 2, "framebuffer() size (N)")

# --- F buffer + copy ---
T.section("buffers F/copy")
T.check_raises(ValueError, hdmi.write, "write('F') before create raises", "F")
hdmi.create()
T.check_raises(ValueError, hdmi.create, "second create() raises")
hdmi.fill(0)
fbN.fill_rect(30, 30, 4, 4, c1)
hdmi.copy("N", "F")
hdmi.fill(0)
T.check(fbN.pixel(30, 30) == 0, "display cleared")
hdmi.copy("F", "N")
T.check(fbN.pixel(30, 30) == c1, "copy N->F->N round-trip")

# Write target snaps back to N when its buffer closes.
hdmi.write("F")
hdmi.close("F")
T.check(hdmi.write() == "N", "close('F') resets write target")
hdmi.write("L")
hdmi.close()  # close both
T.check(hdmi.write() == "N" and hdmi.transparent() == -1, "close() drops layer too")
T.check_raises(ValueError, hdmi.close, "close('N') rejected", "N")

# --- Mode change tears everything down ---
T.section("buffers mode change")
hdmi.layer()
hdmi.create()
hdmi.write("F")
hdmi.deinit()
hdmi.init(hdmi.RGB640)
T.check(hdmi.write() == "N", "mode change resets write target")
T.check(hdmi.transparent() == -1, "mode change drops the layer")
T.check_raises(ValueError, hdmi.write, "mode change frees F", "F")
T.check_raises(ValueError, hdmi.layer, "layer() outside RGB320 raises")

# F works in a non-layer mode too.
fb = hdmi.fb()
c1 = fb.colour(0xFFFF00)
hdmi.create()
hdmi.fill(0)
fb.fill_rect(5, 5, 3, 3, c1)
hdmi.copy("N", "F")
hdmi.fill(0)
hdmi.copy("F", "N")
T.check(fb.pixel(5, 5) == c1, "F round-trip in RGB640")
hdmi.close()

if __name__ == "__main__":
    T.restore_screen()
    T.report()
