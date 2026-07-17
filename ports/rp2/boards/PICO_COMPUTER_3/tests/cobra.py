# cobra.py -- Peter's MMBasic Cobra demo converted to a hidden-line,
# edge-only render (draw3d depthmode 2).
#
# The original drew the ship as filled polygons, culled by the screen-space
# cross product and painted front-to-back. Here the same 12 vertices and 17
# planes become one wireframe draw3d object: every face is outline-only, so
# the z-buffer removes the edges that pass behind the hull -- the Elite
# look. Differences from the BASIC:
#   - rotation: the three per-frame Euler angles (z, then y, then x, always
#     from the ORIGINAL vertex data) are composed into one quaternion for
#     draw3d.rotate(), which also rotates from the original orientation --
#     no reset(), matching the BASIC's absolute angles;
#   - culling: the BASIC's screen-space cross product becomes draw3d's
#     3D-normal backface culling (the face windings transfer as-is), and
#     depthmode 2 removes what culling alone cannot;
#   - the xpos/scale bounce: world-x drift with z coupled to it; the
#     perspective projection provides the grow/shrink that `scale` did.
# Culling does most of the hiding (as in the BASIC); the z-buffer handles
# the non-convex leftovers, using MMBasic's depth test verbatim.
import math
import time

import hdmi
import draw3d

VRES, HRES = hdmi.height(), hdmi.width()
S = VRES / 480          # keep proportions across screen modes
G = 1.2 * S             # geometry scale (ship radius ~134 -> ~161*S)

# vertex(0..11): x, y, z -- verbatim from the BASIC
vertices = [c * G for c in (
    32, 0, 76,      # 0 nose right
    -32, 0, 76,     # 1 nose left
    0, 26, 24,      # 2 cockpit peak
    -120, -3, -8,   # 3 left wing tip
    120, -3, -8,    # 4 right wing tip
    -88, 16, -40,   # 5 stern top left
    88, 16, -40,    # 6 stern top right
    128, -8, -40,   # 7 stern right tip
    -128, -8, -40,  # 8 stern left tip
    0, 26, -40,     # 9 stern top centre
    -32, -24, -40,  # 10 stern bottom left
    32, -24, -40,   # 11 stern bottom right
)]

# plane(0..16) -- verbatim (3- and 4-vertex faces)
fc = [3, 4, 3, 3, 4, 3, 3, 4, 3, 3, 3, 3, 3, 3, 4, 4, 3]
faces = [
    2, 1, 0,
    0, 1, 10, 11,
    6, 2, 0,
    0, 4, 6,
    7, 4, 0, 11,
    1, 2, 5,
    5, 3, 1,
    1, 3, 8, 10,
    9, 5, 2,
    2, 6, 9,
    3, 5, 8,
    7, 6, 4,
    10, 8, 5,
    11, 7, 4,
    9, 10, 8, 5,
    9, 6, 7, 11,
    9, 11, 10,
]
nv, nf, cam = 12, 17, 1
colours = [0x00FFFF]            # pale cyan, as the original's rgb(...,255,255)
edge = [0] * nf                 # every face edge-only: no fill list at all

draw3d.close_all()
draw3d.create(1, nv, nf, cam, vertices, fc, faces, colours, edge)
draw3d.camera(1, 110 * S, 0, 0)
hdmi.close("F")
hdmi.create()
hdmi.write("F")

WHITE = {4: 15, 8: 0xFF, 16: 0xFFFF}[hdmi.bpp()]


def qmul(a, b):
    # Hamilton product of two (w, x, y, z, m) quaternions (MMBasic T_Mult).
    w1, x1, y1, z1, m1 = a
    w2, x2, y2, z2, m2 = b
    return (
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        m1 * m2,
    )


# nearest world z the tumbling hull can reach must stay in front of the
# viewplane: |vertex|max ~134*G, so keep z >= 110*S + 134*G + margin.
ax = ay = az = 0.0
xw, step = 0.0, 0.6 * S
XLIM = 60.0 * S
fps = 0.0
frames = int(globals().get("FRAMES", 1000))
t0 = time.ticks_ms()
# the finally runs even on Ctrl-C: come home to the visible display and free
# F, or the console would keep printing into the invisible buffer
try:
    for _ in range(frames):
        tts = time.ticks_ms()
        # the BASIC's per-frame angle steps (radians)
        ax -= 0.00005
        ay += 0.01
        az += 0.025
        # rotate around z, then y, then x == one combined quaternion
        q = qmul(draw3d.q_create(ax, 1.0, 0.0, 0.0),
            qmul(draw3d.q_create(ay, 0.0, 1.0, 0.0),
                draw3d.q_create(az, 0.0, 0.0, 1.0)))
        draw3d.rotate(q, 1)

        # drift and breathe: x bounces, z rides it (perspective = old scale)
        xw += step
        if xw > XLIM or xw < -XLIM:
            step = -step
            xw += step
        yw = 0.25 * xw
        zw = 315.0 * S + 0.5 * xw

        hdmi.fill(0)
        draw3d.show(1, xw, yw, zw, 0, 2)    # backface culling + hidden line
        ms = time.ticks_diff(time.ticks_ms(), tts)
        if ms > 0:
            fps = fps * 0.9 + 0.1 * (1000.0 / ms)
        hdmi.text("FPS: %.1f" % fps, 0, 0, WHITE)
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    hdmi.close("F")
ms = time.ticks_diff(time.ticks_ms(), t0)
if ms > 0:
    print("cobra: %d frames in %d ms (%.1f fps)" % (frames, ms, frames * 1000.0 / ms))
