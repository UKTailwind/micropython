import pcsprite as sp
import keyboard

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

SKY = d.colour(0x203050)
d.fill(SKY)
d.fill_rect(0, H - 60, W, 60, d.colour(MIDGREEN))          # grassy bank
hdmi.text("SHOOTING GALLERY", 192, H - 40, d.colour(GOLD), -1, 1, 3)

def grab_and_wipe(w, h):
    s = sp.grab(0, 0, w, h, transparent=SKY)
    d.fill_rect(0, 0, w, h, SKY)
    return s

def make_duck():
    d.fill_rect(0, 0, 28, 18, SKY)
    d.ellipse(10, 9, 9, 7, d.colour(YELLOW), True)
    d.ellipse(20, 5, 5, 4, d.colour(YELLOW), True)
    d.fill_rect(24, 4, 4, 2, d.colour(ORANGE))
    return grab_and_wipe(28, 18)

def make_cross():
    d.fill_rect(0, 0, 17, 17, SKY)
    d.ellipse(8, 8, 7, 7, d.colour(RED))
    d.line(8, 0, 8, 16, d.colour(RED), 1)
    d.line(0, 8, 16, 8, d.colour(RED), 1)
    return grab_and_wipe(17, 17)

def make_shot():
    d.fill_rect(0, 0, 5, 5, d.colour(WHITE))
    return grab_and_wipe(5, 5)

ducks = []
for i in range(3):
    duck = make_duck()
    duck.speed = 2 + i                     # objects accept new attributes!
    duck.show(i * 200, 70 + i * 80, layer=1)
    ducks.append(duck)

cross = make_cross()
cross.show(W // 2, H // 2, layer=2)
shot = make_shot()                         # made, not shown

score = 0
cooldown = 0

def hud():
    hdmi.text(f"SCORE {score:3}", 8, 8, d.colour(WHITE), SKY, 1, 3)

hud()
console("none")

try:
    while not held(keyboard.ESC):
        if held(keyboard.LEFT):
            cross.x = max(0, cross.x - 4)
        if held(keyboard.RIGHT):
            cross.x = min(W - 17, cross.x + 4)
        if held(keyboard.UP):
            cross.y = max(0, cross.y - 4)
        if held(keyboard.DOWN):
            cross.y = min(H - 17, cross.y + 4)

        if cooldown:
            cooldown -= 1
        if held(ord(" ")) and cooldown == 0:
            shot.x = cross.x + 6           # centre the 5x5 shot
            shot.y = cross.y + 6
            shot.show(shot.x, shot.y, layer=1)
            cooldown = 15                  # quarter-second between shots
            beep(220, 20)

        for duck in ducks:
            duck.x += duck.speed
            if duck.x > W:
                duck.x = -28

        for a, b in sp.update(vsync=True):
            if a is shot or b is shot:
                target = b if a is shot else a
                if target in ducks:
                    score += 1
                    beep(880, 30)
                    target.x = -28         # respawn at the left
                    hud()

        shot.hide()                        # the shot exists for one frame
finally:
    sp.reset()
    console()
