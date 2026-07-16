# Chapter 26 — Polish: the last ten percent

Set two versions of Breakout side by side. In one, the ball hits a
brick and the brick vanishes. In the other, the *same* collision —
but the screen jolts, the world holds its breath for a twentieth of a
second, sparks fly, and the thump lands an octave lower when the
brick was red. Identical rules, identical difficulty; one of them
gets played twice. Game developers call the difference **juice**, and
this interlude is a laboratory for it — plus the quieter polish that
juice can't replace: pause buttons, difficulty curves, name entry,
and the discipline of watching a human play your game.

No new machinery again — the point of an interlude — but two
complete programs: a **juice lab** where every effect is on a key, so
you can *feel* each one in isolation before grafting it into your
four games; and a component every arcade game since 1979 has needed.

## The juice lab

`edit("juice.py")` — a ball in a box, plus four famous effects on
demand. SPACE detonates an "impact" (all four at once), and the other
keys isolate them:

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

screen(hdmi.RGB640)
time.sleep(3)

W = hdmi.width()
H = hdmi.height()

hdmi.close("F")
hdmi.create()
hdmi.write("N")
hdmi.fb().fill(0)                  # black margins for the shake to show
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
                clock.reset()      # don't "catch up" the missed frames

        if paused:
            pass                   # the world simply isn't updated
        elif freeze > 0:
            freeze -= 1            # hit-stop: drawn, but not moved
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
                                      random.randint(-160, 160) * 1.0,
                                      random.randint(-160, 160) * 1.0,
                                      random.randint(15, 40) / 60])
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
            hdmi.text("PAUSED", W // 2 - 96, H // 2 - 24, INK, -1, 2, 3)
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
```

Run it. Tap SPACE once and *watch what your hands feel*. Then the
tour, effect by effect:

**Screen shake** is the flip done crooked: instead of `copy("F",
"N")`, the finished frame is *blitted* a few random pixels off-centre
— `hdmi.blit(0, 0, W, H, ox, oy, "F", "N")` — with the magnitude
riding a decaying `dt` timer, chapter 24's pattern. (The one-off
black fill of `N` at the top is so the jolted frame's margins show
black, not stale pixels — and note the write-target dance around it,
chapter 17's binding rule in the wild.) Two rules of taste: shake the
*camera*, never the physics — the update above knows nothing of
`ox` — and decay fast; a quarter second is plenty.

**Hit-stop** (`freeze`) may be the highest juice-per-line in games:
for four frames the world is drawn but not updated, and the impact
gains *weight* — fighting games have tuned entire reputations on
this number. Notice it's just a counter consulted by the ladder;
pause is the same idea with a human on the button.

**The flash** is one white fill, two frames. Use it like chilli:
damage taken, boss down — and *no more*. (Genuine accessibility
note: rapid full-screen flashing can harm photosensitive players.
Two frames, rarely, is the ceiling — and a settings flag to disable
it entirely is the professional touch.)

**Pause** costs eight lines and its absence is the fastest way to
look amateur. The world freezes because — chapter 25's insight — its
update simply isn't reached; the one subtlety is `clock.reset()` on
resume, or the clock "owes" the pause and motion lurches. Add P to
all four of your games tonight; it's the same eight lines in each.

And the wipe — scene transitions — is a bonus experiment below,
because it teaches the binding rule one more time.

## The component: three-initial entry

Every game with a high-score table meets the same problem: names,
without `input()` breaking the arcade spell. Build it once, as a
library (chapter 20's dual-career pattern — demo under `__main__`).
`edit("initials.py")`:

```python
# initials.py -- arcade name entry.  import initials; name = initials.get()
import pcgame
import keyboard

def _held(code):
    for i in range(1, keydown(0) + 1):
        if keydown(i) == code:
            return True
    return False

def get(title="ENTER YOUR INITIALS"):
    """Three letters, arcade style: LEFT/RIGHT spin, SPACE locks.
    Draws over whatever is on screen; returns e.g. 'ADA'."""
    d = hdmi.fb()
    W = hdmi.width()
    letters = [0, 0, 0]
    slot = 0
    l_was = r_was = f_was = True          # keys may still be held (ch 25!)
    clock = pcgame.Clock(vsync=True)
    while True:
        clock.tick()
        l_now, r_now = _held(keyboard.LEFT), _held(keyboard.RIGHT)
        f_now = _held(ord(" "))
        if r_now and not r_was:
            letters[slot] = (letters[slot] + 1) % 26
        if l_now and not l_was:
            letters[slot] = (letters[slot] - 1) % 26
        if f_now and not f_was:
            slot += 1
            beep(660 + slot * 110, 30)
            if slot == 3:
                return "".join(chr(65 + n) for n in letters)
        l_was, r_was, f_was = l_now, r_now, f_now

        x0 = W // 2 - 120
        d.fill_rect(x0 - 20, 150, 280, 160, d.colour(0x101828))
        hdmi.text(title, W // 2 - len(title) * 4, 165, d.colour(GRAY))
        for i in range(3):
            colr = d.colour(GOLD) if i == slot else d.colour(WHITE)
            hdmi.text(chr(65 + letters[i]), x0 + i * 90, 200, colr, -1, 2, 5)
        d.fill_rect(x0 + slot * 90, 275, 48, 4, d.colour(GOLD))

if __name__ == "__main__":
    print("demo: spin with LEFT/RIGHT, lock with SPACE")
    name = get()
    print("welcome,", name)
```

Study two choices. The `_was` flags start `True` — chapter 25's
carried-key lesson, because the game-over keys that *led here* may
still be under a thumb. And `get()` deliberately draws its panel
**over whatever the screen holds** — no mode change, no clear — so
the defeated game stays visible behind the ceremony, which is
exactly how the arcades did it. Grafting it into a game is now three
lines on the over screen:

`import initials`, `name = initials.get()`,
`scorelib.add(score, name)` — and chapter 12's hall of fame turns
every one of your games into a family tournament.

## Difficulty: the curve is the game

The ramps you've shipped — Breakout's `0.15` per level, Pong's
`1.04` per return, Asteroids' `3 + wave` rocks — share a shape worth
making explicit:

- **Start below the player.** The first minute should be nearly
  unlosable; its job is teaching the controls and buying goodwill
  (the playtest below measures this minute).
- **Ramp geometrically, cap honestly.** Percentages compound;
  `min()` caps stop compounding past physics (tunnelling) or fun.
- **Telegraph it.** Asteroids' quickening heartbeat, rising brick
  pitch, a "WAVE 5" card — players accept difficulty they can *hear
  coming*, and rage at difficulty that ambushes.
- **One knob at a time.** Speed *or* count *or* size per level — two
  at once and you can't tune either.

## Playtesting: the mum test

Everything above is tuning; this is how you get the data. Hand your
game to someone — a parent, a friend, ideally someone who won't
flatter you — and follow three rules that are harder than they read:

1. **Say nothing.** Not the controls, not the goal ("the title
   screen's job, not yours"). Every word you say is a patch the game
   needed.
2. **Watch them, not the screen.** Note the exact moment of the
   first smile, the first frown, the first glance away. Write the
   three moments down.
3. **Change one number, then retest.** Chapter 13's debugging method
   with a human in the loop — two changes at once and the second
   test tells you nothing.

The metrics that matter for a small game: does minute one produce a
smile; do they die before understanding *why*; and — the only score
that counts — do they press SPACE again without being asked.

## The shippable checklist

Before a game of yours rides an SD card to someone's house:

- Title screen: name, controls, one enticing motion behind the text.
- Pause on P. Quit on ESC — *with the collar run* (console, buffers,
  mode all restored: your reputation is `finally:`).
- A sound for every event the player causes — and none they didn't.
- High scores that survive power-off, names attached.
- The first minute tested on a human who isn't you.
- Starts clean from a cold boot: `run("/sd/game.py")` on a machine
  that has never seen it. (Assets beside the program — chapter 11's
  `sys.path` wisdom — and every file it writes has a home.)

Do all that to one of the four games this week — properly, every box
— and you will have *shipped software*, which is a different hobby
from writing it, and the one this Part was secretly teaching.

## Experiments

1. In the lab, set `freeze = 12` and feel how impact becomes
   slow-motion theatre; then `freeze = 1` (imperceptible, yet
   *something* is lost — juice works below consciousness). Find your
   number.
2. Shake taste: magnitude `36` vs `12` vs `80`, decay `0.25` vs
   `0.6`. Somewhere is *your* earthquake; somewhere else is a broken
   monitor. Label both in comments.
3. The iris wipe: write `wipe()` — a loop of growing black discs
   (`arc`, radii 0 to 420) drawn to **N** directly with a `vsync`
   between each — and call it on a key. Mind the binding rule: a
   fresh `hdmi.fb()` *after* `hdmi.write("N")`, and switch back to
   `"F"` when done. The scene "reappears" on your next flip, free.
4. Graft one effect into a real game: shake on every Breakout brick
   (too much — feel *why*), then only on life-lost (right). Juice
   spent everywhere is juice spent nowhere.
5. Run the mum test on the adventure with someone who has never seen
   it. Where do they get lost — and is the fix a *game* change or a
   *dialogue* change? (The elder can say anything you need her to.)

## Challenges

1. **The juice pass.** All four effects plus `initials` into
   Breakout, tuned by playtest. This is the "full arcade" challenge
   of chapter 23 finished for real — burn it to a card, gift it.
2. **The settings screen.** A `settings.py` state on your best
   game's title (chapter 22's ladder grows a rung): sound on/off,
   flash on/off (the accessibility flag), difficulty
   easy/normal/fierce — persisted in a file (chapter 12), read at
   boot. You will meet this exact screen again in Part V, wearing
   `pcgui`.
3. **Attract mode, universal.** A reusable `demo_input()` that
   replaces `held()` after ten idle seconds on any title screen —
   returning fake keys that chase the ball / drift and fire — and
   yields back to the human on any real keypress. One module;
   install everywhere. (Chapter 21's philosophies: it's a fake
   *poll*.)
4. **Ship it, truly.** The whole checklist on the game of your
   choice, then the final boss: give the SD card away and *don't
   visit for a week*. The bug reports that come back are chapter 13
   in its natural habitat — and the fact that they come back at all
   means someone kept playing. Congratulations: Part IV is yours.
