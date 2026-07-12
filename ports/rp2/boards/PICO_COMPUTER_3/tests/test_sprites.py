# pcsprite engine: rendering (pixel-verified), z-order, collisions (sprite/
# edge/wall, layers, edge-triggered), scroll — in both compositor modes
# (RGB320 = overlay layer, RGB640 = F-snapshot dirty rectangles).

import framebuf
import hdmi
import pcsprite as sp
import testutil as T

T.quiet()


def solid_sprite(w, h, colour):
    img = bytearray(w * h * hdmi.bpp() // 8)
    fmt = framebuf.GS4_HMSB if hdmi.bpp() == 4 else (
        framebuf.GS8 if hdmi.bpp() == 8 else framebuf.RGB565)
    framebuf.FrameBuffer(img, w, h, fmt).fill(colour)
    return sp.Sprite(img, w, h)


def read_target():
    # Where sprites are composited: the layer in RGB320, the display else.
    if hdmi.width() == 320:
        prev = hdmi.write()
        hdmi.write("L")
        d = hdmi.fb()
        hdmi.write(prev)
        return d
    return hdmi.fb()


for mode, name in ((hdmi.RGB320, "layer mode"), (hdmi.RGB640, "F mode")):
    hdmi.deinit()
    hdmi.init(mode)
    sp.reset()
    T.section("sprites " + name)
    fbN = hdmi.fb()
    c_scene = fbN.colour(0x0000FF)
    c_s1 = fbN.colour(0xFF0000)
    c_s2 = fbN.colour(0x00FF00)
    hdmi.fill(c_scene)  # scenery: solid blue (snapshotted at first update)

    s1 = solid_sprite(8, 8, c_s1)
    s2 = solid_sprite(8, 8, c_s2)
    s1.show(20, 20)
    ev = sp.update()
    d = read_target()
    T.check(ev == [], "no events for a lone sprite")
    T.check(d.pixel(20, 20) == c_s1 and d.pixel(27, 27) == c_s1, "sprite drawn")
    T.check(fbN.pixel(40, 40) == c_scene, "scenery intact")

    # Move: old spot restored, new spot drawn.
    s1.x = 40
    sp.update()
    T.check(d.pixel(47, 20) == c_s1, "moved sprite drawn")
    if name == "layer mode":
        T.check(d.pixel(20, 20) == 0, "old spot transparent again on layer")
    else:
        T.check(d.pixel(20, 20) == c_scene, "old spot restored from snapshot")

    # Z-order: s2 shown later overlaps s1 and wins; top() flips it.
    s2.show(44, 20)
    sp.update()
    T.check(d.pixel(45, 21) == c_s2, "later show() draws on top")
    s1.top()
    sp.update()
    T.check(d.pixel(45, 21) == c_s1, "top() raises in z-order")

    # Collisions: edge-triggered, once per new contact.
    s2.hide()
    s1.hide()
    sp.update()
    s1.show(20, 20)
    s2.show(60, 60)
    T.check(sp.update() == [], "separated: no events")
    s2.x, s2.y = 24, 24
    ev = sp.update()
    T.check(len(ev) == 1 and s1 in ev[0] and s2 in ev[0], "overlap event fired")
    s2.x = 25
    T.check(sp.update() == [], "still touching: no repeat event")
    s2.x, s2.y = 60, 60
    sp.update()
    s2.x, s2.y = 24, 24
    T.check(len(sp.update()) == 1, "re-contact fires again")

    # Layer partitioning: different layers don't collide; layer 0 hits all.
    s2.hide()
    sp.update()
    s2.layer = 2
    s2.show(24, 24)
    T.check(sp.update() == [], "different layers: no event")
    s2.hide()
    sp.update()
    s2.layer = 0
    s2.show(24, 24)
    T.check(len(sp.update()) == 1, "layer 0 collides with all")
    s2.hide()
    s2.layer = 1

    # Screen edge.
    s1.x = -2
    ev = sp.update()
    T.check(("left" in [e[1] for e in ev]) if ev else False, "left edge event")
    s1.x = 20
    sp.update()

    # Wall: strict overlap (touching edges do NOT collide, as MMBasic).
    w1 = sp.wall(100, 100, 20, 20)
    s1.x, s1.y = 92, 100     # touching at x: 92+8 == 100 -> no collision
    T.check(sp.update() == [], "touching a wall edge: no event")
    s1.x = 95                # overlaps by 3px
    ev = sp.update()
    T.check(len(ev) == 1 and ev[0][1] is w1, "wall overlap event")
    w1.remove()

    # Hide restores what was underneath.
    s1.hide()
    sp.update()
    if name == "layer mode":
        T.check(d.pixel(95, 100) == 0, "hide clears the layer")
    else:
        T.check(d.pixel(95, 100) == c_scene, "hide restores the scenery")

    sp.reset()

# Scroll: scenery wraps; layer-0 sprites travel; other layers stay.
T.section("sprites scroll")
hdmi.deinit()
hdmi.init(hdmi.RGB320)
sp.reset()
fbN = hdmi.fb()
c1 = fbN.colour(0xFF00FF)
hdmi.fill(0)
fbN.pixel(10, 100, c1)
s0 = solid_sprite(8, 8, fbN.colour(0x00FFFF))
s0.layer = 0
s0.show(50, 50)
s9 = solid_sprite(8, 8, fbN.colour(0xFFFF00))
s9.show(200, 50)  # layer 1: must not move
sp.update()
sp.scroll(4, 0)
T.check(fbN.pixel(14, 100) == c1, "scenery scrolled right by 4")
T.check(s0.x == 54, "layer-0 sprite travelled with the scenery")
T.check(s9.x == 200, "layer-1 sprite stayed put")
# Wrap: a pixel at the right edge reappears on the left.
hdmi.fill(0)
sp.update()
fbN.pixel(319, 100, c1)
sp.scroll(4, 0)
T.check(fbN.pixel(3, 100) == c1, "right edge wraps to the left")
sp.reset()

if __name__ == "__main__":
    T.restore_screen()
    T.report()
