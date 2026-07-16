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
ROCKC = d.colour(LITEGRAY)

RADIUS = {3: 15, 2: 9, 1: 5}            # rock size -> radius
POINTS = {3: 20, 2: 50, 1: 100}         # small rocks pay best

def make_rocks(n):
    rocks = []
    for _ in range(n):                  # spawn on the border: centre is safe
        if random.randint(0, 1):
            rx, ry = random.randint(0, W - 1), 0
        else:
            rx, ry = 0, random.randint(0, H - 1)
        rocks.append([rx * 1.0, ry * 1.0,
                      random.randint(-60, 60) * 1.0,
                      random.randint(-60, 60) * 1.0, 3])
    return rocks

def burst(x, y, n):
    for _ in range(n):
        ang = random.randint(0, 359)
        speed = random.randint(40, 140)
        r = math.radians(ang)
        particles.append([x, y, math.sin(r) * speed, -math.cos(r) * speed,
                          random.randint(20, 45) / 60])

random.seed()                            # different rocks every game
stars = [(random.randint(0, W - 1), random.randint(0, H - 1))
         for _ in range(60)]

state = "title"
rocks = make_rocks(4)
bullets = []
particles = []
x, y = W / 2, H / 2
vx = vy = 0.0
a = 0.0
score = 0
lives = 3
wave = 1
cooldown = 0.0
shield = 0.0                             # seconds of respawn safety
thrusting = False

try:
    play("/sd/asteroids.mod", loop=True) # soundtrack, if one's aboard
except OSError:
    pass

clock = pcgame.Clock(vsync=True)
console("none")

try:
    while not held(keyboard.ESC):
        dt = clock.tick()

        if state == "title":
            if held(ord(" ")):
                score, lives, wave = 0, 3, 1
                rocks = make_rocks(4)
                bullets = []
                particles = []
                x, y, vx, vy, a = W / 2, H / 2, 0.0, 0.0, 0.0
                shield = 2.0
                state = "play"
                clock.reset()

        elif state == "play":
            if held(keyboard.LEFT):
                a -= 220 * dt
            if held(keyboard.RIGHT):
                a += 220 * dt
            thrusting = held(keyboard.UP)
            if thrusting:
                r = math.radians(a)
                vx += math.sin(r) * 240 * dt
                vy -= math.cos(r) * 240 * dt

            cooldown = max(0.0, cooldown - dt)
            shield = max(0.0, shield - dt)
            if held(ord(" ")) and cooldown == 0 and len(bullets) < 4:
                nx, ny = pt(x, y, a, 11)
                r = math.radians(a)
                bullets.append([nx, ny,
                                vx + math.sin(r) * 260,
                                vy - math.cos(r) * 260, 0.9])
                cooldown = 0.25
                beep(1568, 12)

            vx *= 1 - 0.3 * dt
            vy *= 1 - 0.3 * dt
            x = (x + vx * dt) % W
            y = (y + vy * dt) % H

            for b in bullets:
                b[0] = (b[0] + b[2] * dt) % W
                b[1] = (b[1] + b[3] * dt) % H
                b[4] -= dt
            bullets = [b for b in bullets if b[4] > 0]

            for rk in rocks:
                rk[0] = (rk[0] + rk[2] * dt) % W
                rk[1] = (rk[1] + rk[3] * dt) % H

            for p in particles:
                p[0] += p[2] * dt
                p[1] += p[3] * dt
                p[4] -= dt
            particles = [p for p in particles if p[4] > 0]

            # bullets vs rocks: circle test, no square roots needed
            for b in bullets:
                for rk in rocks:
                    rr = RADIUS[rk[4]]
                    if (b[0] - rk[0]) ** 2 + (b[1] - rk[1]) ** 2 < rr * rr:
                        b[4] = 0                     # bullet spent
                        score += POINTS[rk[4]]
                        burst(rk[0], rk[1], 10)
                        beep(180, 25)
                        if rk[4] > 1:                # big rocks split in two
                            for _ in range(2):
                                rocks.append([rk[0], rk[1],
                                              random.randint(-90, 90) * 1.0,
                                              random.randint(-90, 90) * 1.0,
                                              rk[4] - 1])
                        rocks.remove(rk)
                        break
            bullets = [b for b in bullets if b[4] > 0]

            # ship vs rocks
            if shield == 0:
                for rk in rocks:
                    rr = RADIUS[rk[4]] + 6
                    if (x - rk[0]) ** 2 + (y - rk[1]) ** 2 < rr * rr:
                        lives -= 1
                        burst(x, y, 24)
                        beep(120, 400)
                        x, y, vx, vy, a = W / 2, H / 2, 0.0, 0.0, 0.0
                        shield = 2.0
                        if lives == 0:
                            state = "over"
                        break

            if not rocks:                            # wave cleared
                wave += 1
                rocks = make_rocks(3 + wave)
                shield = 2.0
                beep(1320, 250)

        elif state == "over":
            if held(ord(" ")):
                state = "title"

        # --- draw
        d.fill(BG)
        for sx, sy in stars:
            d.pixel(sx, sy, STAR)
        for p in particles:
            d.pixel(int(p[0]) % W, int(p[1]) % H, FLAME)
        for rk in rocks:
            d.ellipse(int(rk[0]), int(rk[1]), RADIUS[rk[4]], RADIUS[rk[4]],
                      ROCKC)
        for b in bullets:
            d.fill_rect(int(b[0]), int(b[1]), 2, 2, INK)
        if state == "play" and (shield == 0 or int(shield * 8) % 2 == 0):
            nx, ny = pt(x, y, a, 11)
            lx, ly = pt(x, y, a + 140, 9)
            rx2, ry2 = pt(x, y, a - 140, 9)
            d.line(int(nx), int(ny), int(lx), int(ly), INK, 1)
            d.line(int(nx), int(ny), int(rx2), int(ry2), INK, 1)
            d.line(int(lx), int(ly), int(rx2), int(ry2), INK, 1)
            if thrusting:
                fx, fy = pt(x, y, a + 180, 8)
                d.line(int(x), int(y), int(fx), int(fy), FLAME, 1)
        hdmi.text(f"{score:5}", 8, 6, INK, -1, 1, 3)
        hdmi.text(f"WAVE {wave}", W - 76, 6, STAR)
        for i in range(lives):
            d.line(12 + i * 12, 36, 8 + i * 12, 44, INK, 1)
            d.line(12 + i * 12, 36, 16 + i * 12, 44, INK, 1)
        if state == "title":
            hdmi.text("A S T E R O I D S", 24, 90, INK, -1, 1, 3)
            hdmi.text("arrows steer - space fires", 56, 140, STAR)
        elif state == "over":
            hdmi.text("GAME OVER", 88, 90, INK, -1, 1, 3)
            hdmi.text("space to try again", 88, 140, STAR)
        hdmi.copy("F", "N")
finally:
    stop()
    hdmi.write("N")
    console()
    screen(hdmi.RGB640)
