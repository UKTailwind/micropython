# Chapter 25 — The adventure: NPCs, dialogue and saved worlds

The last three games were arcade machines: sit down, play, lose,
again. This chapter builds the other kind — a *place*: an island with
an inhabitant, an errand, and a save file, so that switching off means
*pausing a world* rather than ending one. Nothing in it is new
machinery — it is chapter 19's island, chapter 22's state ladder and
chapter 12's files, composed — and that is the chapter's real thesis:
**past a certain point, programming is architecture, not vocabulary.**

Three genuinely new *patterns* do arrive, all small: the just-pressed
key test, dialogue as data driven by a state, and the save file that
lets a title screen say *Continue*.

## The forge, extended

Two new residents join chapter 19's tileset: the **elder** (tile 7)
and the **amulet shard** (tile 8). Same studio, two more benches —
this listing supersedes `maketiles.py`. `edit("maketiles2.py")`:

```python
d = hdmi.fb()
d.fill(d.colour(BLACK))

GRASS = d.colour(0x1E7A1E)
DARK = d.colour(0x156015)

# tile 1: grass
d.fill_rect(0, 0, 16, 16, GRASS)
for i in range(6):
    d.pixel(2 + i * 2, (i * 5) % 16, DARK)

# tile 2: water
d.fill_rect(16, 0, 16, 16, d.colour(0x1040A0))
d.hline(18, 5, 10, d.colour(0x3070D0))
d.hline(20, 11, 10, d.colour(0x3070D0))

# tile 3: tree
d.fill_rect(32, 0, 16, 16, GRASS)
d.fill_rect(39, 10, 3, 5, d.colour(BROWN))
d.ellipse(40, 6, 6, 6, d.colour(MYRTLE), True)

# tile 4: rock
d.fill_rect(48, 0, 16, 16, GRASS)
d.rbox(50, 4, 12, 10, 3, d.colour(GRAY), d.colour(GRAY))

# tile 5: flowers
d.fill_rect(64, 0, 16, 16, GRASS)
for fx, fy in ((68, 4), (74, 9), (69, 12)):
    d.pixel(fx, fy - 1, d.colour(MAGENTA))
    d.pixel(fx - 1, fy, d.colour(MAGENTA))
    d.pixel(fx + 1, fy, d.colour(MAGENTA))
    d.pixel(fx, fy + 1, d.colour(MAGENTA))
    d.pixel(fx, fy, d.colour(YELLOW))

# tile 6: the hero (on black = cut-out)
d.fill_rect(84, 6, 8, 7, d.colour(GOLD))
d.fill_rect(85, 1, 6, 5, d.colour(LITEGRAY))
d.fill_rect(84, 13, 3, 3, d.colour(RED))
d.fill_rect(89, 13, 3, 3, d.colour(RED))

# tile 7: the elder (blue robe, white beard)
d.fill_rect(100, 5, 8, 9, d.colour(COBALT))
d.fill_rect(101, 1, 6, 5, d.colour(LITEGRAY))
d.fill_rect(102, 5, 4, 3, d.colour(WHITE))
d.fill_rect(100, 14, 8, 2, d.colour(GRAY))

# tile 8: an amulet shard (on grass, so it sits in the world)
d.fill_rect(112, 0, 16, 16, GRASS)
d.fill_rect(117, 4, 6, 6, d.colour(GOLD))
d.fill_rect(119, 2, 2, 2, d.colour(GOLD))
d.fill_rect(118, 10, 4, 3, d.colour(YELLOW))

save_image("tiles2.bmp")
print("tileset saved: 8 tiles")
```

## The game

`edit("adventure.py")` — read the dissection after your first errand:

```python
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
```

Walk to the clearing. Talk to the elder (E, then SPACE through her
lines). Find shards by their glint. Press Q mid-errand, run the
program again, and choose *continue* — the island remembers. That
moment is the chapter.

## The just-pressed pattern

The game's one new input idea, and it matters everywhere. `held()`
answers *is the key down?* — sixty times a second. If SPACE advanced
dialogue on `held`, one tap would blast through every line in four
frames. What dialogue needs is *was it pressed this frame?*:

```python
space_now = held(ord(" "))
space_pressed = space_now and not space_was
space_was = space_now
```

Down *now* and not down *last frame* — an edge, not a level (the same
distinction chapter 18's collision events drew). Note the sly
initialisation `space_was = True`: the SPACE that chose *continue* on
the title screen is still physically held on the first play frame,
and without that line it would immediately count as "pressed" inside
whatever came next. Carrying a key across a state change is the
classic bug of state machines; now you've met it in daylight.

## Dialogue is a state, and the words are data

The `talk` state runs *instead of* `play` in the ladder — so while
the elder speaks, nobody moves, nothing collides, shards stay put:
the world freezes because its update simply isn't reached. And the
words come from `elder_lines()`, which builds them **from the world's
state** — the shard count decides whether she pleads, encourages or
rejoices. One NPC, three conversations, no flags: the dialogue is
*derived*. (Which is this chapter's quiet design principle — see the
save file next.)

The elder herself is fifteen lines: a position, a patrol between two
waypoints, and an interaction radius (`30 * 30`, squared distance,
chapter 24's trick). NPCs in games a hundred times this size are the
same three things with better wardrobes.

## The save file: derive, don't store

Look at what `save_game` writes: the hero's position, and *where the
remaining shards are*. Not the score, not the "stage", not the
elder's mood — all of those are **derived** from the world every time
they're needed (`shards_left()` counts the map; `elder_lines()` reads
the count). The less a save file stores, the less can disagree with
itself — a corrupted or hand-edited save (chapter 12, experiment 4!)
can misplace a shard, but it cannot claim six-of-five.

The mechanics are pure chapter 12 — one `write` per line, `split` on
the way back, `try/except OSError` making *no save file* a normal,
handled case — plus two manners worth copying: the title screen only
offers *continue* when a save exists, and victory **deletes** the
save, so the tale, once told, is done.

## What this chapter proves

Count the machinery: `TileMap` (19), the F-flip and collar (17), the
state ladder and `dt` (22), squared-distance (24), text files (12),
the just-pressed test (new, eight lines). The *game* — the errand,
the patrol, the conversation that knows how your quest is going, the
world that waits for you — is those parts *arranged*. You are no
longer learning to program; you are designing. Part V takes exactly
this skill to applications.

## Experiments

1. Give the island a second NPC — a goat: a position, a random
   heading changed every few seconds, `blit_tile` of the rock... no,
   draw a goat tile in the forge (tile 9 — the studio is yours).
   No dialogue: proximity makes it *bleat* (`beep`) and trot away.
   Ambience is cheap and worth everything.
2. Signposts: when `tile_at` under the hero is a FLOWER, show a
   one-line hint bar ("the elder waits in the clearing") — dialogue's
   little sibling, no state needed. Where should the *text* live?
   (A dict keyed by tile — the words stay data.)
3. Break the save on purpose (chapter 13's ritual): edit
   `/adventure.sav`, misplace a shard into the ocean, reload. What
   *should* the game do about an unreachable shard? Design an answer,
   then implement your favourite (shards float? regenerate on land?
   elder accepts four-of-five with a sigh?).
4. The patrol is a metronome — make it *pause*: the elder stands
   still while you're near (`near` already knows). Politeness, in
   four lines, and the game feels startlingly more alive.
5. Autosave: call `save_game()` on every shard pickup. Now Q is just
   *quit* — and pulling the power loses nothing. What did that cost?
   (Measure: `ticks_diff` around the call — chapter 21.)

## Challenges

1. **The second quest.** After the amulet, the elder wants flowers —
   five of those — and the win becomes a *stage* that must now be
   stored (derive-don't-store meets its limit: the amulet's shards
   are gone from the world, so completion must be *remembered*).
   Extend the save format honestly — version line first, chapter 12.
2. **The cave.** A second, smaller map (a dungeon of ROCK walls,
   loaded from a CSV — chapter 19's cartographer) entered by walking
   onto a cave-mouth tile, exited likewise. Two TileMaps, one
   `where` variable choosing which is live — the state ladder's idea
   applied to *space*.
3. **Trading.** Flowers are currency: the elder pays two shards'
   worth of *hints* (the nearest shard's compass direction) per
   bouquet. Inventory is a dict (chapter 10), the hint is arithmetic
   on cell coordinates, and suddenly your island has an economy.
4. **The heirloom.** Save files that survive *versions*: add a
   `v=2` header, and make `load_game` read v1 files too (default the
   new fields). Every shipped game faces this within a month; face
   it on an island first.
