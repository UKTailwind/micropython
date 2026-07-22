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

BG = d.colour(0x081018)
INK = d.colour(WHITE)
DIM = d.colour(GRAY)

PW, PH, BS = 8, 64, 8
PSPEED = 320.0
WIN = 5

def serve(direction):
    """Centre the ball, heading toward `direction` (+1 right, -1
    left)."""
    return (W / 2, H / 2,
            direction * 220.0, random.randint(-140, 140) * 1.0)

p1y = p2y = (H - PH) / 2
bx, by, bdx, bdy = serve(1)
s1 = s2 = 0
state = "title"

clock = pcgame.Clock(vsync=True)
console("none")

try:
    while not held(keyboard.ESC):
        dt = clock.tick()

        if state == "title":
            if held(ord(" ")):
                s1 = s2 = 0
                bx, by, bdx, bdy = serve(1)
                state = "play"
                clock.reset()

        elif state == "play":
            if held(ord("w")):
                p1y -= PSPEED * dt
            if held(ord("s")):
                p1y += PSPEED * dt
            if held(keyboard.UP):
                p2y -= PSPEED * dt
            if held(keyboard.DOWN):
                p2y += PSPEED * dt
            p1y = max(0, min(H - PH, p1y))
            p2y = max(0, min(H - PH, p2y))

            bx += bdx * dt
            by += bdy * dt
            if by < 0 or by > H - BS:
                by = max(0, min(H - BS, by))
                bdy = -bdy
                beep(440, 15)

            if (bdx < 0 and 16 <= bx <= 16 + PW
                and p1y - BS < by < p1y + PH):
                # every return, faster
                bdx = -bdx * 1.04
                bdy = 260 * ((by + BS / 2 - p1y) / PH - 0.5) * 2
                beep(880, 15)
            if (bdx > 0 and W - 24 - BS <= bx <= W - 16
                and p2y - BS < by < p2y + PH):
                bdx = -bdx * 1.04
                bdy = 260 * ((by + BS / 2 - p2y) / PH - 0.5) * 2
                beep(880, 15)

            # past the left edge: P2 scores
            if bx < -BS:
                s2 += 1
                beep(220, 200)
                bx, by, bdx, bdy = serve(-1)      # loser receives
            # past the right: P1 scores
            elif bx > W:
                s1 += 1
                beep(220, 200)
                bx, by, bdx, bdy = serve(1)
            if s1 == WIN or s2 == WIN:
                state = "over"

        elif state == "over":
            if held(ord(" ")):
                state = "title"

        # --- draw (every state draws the court; some add words)
        d.fill(BG)
        for y in range(0, H, 24):
            d.fill_rect(W // 2 - 2, y, 4, 12, INK)
        d.fill_rect(16, int(p1y), PW, PH, INK)
        d.fill_rect(W - 16 - PW, int(p2y), PW, PH, INK)
        hdmi.text(str(s1), W // 2 - 96, 24, DIM, -1, 1, 6)
        hdmi.text(str(s2), W // 2 + 64, 24, DIM, -1, 1, 6)
        if state == "play":
            d.fill_rect(int(bx), int(by), BS, BS, INK)
        elif state == "title":
            hdmi.text("P O N G", W // 2 - 112, 180, INK, -1, 2, 3)
            hdmi.text("W/S and UP/DOWN -- first to 5 -- SPACE to "
                      "start",
                      W // 2 - 188, 260, DIM)
        elif state == "over":
            champ = "PLAYER 1" if s1 == WIN else "PLAYER 2"
            hdmi.text(champ + " WINS", W // 2 - 176, 180, INK, -1,
                      2, 3)
            hdmi.text("SPACE for a rematch", W // 2 - 76, 260,
                      DIM)
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
