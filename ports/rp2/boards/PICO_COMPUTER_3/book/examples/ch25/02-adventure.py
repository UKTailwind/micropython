import pcgame
import random
import keyboard
import time
from pcimage import load_image

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

screen(hdmi.RGB640)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()
d = hdmi.fb()

GRASS, WATER, TREE, ROCK, FLOWER, HERO, ELDER, SHARD = range(1, 9)
SOLID = 1
SAVE = "/adventure.sav"

sheet = load_image("tiles2.bmp")
tm = TileMap(sheet, 16, 16, cols=64, rows=48)

# --- the island (fixed seed: everyone's island is this island)
random.seed(12)
tm.fill(GRASS)
for col in range(64):
    for row in (0, 1, 46, 47):
        tm.set(col, row, WATER)
for row in range(48):
    for col in (0, 1, 62, 63):
        tm.set(col, row, WATER)
for _ in range(110):
    tm.set(random.randint(2, 61), random.randint(2, 45), TREE)
for _ in range(30):
    tm.set(random.randint(2, 61), random.randint(2, 45), ROCK)
for _ in range(30):
    tm.set(random.randint(2, 61), random.randint(2, 45), FLOWER)
for _ in range(5):                                # the five shards
    tm.set(random.randint(4, 59), random.randint(4, 43), SHARD)
for c in range(30, 36):                           # the village clearing
    for r in range(22, 27):
        tm.set(c, r, GRASS)

tm.set_attr(WATER, SOLID)
tm.set_attr(TREE, SOLID)
tm.set_attr(ROCK, SOLID)
tm.clamp(W, H)

# --- the cast
px, py = 32 * 16.0, 25 * 16.0                     # the hero
ex, ey = 33 * 16.0, 23 * 16.0                     # the elder...
E_LEFT, E_RIGHT, espeed = 31 * 16.0, 35 * 16.0, 22.0   # ...and her patrol

def shards_left():
    return sum(row.count(SHARD) for row in
               [[tm.get(c, r) for c in range(64)] for r in range(48)])

def save_game():
    with open(SAVE, "w") as f:
        f.write(f"{int(px)},{int(py)}\n")
        cells = []
        for r in range(48):
            for c in range(64):
                if tm.get(c, r) == SHARD:
                    cells.append(f"{c}:{r}")
        f.write(";".join(cells) + "\n")

def load_game():
    """Apply a saved game. Returns the restored (px, py), or None."""
    try:
        with open(SAVE) as f:
            pos = f.readline().strip().split(",")
            cellline = f.readline().strip()
    except OSError:
        return None
    for r in range(48):                           # strip the fresh shards...
        for c in range(64):
            if tm.get(c, r) == SHARD:
                tm.set(c, r, GRASS)
    if cellline:                                  # ...and lay the saved ones
        for cell in cellline.split(";"):
            c, r = cell.split(":")
            tm.set(int(c), int(r), SHARD)
    return float(pos[0]), float(pos[1])

def save_exists():
    try:
        with open(SAVE) as f:
            pass
        return True
    except OSError:
        return False

def elder_lines():
    n = 5 - shards_left()
    if n == 0:
        return ["Traveller! The storm broke my amulet",
                "and scattered five shards across the island.",
                "Bring them home, please."]
    if n < 5:
        return [f"You carry {n} of the five shards.",
                "The forest hides the rest. Keep looking!"]
    return ["My amulet... whole again!",
            "The island thanks you, hero."]

state = "title"
talk_lines = []
talk_i = 0
space_was = True                                  # held from the title press
e_was = False
edir = 1

clock = pcgame.Clock(vsync=True)
console("none")

try:
    while not held(keyboard.ESC):
        dt = clock.tick()
        space_now = held(ord(" "))
        e_now = held(ord("e"))
        space_pressed = space_now and not space_was
        e_pressed = e_now and not e_was
        space_was, e_was = space_now, e_now

        if state == "title":
            if held(ord("n")):
                state = "play"
                clock.reset()
            elif space_pressed and save_exists():
                loaded = load_game()
                if loaded:
                    px, py = loaded
                state = "play"
                clock.reset()

        elif state == "play":
            nx, ny = px, py
            if held(keyboard.LEFT):
                nx -= 120 * dt
            if held(keyboard.RIGHT):
                nx += 120 * dt
            if held(keyboard.UP):
                ny -= 120 * dt
            if held(keyboard.DOWN):
                ny += 120 * dt
            if not tm.collide(nx, py, 14, 14, mask=SOLID):
                px = nx
            if not tm.collide(px, ny, 14, 14, mask=SOLID):
                py = ny

            if tm.tile_at(px + 7, py + 7) == SHARD:      # pick a shard up
                tm.set(int(px + 7) // 16, int(py + 7) // 16, GRASS)
                beep(880 + (5 - shards_left()) * 110, 60)

            ex += espeed * edir * dt                     # the elder's patrol
            if ex < E_LEFT or ex > E_RIGHT:
                ex = max(E_LEFT, min(E_RIGHT, ex))
                edir = -edir

            near = (px - ex) ** 2 + (py - ey) ** 2 < 30 * 30
            if e_pressed and near:
                talk_lines = elder_lines()
                talk_i = 0
                state = "talk"

            if held(ord("q")):                           # save and rest
                save_game()
                break

        elif state == "talk":
            if space_pressed:
                talk_i += 1
                if talk_i >= len(talk_lines):
                    if shards_left() == 0:
                        try:
                            import os
                            os.remove(SAVE)              # the tale is told
                        except OSError:
                            pass
                        state = "win"
                    else:
                        state = "play"

        elif state == "win":
            if space_pressed:
                break

        # --- draw (the world shows under every state)
        vx = px + 7 - W // 2
        vy = py + 7 - H // 2
        tm.view(vx, vy)
        cvx = max(0, min(vx, 64 * 16 - W))
        cvy = max(0, min(vy, 48 * 16 - H))
        hdmi.write("F")
        tm.draw()
        tm.blit_tile(ELDER, int(ex - cvx), int(ey - cvy), skip=d.colour(BLACK))
        tm.blit_tile(HERO, int(px - cvx), int(py - cvy), skip=d.colour(BLACK))
        hdmi.text(f"shards: {5 - shards_left()}/5", 8, 8,
                  d.colour(WHITE), d.colour(BLACK))
        if state == "title":
            d.rbox(120, 160, 400, 150, 12, d.colour(GOLD), d.colour(0x102030))
            hdmi.text("THE FIVE SHARDS", 200, 190, d.colour(GOLD), -1, 1, 3)
            if save_exists():
                hdmi.text("SPACE continue    N new game", 208, 240,
                          d.colour(WHITE))
            else:
                hdmi.text("N to begin", 280, 240, d.colour(WHITE))
            hdmi.text("arrows walk  E talk  Q save+quit", 192, 270,
                      d.colour(GRAY))
        elif state == "talk":
            d.rbox(60, H - 120, W - 120, 90, 10,
                   d.colour(GOLD), d.colour(0x102030))
            hdmi.text(talk_lines[min(talk_i, len(talk_lines) - 1)],
                      80, H - 96, d.colour(WHITE))
            hdmi.text("SPACE", W - 130, H - 52, d.colour(GRAY))
        elif state == "win":
            d.rbox(140, 160, 360, 120, 12, d.colour(GOLD), d.colour(0x102030))
            hdmi.text("THE AMULET IS WHOLE", 168, 195, d.colour(GOLD), -1, 1, 3)
            hdmi.text("SPACE to rest", 268, 245, d.colour(WHITE))
        hdmi.write("N")
        hdmi.vsync()
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
