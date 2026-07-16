import pcgame
import keyboard
import time

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

screen(hdmi.RGB640)                 # this game is laid out for 640x480
time.sleep(3)

W = hdmi.width()
H = hdmi.height()

hdmi.close("F")
hdmi.create()
hdmi.write("F")
d = hdmi.fb()

BG = d.colour(0x081018)
INK = d.colour(WHITE)

PW, PH = 8, 64                      # paddle size
BS = 8                              # ball size
PSPEED = 320.0                      # pixels per second
p1y = p2y = (H - PH) / 2
bx, by = W / 2, H / 2
bdx, bdy = 220.0, 120.0             # ball velocity, per second

clock = pcgame.Clock(vsync=True)
console("none")

try:
    while not held(keyboard.ESC):
        dt = clock.tick()

        # --- input
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

        # --- update
        bx += bdx * dt
        by += bdy * dt
        if by < 0 or by > H - BS:            # top and bottom walls
            by = max(0, min(H - BS, by))
            bdy = -bdy
            beep(440, 15)
        if bx < 0 or bx > W - BS:            # side walls (for now!)
            bx = max(0, min(W - BS, bx))
            bdx = -bdx
            beep(440, 15)

        # paddle faces: deflect, and steer by where the ball struck
        if bdx < 0 and 16 <= bx <= 16 + PW and p1y - BS < by < p1y + PH:
            bdx = -bdx
            bdy = 260 * ((by + BS / 2 - p1y) / PH - 0.5) * 2
            beep(880, 15)
        if bdx > 0 and W - 24 - BS <= bx <= W - 16 and p2y - BS < by < p2y + PH:
            bdx = -bdx
            bdy = 260 * ((by + BS / 2 - p2y) / PH - 0.5) * 2
            beep(880, 15)

        # --- draw
        d.fill(BG)
        for y in range(0, H, 24):            # the classic dashed net
            d.fill_rect(W // 2 - 2, y, 4, 12, INK)
        d.fill_rect(16, int(p1y), PW, PH, INK)
        d.fill_rect(W - 16 - PW, int(p2y), PW, PH, INK)
        d.fill_rect(int(bx), int(by), BS, BS, INK)
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
