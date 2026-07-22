import pcgame
import keyboard
import math
import random
import time

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

def pt(cx, cy, ang, dist):
    r = math.radians(ang)
    return cx + math.sin(r) * dist, cy - math.cos(r) * dist

screen(hdmi.RGB320)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()

hdmi.close("F")
hdmi.create()
hdmi.write("F")
d = hdmi.fb()

BG = d.colour(0x000008)
INK = d.colour(WHITE)
FLAME = d.colour(ORANGE)
STAR = d.colour(GRAY)

random.seed(4)
stars = [(random.randint(0, W - 1), random.randint(0, H - 1))
         for _ in range(60)]

x, y = W / 2, H / 2
vx = vy = 0.0
# heading, degrees, 0 = up
a = 0.0

clock = pcgame.Clock(vsync=True)
console("none")

try:
    while not held(keyboard.ESC):
        dt = clock.tick()

        if held(keyboard.LEFT):
            a -= 220 * dt
        if held(keyboard.RIGHT):
            a += 220 * dt
        thrusting = held(keyboard.UP)
        if thrusting:
            r = math.radians(a)
            # thrust adds to VELOCITY
            vx += math.sin(r) * 240 * dt
            vy -= math.cos(r) * 240 * dt

        # a whisper of drag
        vx *= 1 - 0.3 * dt
        vy *= 1 - 0.3 * dt
        x = (x + vx * dt) % W                    # space wraps
        y = (y + vy * dt) % H

        d.fill(BG)
        for sx, sy in stars:
            d.pixel(sx, sy, STAR)
        nx, ny = pt(x, y, a, 11)                 # nose
        lx, ly = pt(x, y, a + 140, 9)            # wingtips
        rx, ry = pt(x, y, a - 140, 9)
        d.line(int(nx), int(ny), int(lx), int(ly), INK, 1)
        d.line(int(nx), int(ny), int(rx), int(ry), INK, 1)
        d.line(int(lx), int(ly), int(rx), int(ry), INK, 1)
        if thrusting:
            fx, fy = pt(x, y, a + 180, 8)
            d.line(int(x), int(y), int(fx), int(fy), FLAME, 1)
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
    screen(hdmi.RGB640)
