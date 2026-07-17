# football.py -- Peter's DRAW3D test, converted from MMBasic: a truncated
# icosahedron (12 red pentagons, 20 white hexagons, black edges) tumbling
# and bouncing around the screen. Draws double-buffered: compose on F,
# copy to the display each frame.
import math
import random
import time

import hdmi
import draw3d

phi = (1 + math.sqrt(5)) / 2

# vertex coordinates for a truncated icosahedron of edge length 2
_V = []
for x, y, z in (
    (0, 1, 3 * phi), (0, 1, -3 * phi), (0, -1, 3 * phi), (0, -1, -3 * phi),
    (1, 3 * phi, 0), (1, -3 * phi, 0), (-1, 3 * phi, 0), (-1, -3 * phi, 0),
    (3 * phi, 0, 1), (3 * phi, 0, -1), (-3 * phi, 0, 1), (-3 * phi, 0, -1),
    (2, 1 + 2 * phi, phi), (2, 1 + 2 * phi, -phi), (2, -(1 + 2 * phi), phi),
    (2, -(1 + 2 * phi), -phi), (-2, 1 + 2 * phi, phi), (-2, 1 + 2 * phi, -phi),
    (-2, -(1 + 2 * phi), phi), (-2, -(1 + 2 * phi), -phi),
    (1 + 2 * phi, phi, 2), (1 + 2 * phi, phi, -2), (1 + 2 * phi, -phi, 2),
    (1 + 2 * phi, -phi, -2), (-(1 + 2 * phi), phi, 2), (-(1 + 2 * phi), phi, -2),
    (-(1 + 2 * phi), -phi, 2), (-(1 + 2 * phi), -phi, -2),
    (phi, 2, 1 + 2 * phi), (phi, 2, -(1 + 2 * phi)), (phi, -2, 1 + 2 * phi),
    (phi, -2, -(1 + 2 * phi)), (-phi, 2, 1 + 2 * phi), (-phi, 2, -(1 + 2 * phi)),
    (-phi, -2, 1 + 2 * phi), (-phi, -2, -(1 + 2 * phi)),
    (1, 2 + phi, 2 * phi), (1, 2 + phi, -2 * phi), (1, -(2 + phi), 2 * phi),
    (1, -(2 + phi), -2 * phi), (-1, 2 + phi, 2 * phi), (-1, 2 + phi, -2 * phi),
    (-1, -(2 + phi), 2 * phi), (-1, -(2 + phi), -2 * phi),
    (2 + phi, 2 * phi, 1), (2 + phi, 2 * phi, -1), (2 + phi, -2 * phi, 1),
    (2 + phi, -2 * phi, -1), (-(2 + phi), 2 * phi, 1), (-(2 + phi), 2 * phi, -1),
    (-(2 + phi), -2 * phi, 1), (-(2 + phi), -2 * phi, -1),
    (2 * phi, 1, 2 + phi), (2 * phi, 1, -(2 + phi)), (2 * phi, -1, 2 + phi),
    (2 * phi, -1, -(2 + phi)), (-2 * phi, 1, 2 + phi), (-2 * phi, 1, -(2 + phi)),
    (-2 * phi, -1, 2 + phi), (-2 * phi, -1, -(2 + phi)),
):
    _V.extend((x, y, z))

# 12 pentagons then 20 hexagons (vertex index lists, flat)
faces = [
    0, 28, 36, 40, 32,   33, 41, 37, 29, 1,   34, 42, 38, 30, 2,
    3, 31, 39, 43, 35,   4, 12, 44, 45, 13,   15, 47, 46, 14, 5,
    17, 49, 48, 16, 6,   7, 18, 50, 51, 19,   8, 20, 52, 54, 22,
    23, 55, 53, 21, 9,   26, 58, 56, 24, 10,  25, 57, 59, 27, 11,
    32, 56, 58, 34, 2, 0,    0, 2, 30, 54, 52, 28,   29, 53, 55, 31, 3, 1,
    1, 3, 35, 59, 57, 33,    13, 37, 41, 17, 6, 4,   4, 6, 16, 40, 36, 12,
    5, 7, 19, 43, 39, 15,    14, 38, 42, 18, 7, 5,   22, 46, 47, 23, 9, 8,
    8, 9, 21, 45, 44, 20,    10, 11, 27, 51, 50, 26, 24, 48, 49, 25, 11, 10,
    36, 28, 52, 20, 44, 12,  13, 45, 21, 53, 29, 37, 14, 46, 22, 54, 30, 38,
    39, 31, 55, 23, 47, 15,  16, 48, 24, 56, 32, 40, 41, 33, 57, 25, 49, 17,
    42, 34, 58, 26, 50, 18,  19, 51, 27, 59, 35, 43,
]

nf, nv, cam = 32, 60, 1
VRES, HRES = hdmi.height(), hdmi.width()

# scale the vertices: MM.VRES / max(vertices) * 0.3  (MMBasic MATH SCALE)
s = VRES / max(_V) * 0.3
vertices = [v * s for v in _V]

fc = [5] * 12 + [6] * 20
colours = [0xFF0000, 0xFFFFFF, 0x000000]        # red, white, black
edge = [2] * nf                                 # black edges
fill = [0] * 12 + [1] * 20                      # red pentagons, white hexagons

draw3d.close_all()
draw3d.create(1, nv, nf, cam, vertices, fc, faces, colours, edge, fill)
draw3d.camera(1, 800, 0, 0)
hdmi.close("F")
hdmi.create()
hdmi.write("F")

# bouncing state
px, py, vx, vy = 0.0, 0.0, 4.0, 3.0
sx, sy, sz, spd = 1.0, 3.0, 5.0, 0.25
radius = VRES * 0.2
xmax, ymax = HRES / 2 - radius, VRES / 2 - radius

frames = int(globals().get("FRAMES", 300))
draw3d.show(1, 0, 0, 1000)
t0 = time.ticks_ms()
for _ in range(frames):
    px += vx
    py += vy
    bounced = False
    if px > xmax:
        px, vx, bounced = xmax, -vx, True
    if px < -xmax:
        px, vx, bounced = -xmax, -vx, True
    if py > ymax:
        py, vy, bounced = ymax, -vy, True
    if py < -ymax:
        py, vy, bounced = -ymax, -vy, True
    if bounced:
        sx = random.random() * 6 - 3
        sy = random.random() * 6 - 3
        sz = random.random() * 6 - 3
        spd = random.random() * 2 + 0.1
    q1 = draw3d.q_create(math.radians(spd), sx, sy, sz)
    draw3d.rotate(q1, 1)
    draw3d.reset(1)
    hdmi.fill(0)
    draw3d.show(1, px, py, 1000)
    hdmi.copy("F", "N")
ms = time.ticks_diff(time.ticks_ms(), t0)
if ms > 0:
    print("football: %d frames in %d ms (%.1f fps)" % (frames, ms, frames * 1000.0 / ms))

try:
    hdmi.write("N")
finally:
    hdmi.close("F")
