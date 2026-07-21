# Chapter 23 — Breakout

One year after Pong, the same company wondered what would happen if the
wall you rallied against could *break*. The answer financed Apple
Computer (really — look it up), and it is your game this chapter.
Breakout is Pong turned ninety degrees plus three new disciplines:
**a destructible world stored as data**, **rectangle collisions that
know which side was struck**, and **the lives-levels-score economy**
that turns a toy into an arcade game. Everything else — clock, states,
deflection, the collar — you built last chapter and will now reuse
without ceremony. That, too, is the lesson.

## The whole game

One listing this time — Breakout is Pong's cousin, and you have earned
the right to read a complete game in one sitting. Type it (or
`autosave` it), play it, then meet the dissection below.
`edit("breakout.py")`:

```python
import pcgame
import keyboard
import random
import time

def held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

screen(hdmi.RGB640)                      # this game is laid out for 640x480
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
ROWS = [(RED, 50), (ORANGE, 40), (YELLOW, 30), (GREEN, 20), (CYAN, 10)]
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
    return (W / 2, 200.0,                # from just beneath the wall...
            random.randint(-100, 100) * 1.0, speed)   # ...a full second out

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
            if bdy > 0 and PY - BS <= by <= PY and px - BS < bx < px + PW:
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
                    if from_x < from_y:            # struck a side face
                        bdx = -bdx
                    else:                          # struck top or bottom
                        bdy = -bdy
                    bricks.remove(b)
                    score += points
                    beep(400 + points * 10, 15)
                    break

            if not bricks:                         # level cleared!
                level += 1
                bricks = new_wall()
                bx, by, bdx, bdy = serve(level)
                beep(1320, 300)

            if by > H:                             # lost below the paddle
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
            hdmi.text("B R E A K O U T", W // 2 - 240, 200, INK, -1, 2, 3)
            hdmi.text("LEFT/RIGHT to steer -- SPACE to start",
                      W // 2 - 148, 280, DIM)
        elif state == "over":
            hdmi.text("GAME OVER", W // 2 - 144, 200, INK, -1, 2, 3)
            hdmi.text("SPACE to try again", W // 2 - 72, 280, DIM)
        hdmi.copy("F", "N")
finally:
    hdmi.write("N")
    console()
```

Play a full game before the autopsy — at least one lost life, at least
one cleared level. The feel of the row-by-row pitch rising as you dig
toward the red bricks is data you'll want for what follows.

## The wall is a list

`new_wall()` builds forty bricks as a **list of `[x, y, colour,
points]` entries** — chapter 10, load-bearing as ever. Three
consequences flow from that one decision:

- Drawing the wall is `for ... in bricks` — no grid bookkeeping, no
  "is this one alive?" flags. **A destroyed brick isn't marked dead;
  it's *removed*** (`bricks.remove(b)`), so the draw loop, the
  collision loop and the level-cleared test (`if not bricks:` — the
  empty-list-is-False idiom from chapter 10) all stay pristine.
- The wall's *shape* is the loop that builds it. Rewrite `new_wall`
  and the same game plays pyramids, checkerboards, your initials —
  experiment 1 goes there.
- Colour and points travel together in each entry — the tuple lesson,
  even inside a list.

The row constants earn a look too: rows are worth 50 down to 10, so
digging deep pays — the score system *creating* the game's ambition
without a single extra rule.

## Which face did the ball hit?

The chapter's new physics. Pong's paddles only ever reflected `dx`;
a brick can be struck from any side, and reflecting the wrong axis
looks *wrong* instantly (the ball appears to burrow through). The test
in the listing is the standard trick, worth knowing forever:

When the ball overlaps a brick, measure the overlap on each axis —

```
from_x = min(ball_right - brick_left, brick_right - ball_left)
from_y = min(ball_bottom - brick_top, brick_bottom - ball_top)
```

— and **the shallower overlap names the face it came through**: a
ball that just crossed a side edge has barely penetrated in x but
overlaps generously in y, so `from_x < from_y` means a side strike
(flip `dx`); otherwise it came through the top or bottom (flip `dy`).
Two `min`s and a comparison: that's the whole algorithm, and it reads
as geometry once you draw one picture of it. Corner strikes are
genuinely ambiguous — the comparison just picks the shallower axis,
and nobody watching sixty frames a second will ever dispute it.

And the `break` — **at most one brick per frame** — is not laziness
but correctness twice over: the ball can overlap two bricks at once
(edge between neighbours), and destroying both with one flip each
would cancel the bounce entirely; worse, we're *removing from the
list we're looping over*, which chapter 10 would remind you to never
continue past. Strike one, bounce, leave; the second brick gets its
turn next frame, eight milliseconds later.

## The economy: lives, levels, ramp

Notice what lives and levels are *not*: new states, new loops, new
screens. They are two integers, adjusted inside `play`, that decide
which of the existing paths runs — down a life and `serve`, or
`state = "over"`; wall empty and it's `new_wall()` plus a faster
`serve(level)`. The `min(..., 420)` cap on serve speed is an honest
engineering note in one number: past a certain speed, a ball can step
*through* the paddle band between two frames ("tunnelling" — the
step-per-frame exceeding the paddle's thickness). Capping speed is
the beginner-honest fix; chapter 34 mentions the grown-up ones.

The difficulty ramp is `0.15` per level, and the paddle deflection is
Pong's formula rotated (relative strike position steering `bdx` now).
The serve numbers carry a story worth telling: this book's own
hardware playtest found the first draft *unfair* — the ball launched
from mid-court at 260, giving a paddle stranded at one wall half a
second to cross. The shipped serve starts just beneath the wall and
opens at 200, a full second's travel — because a player who dies
feeling cheated stops playing, while one who dies feeling *slow*
presses SPACE again. Every one of those numbers is negotiable, tuned
on humans; that's why they're at the top of sections, not buried.

## Someone else's Breakout

One more discipline, best learned early: **reading another
implementation after writing your own.** The firmware source
repository carries the port's own Breakout
(`boards/PICO_COMPUTER_3/tests/breakout.py`, from the MMBasic
original) — and it makes a fascinatingly different choice: the brick
wall is a chapter 19 **tile map**, collision comes from tile
attributes, and the paddle and ball are blitted tiles. Same game, same
physics ideas, entirely different data structure — and neither of you
is wrong. When you can read it and *argue with its choices*, you have
stopped being a beginner.

## Experiments

1. New architecture, free: make `new_wall` build a pyramid (rows of
   decreasing width, centred) — then a checkerboard (`if (row + col)
   % 2:`, chapter 8's remainder trick drawing again).
2. Silver bricks: give some entries a fifth element, `hits = 2`; a
   struck silver brick decrements and repaints `GRAY` instead of
   dying. (Careful: only `remove` at zero — and notice how naturally
   the list entry absorbed a new field.)
3. Shrink the paddle by 8 pixels each level (`PW` becomes a variable
   reset at title). Classic cruelty; watch a human notice.
4. The pitch ladder: brick beeps already rise with row value — make
   wall and paddle sounds drop an octave each lost life instead.
   Sound as information; players feel it before they notice it.
5. Serve mercy: after a lost life, hold the ball glued to the paddle
   until SPACE launches it (a `serving` flag — or is it a fourth
   *state*? Try it both ways and decide which reads better. There is
   a defensible answer each way, which is the point).

## Challenges

1. **Power-ups.** When a brick dies, one time in eight
   (`random.randint(1, 8) == 1`) drop a falling capsule; catching it
   with the paddle grants wide-paddle / slow-ball / extra-life for
   ten seconds (`ticks_diff` — chapter 21). Falling things are a
   list; you know everything required.
2. **The level designer.** Walls from text files — `#` is a brick,
   `.` a gap, one row per line (chapter 12's reading loop,
   chapter 19's cartographer spirit). Ship `level1.txt` through
   `level5.txt` and load them in sequence.
3. **The full arcade.** `scorelib` hall of fame with name entry on
   the over screen, `sfx` sounds, a `.mod` soundtrack with
   `mod_sample` stings (chapter 20), and an attract mode (chapter
   22, experiment 5). This is a *finished product* — put it on a
   card, hand it to someone with no idea what's inside.
4. **Read the rival.** Get the firmware repo's `tests/breakout.py`
   onto your machine and actually run and read it. Write down two
   choices it made better than yours, and two you made better. (Both
   lists will be non-empty. They always are.)
