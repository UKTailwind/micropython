import pcgame
import keyboard
import random
import time

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

# this game is laid out for 640x480
screen(hdmi.RGB640)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()

hdmi.close("F")
hdmi.create()
hdmi.write("F")
d = hdmi.fb()

BG = d.colour(0x080810)
INK = d.colour(WHITE)
DIM = d.colour(GRAY)

BW, BH = 72, 20                          # brick cell size
ROWS = [(RED, 50), (ORANGE, 40), (YELLOW, 30), (GREEN, 20), (CYAN,
        10)]
PW, PH = 80, 10                          # paddle
PY = H - 40                              # paddle's fixed height
BS = 8                                   # ball

def new_wall():
    bricks = []
    for row, (colour, points) in enumerate(ROWS):
        for col in range(8):
            bricks.append([18 + col * 76, 60 + row * 24,
                           d.colour(colour), points])
    return bricks

def serve(level):
    speed = min(200 * (1.0 + 0.15 * (level - 1)), 420)
    # from just beneath the wall...
    return (W / 2, 200.0,
            # ...a full second out
            random.randint(-100, 100) * 1.0, speed)

state = "title"
bricks = new_wall()
px = (W - PW) / 2
bx, by, bdx, bdy = serve(1)
score = 0
lives = 3
level = 1

clock = pcgame.Clock(vsync=True)
console("none")

try:
    while not held(keyboard.ESC):
        dt = clock.tick()

        if state == "title":
            if held(ord(" ")):
                score, lives, level = 0, 3, 1
                bricks = new_wall()
                bx, by, bdx, bdy = serve(1)
                px = (W - PW) / 2
                state = "play"
                clock.reset()

        elif state == "play":
            if held(keyboard.LEFT):
                px -= 420 * dt
            if held(keyboard.RIGHT):
                px += 420 * dt
            px = max(0, min(W - PW, px))

            bx += bdx * dt
            by += bdy * dt

            if bx < 0 or bx > W - BS:              # side walls
                bx = max(0, min(W - BS, bx))
                bdx = -bdx
                beep(440, 12)
            if by < 0:                             # ceiling
                by = 0
                bdy = -bdy
                beep(440, 12)

            # the paddle -- only on the way down
            if (bdy > 0 and PY - BS <= by <= PY
                and px - BS < bx < px + PW):
                rel = (bx + BS / 2 - px) / PW - 0.5
                bdx = rel * 2 * 320
                bdy = -abs(bdy)
                beep(880, 12)

            # the bricks -- at most one per frame
            for b in bricks:
                x, y, colour, points = b
                if x - BS < bx < x + BW and y - BS < by < y + BH:
                    from_x = min(bx + BS - x, x + BW - bx)
                    from_y = min(by + BS - y, y + BH - by)
                    # struck a side face
                    if from_x < from_y:
                        bdx = -bdx
                    # struck top or bottom
                    else:
                        bdy = -bdy
                    bricks.remove(b)
                    score += points
                    beep(400 + points * 10, 15)
                    break

            # level cleared!
            if not bricks:
                level += 1
                bricks = new_wall()
                bx, by, bdx, bdy = serve(level)
                beep(1320, 300)

            # lost below the paddle
            if by > H:
                lives -= 1
                beep(220, 300)
                if lives == 0:
                    state = "over"
                else:
                    bx, by, bdx, bdy = serve(level)

        elif state == "over":
            if held(ord(" ")):
                state = "title"

        # --- draw
        d.fill(BG)
        for x, y, colour, points in bricks:
            d.fill_rect(int(x), int(y), BW - 4, BH - 4, colour)
        d.fill_rect(int(px), PY, PW, PH, INK)
        if state == "play":
            d.fill_rect(int(bx), int(by), BS, BS, INK)
        hdmi.text(f"SCORE {score:5}", 8, 8, DIM, -1, 1, 3)
        hdmi.text(f"LEVEL {level}", W - 260, 8, DIM, -1, 1, 3)
        for i in range(lives):
            d.fill_rect(W - 20 - i * 16, 14, 12, 6, INK)
        if state == "title":
            hdmi.text("B R E A K O U T", W // 2 - 240, 200, INK,
                      -1, 2, 3)
            hdmi.text("LEFT/RIGHT to steer -- SPACE to start",
                      W // 2 - 148, 280, DIM)
        elif state == "over":
            hdmi.text("GAME OVER", W // 2 - 144, 200, INK, -1, 2,
                      3)
            hdmi.text("SPACE to try again", W // 2 - 72, 280, DIM)
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
