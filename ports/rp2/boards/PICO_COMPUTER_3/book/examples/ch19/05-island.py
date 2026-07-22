import random
import keyboard
import time
from pcimage import load_image

screen(hdmi.RGB640)
time.sleep(3)

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

W = hdmi.width()
H = hdmi.height()
d = hdmi.fb()

GRASS, WATER, TREE, ROCK, FLOWER, HERO = 1, 2, 3, 4, 5, 6
SOLID = 1

sheet = load_image("tiles.bmp")
tm = TileMap(sheet, 16, 16, cols=64, rows=48)     # 1024x768 world

# --- generate the island
# the book's island; change me
random.seed(12)
tm.fill(GRASS)
# ocean border, two deep
for col in range(64):
    for row in (0, 1, 46, 47):
        tm.set(col, row, WATER)
for row in range(48):
    for col in (0, 1, 62, 63):
        tm.set(col, row, WATER)
for _ in range(90):                               # forest
    tm.set(random.randint(2, 61), random.randint(2, 45), TREE)
for _ in range(25):                               # boulders
    tm.set(random.randint(2, 61), random.randint(2, 45), ROCK)
for _ in range(40):                               # flowers
    tm.set(random.randint(2, 61), random.randint(2, 45), FLOWER)
# clear ground at the start
for col in (31, 32, 33):
    for row in (23, 24, 25):
        tm.set(col, row, GRASS)

tm.set_attr(WATER, SOLID)
tm.set_attr(TREE, SOLID)
tm.set_attr(ROCK, SOLID)
tm.clamp(W, H)

# start mid-island
px, py = 32 * 16, 24 * 16
picked = 0
BLACK_C = d.colour(BLACK)

hdmi.close("F")
hdmi.create()
console("none")

try:
    while not held(keyboard.ESC):
        # try the move; keep it if the world allows
        nx, ny = px, py
        if held(keyboard.LEFT):
            nx -= 2
        if held(keyboard.RIGHT):
            nx += 2
        if held(keyboard.UP):
            ny -= 2
        if held(keyboard.DOWN):
            ny += 2
        if not tm.collide(nx, py, 14, 14, mask=SOLID):
            px = nx
        if not tm.collide(px, ny, 14, 14, mask=SOLID):
            py = ny

        # standing in flowers? pick them
        if tm.tile_at(px + 7, py + 7) == FLOWER:
            tm.set((px + 7) // 16, (py + 7) // 16, GRASS)
            picked += 1
            beep(880, 15)

        # camera follows, map-clamped
        vx = px + 7 - W // 2
        vy = py + 7 - H // 2
        tm.view(vx, vy)

        # compose and flip
        hdmi.write("F")
        tm.draw()
        tm.blit_tile(HERO, px - max(0, min(vx, 1024 - W)),
                     py - max(0, min(vy, 768 - H)), skip=BLACK_C)
        hdmi.text(f"flowers: {picked}", 8, 8, d.colour(WHITE),
                  BLACK_C)
        hdmi.write("N")
        hdmi.vsync()
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
