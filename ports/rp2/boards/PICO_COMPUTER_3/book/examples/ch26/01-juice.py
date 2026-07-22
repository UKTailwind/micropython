import pcgame
import keyboard
import random
import time

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

screen(hdmi.RGB640)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()

hdmi.close("F")
hdmi.create()
hdmi.write("N")
# black margins for the shake to show
hdmi.fb().fill(0)
hdmi.write("F")
d = hdmi.fb()

BG = d.colour(0x101020)
INK = d.colour(WHITE)
SPARK = d.colour(ORANGE)

x, y = W / 2, H / 2
dx, dy = 160.0, 110.0
particles = []

paused = False
shake = 0.0                        # seconds of jolt left
flash = 0                          # frames of white left
freeze = 0                         # frames of held breath
space_was = p_was = True

clock = pcgame.Clock(vsync=True)
console("none")

try:
    while not held(keyboard.ESC):
        dt = clock.tick()
        space_now = held(ord(" "))
        p_now = held(ord("p"))
        space_pressed = space_now and not space_was
        p_pressed = p_now and not p_was
        space_was, p_was = space_now, p_now

        if p_pressed:
            paused = not paused
            if not paused:
                # don't "catch up" the missed frames
                clock.reset()

        if paused:
            # the world simply isn't updated
            pass
        elif freeze > 0:
            # hit-stop: drawn, but not moved
            freeze -= 1
        else:
            x += dx * dt
            y += dy * dt
            if x < 20 or x > W - 28:
                x = max(20, min(W - 28, x))
                dx = -dx
                beep(440, 10)
            if y < 20 or y > H - 28:
                y = max(20, min(H - 28, y))
                dy = -dy
                beep(440, 10)

            if space_pressed:      # THE IMPACT: all four at once
                shake = 0.25
                flash = 2
                freeze = 4
                for _ in range(14):
                    ang = random.randint(0, 359)
                    particles.append([x, y,
                                      random.randint(-160,
                                          160) * 1.0,
                                      random.randint(-160,
                                          160) * 1.0,
                                      random.randint(15,
                                          40) / 60])
                beep(150, 60)

            for p in particles:
                p[0] += p[2] * dt
                p[1] += p[3] * dt
                p[4] -= dt
            particles = [p for p in particles if p[4] > 0]
            shake = max(0.0, shake - dt)

        # --- draw the frame into F
        d.fill(BG)
        d.rect(16, 16, W - 32, H - 32, INK)
        for p in particles:
            d.fill_rect(int(p[0]), int(p[1]), 3, 3, SPARK)
        d.ellipse(int(x) + 4, int(y) + 4, 8, 8, INK, True)
        hdmi.text("SPACE impact   P pause   ESC quit", 24, H - 40,
                  d.colour(GRAY))
        if paused:
            hdmi.text("PAUSED", W // 2 - 96, H // 2 - 24, INK, -1,
                      2, 3)
        if flash > 0:
            d.fill(d.colour(WHITE))
            flash -= 1

        # --- flip, with the jolt
        ox = oy = 0
        if shake > 0:
            mag = int(shake * 36) + 1
            ox = random.randint(-mag, mag)
            oy = random.randint(-mag, mag)
        hdmi.vsync()
        hdmi.blit(0, 0, W, H, ox, oy, "F", "N")
finally:
    hdmi.write("N")
    console()
