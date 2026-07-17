# elite.py -- hidden-line (depthmode 2) demo: an Elite-style wireframe
# ship tumbling in the middle of the screen, green lines on black, with
# the edges behind the hull properly removed by the 1/z buffer.
#
# The hidden-line depth test uses a relative 5% tolerance on 1/z, so any
# scene scale works; this demo keeps the ship close simply for size.
import math
import time

import hdmi
import draw3d

VRES, HRES = hdmi.height(), hdmi.width()

# A flattened angular hull: nose, raised top deck, wing tips, tail.
s = VRES / 480  # keep proportions across modes
vertices = [c * s for c in (
    0, 2, 90,       # 0 nose
    0, 16, 0,       # 1 top deck
    -70, 0, -50,    # 2 left wing tip
    70, 0, -50,     # 3 right wing tip
    0, 16, -50,     # 4 tail top
    0, -10, -50,    # 5 tail bottom
    0, -10, 0,      # 6 belly
)]
faces = [
    0, 2, 1,        # nose / left / top deck
    0, 1, 3,        # nose / top deck / right
    1, 2, 4,        # top deck / left / tail top
    1, 4, 3,        # top deck / tail top / right
    0, 2, 6,        # nose / left / belly
    0, 6, 3,        # nose / belly / right
    6, 2, 5,        # belly / left / tail bottom
    6, 5, 3,        # belly / tail bottom / right
    2, 4, 3, 5,     # stern plate
]
nv, nf, cam = 7, 9, 1
fc = [3] * 8 + [4]
colours = [0x00FF00]
edge = [0] * nf                 # green edges, no fill (pure wireframe)

draw3d.close_all()
draw3d.create(1, nv, nf, cam, vertices, fc, faces, colours, edge)
draw3d.camera(1, 150 * s, 0, 0)
hdmi.close("F")
hdmi.create()
hdmi.write("F")

Z = 230.0 * s
frames = int(globals().get("FRAMES", 600))
t0 = time.ticks_ms()
# the finally runs even on Ctrl-C: come home to the visible display and free
# F, or the console would keep printing into the invisible buffer
try:
    for _ in range(frames):
        # slow tumble: mostly yaw with a little pitch and roll
        q1 = draw3d.q_create(math.radians(1.2), 0.25, 1.0, 0.12)
        draw3d.rotate(q1, 1)
        draw3d.reset(1)
        hdmi.fill(0)
        draw3d.show(1, 0, 0, Z, 1, 2)   # nonormals + hidden line: pure Elite
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    hdmi.close("F")
ms = time.ticks_diff(time.ticks_ms(), t0)
if ms > 0:
    print("elite: %d frames in %d ms (%.1f fps)" % (frames, ms, frames * 1000.0 / ms))
