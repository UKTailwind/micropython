# Chapter 21 — Reading the player: keyboard, mouse and touch

Part III has taught the machine to show and to sing; this last chapter
teaches it to *listen properly*. You have three ways to hear a human,
and choosing between them is a genuine design decision — most input
bugs in beginner games are really a philosophy chosen wrong:

- **Blocking** — `input()`. Stop the world and wait for a whole line.
  Perfect for questions, menus, names for the high-score table.
  (Chapter 6; you know it cold.)
- **Polling** — `keydown()`. Ask, every frame: *what is held right
  now?* No history, no waiting — pure present tense. This is the games
  answer, and chapters 18–19 borrowed it; today the loan is repaid
  with the full story.
- **Events** — `keyboard.on_key()`. Ask to be *told* when something
  happens, and get on with your life meanwhile. The applications
  answer — chapter 27's GUI lives on it (and chapter 34 explains the
  machinery underneath).

The mouse and the touch panel are both polling-style, with a garnish
of latched events. All of it, MMBasic users will find, is home ground.

## `keydown()`, the whole truth

```python
keydown()      # or keydown(0): how many keys are held (0-6)
keydown(1)     # code of the most recent key held (0 = none)
keydown(2)     # ...the one before it, up to keydown(6)
# modifier bitmap: Shift/Ctrl/Alt/GUI, left and right
keydown(7)
keydown(8)     # lock bitmap: Caps=1, Num=2, Scroll=4
```

Printing keys report their **character** code — layout-, Shift- and
Caps-aware — so `keydown(1) == ord("a")` reads naturally. Non-printing
keys have named constants in the `keyboard` module: `UP`, `DOWN`,
`LEFT`, `RIGHT`, `ENTER`, `ESC`, `TAB`, `BKSP`, `HOME`, `END`, `PGUP`,
`PGDN`, `INS`, `DEL`, and `F1`–`F12`.

Two facts complete the picture. Up to **six keys** register at once
(a hardware truth of the USB keyboard protocol — experiment 5 makes it
visible), which is why chapter 18's `held()` helper walks the list;
give that helper a permanent home in `handy.py` today — complete
with its `from keyboard import keydown` line, which chapter 20's
rule makes essential the moment the helper lives in an imported
file. And each
`keydown()` call quietly **drains pending console input** — so the
keys a game polls don't pile up and spill onto the prompt as typed
text when the game ends. (MMBasic does the same; now you know why
your gallery session ended with a clean prompt.)

> **Coming from MMBasic:** `keydown(n)` *is* `KEYDOWN(n)`, argument
> for argument — count, keys, modifiers, locks, and the console-drain
> behaviour.

## `keyboard.on_key()`: the doorbell

Polling asks; events *answer*. Hand `on_key` a function and it is
called for every keypress (auto-repeat included), with the same codes
`keydown` uses:

```python
import keyboard

def spy(code):
    print("key:", code)

# ...type at the prompt and watch the report
keyboard.on_key(spy)
```

(And `keyboard.on_key()` — no argument — removes it. Do that before
the novelty fades.) Notice the key still *works* normally — the event
is a copy, not a theft, so the prompt keeps functioning under your
spy. Your handler runs via the scheduler, between the machine's other
work: keep it short, set a variable, return — chapter 34 explains the
rules in full, and chapter 27's GUI is this mechanism wearing buttons.

## The mouse

Plug one in (the hub has room — chapter 1) and the machine maintains a
virtual cursor: clamped to the screen, starting centred. `mouse(code)`
reads it:

| Code | Meaning |
|---|---|
| `"X"`, `"Y"` | cursor position, pixels |
| `"L"`, `"R"`, `"M"` | left / right / middle button held (1/0) |
| `"W"` | scroll-wheel accumulator |
| `"D"` | double-click just happened (clears on read) |
| `"PRESENT"` | is a mouse connected? |

`mouse_speed(v)` tunes sensitivity (higher = slower). Note the model:
the machine gives you *state* (where, and what's pressed) — polling
again, `keydown`'s philosophy with different fingers.

## The touch panel

A USB touch panel reads the same way — `touch("X")`/`touch("Y")` for
the first contact (**−1 when nothing touches**, a sentinel to check
before you use the coordinates), `touch("DOWN")` for is-it-touched,
`touch("X2")`/`"XN", n` for more fingers. On top of the raw state sit
**gestures**, each *latched*: the panel remembers a swipe, tap, pinch
or rotate until you read it, then clears it —

```python
import time
while True:
    if touch("SWL"):
        print("swiped left")
    if touch("TAP"):
        print("tap at", touch("X"), touch("Y"))
    time.sleep_ms(20)
```

— so a slow game loop still catches a fast flick. (The full gesture
table — swipes, taps, holds, pinches, rotations — is in the User
Manual, section 8. And `time.sleep_ms(20)`, new in passing, is
`sleep` for milliseconds — politer than `time.sleep(0.02)` in tight
loops.)

## Project: paint

A canvas, a floating cursor, both pointing devices, and a cameo from
every chapter since 15. The trick worth studying: the *strokes* are
ordinary drawing on the screen, while the *cursor* rides chapter 18's
sprite engine — in `RGB320` the engine composites on the overlay
layer, so the cursor glides **over** the painting without ever
touching a pixel of it. `edit("paint.py")`:

```python
import time
import keyboard
import pcsprite as sp

screen(hdmi.RGB320)
# let the monitor lock the mode
time.sleep(3)

d = hdmi.fb()
W = hdmi.width()
H = hdmi.height()

PALETTE = [WHITE, RED, ORANGE, YELLOW, GREEN, CYAN, COBALT,
           MAGENTA]
BG = d.colour(0x101010)
d.fill(BG)
hdmi.text("1-8 colour  [ ] size  c clear  s save  Esc quit",
          4, H - 12, d.colour(GRAY))

# crosshair cursor: draw, grab, wipe (chapter 18's move)
d.line(8, 0, 8, 16, d.colour(WHITE), 1)
d.line(0, 8, 16, 8, d.colour(WHITE), 1)
cursor = sp.grab(0, 0, 17, 17, transparent=BG)
d.fill_rect(0, 0, 17, 17, BG)
cursor.show(W // 2, H // 2)

colour = d.colour(WHITE)
size = 3

console("none")

try:
    while True:
        k = keydown(1)
        if k == keyboard.ESC:
            break
        if ord("1") <= k <= ord("8"):
            colour = d.colour(PALETTE[k - ord("1")])
        elif k == ord("["):
            size = max(1, size - 1)
        elif k == ord("]"):
            size = min(20, size + 1)
        elif k == ord("c"):
            d.fill(BG)
        elif k == ord("s"):
            save_image("painting.bmp")

        mx = mouse("X")
        my = mouse("Y")
        # centre the crosshair
        cursor.x = mx - 8
        cursor.y = my - 8
        if mouse("L"):
            d.ellipse(mx, my, size, size, colour, True)

        tx = touch("X")                      # fingers paint too
        if touch("DOWN") and tx >= 0:
            d.ellipse(tx, touch("Y"), size, size, colour, True)

        sp.update(vsync=True)
finally:
    sp.reset()
    console()
    # back to the roomy default
    screen(hdmi.RGB640)
```

Painting with a *held* button is polling at its most natural — state,
sampled sixty times a second, leaves a trail. Note the touch sentinel
check (`tx >= 0`) doing quiet duty, the palette as a chapter-10 list
indexed by key arithmetic, and the `finally` restoring *three* things
now: engine, console, screen mode. Paint something; press `s`; chapter
16 taught you where it goes and chapter 4's `fm` will show it off.

(A confession for later: chapter 27 hands you a ready-made pointer —
`pccursor`, MMBasic's `GUI CURSOR` — that saves and restores the
pixels beneath itself, no sprite engine required. Building one here,
by hand, is how you earn the right to enjoy it.)

## Interlude: a stopwatch for the machine

The second project needs to measure *reaction times*, and `gettime()`
only ticks in seconds. Meet the millisecond clock you'll use in every
game from now on:

```python
import time
t0 = time.ticks_ms()
# ... something happens ...
elapsed = time.ticks_diff(time.ticks_ms(), t0)
```

`ticks_ms()` is a runner's stopwatch, not a wall clock — its absolute
value means nothing (and wraps around!), but the **difference** between
two readings, taken through `ticks_diff` (which handles the wrap), is
elapsed milliseconds, precisely. Chapter 22 builds its whole frame-rate
theory on this pair.

## Project: the reaction duel

Two players, one keyboard — legal, because six keys register at once.
Player 1 owns **A**, player 2 owns **L**; jump the gun and the point
goes to your rival. `edit("duel2.py")`:

```python
import time
import random
import keyboard

def flush():
    while keydown(1):
        time.sleep(0.01)          # wait for all fingers off

print("REACTION DUEL -- player 1: A    player 2: L")
print("Wait for the GO beep. Too early loses the round. Five "
      "rounds.")

scores = {1: 0, 2: 0}

for rnd in range(1, 6):
    print(f"\nRound {rnd}: hands ready...")
    flush()
    wait_ms = random.randint(1500, 4000)
    t0 = time.ticks_ms()
    early = 0
    while time.ticks_diff(time.ticks_ms(), t0) < wait_ms:
        k = keydown(1)
        if k == ord("a"):
            early = 1
            break
        if k == ord("l"):
            early = 2
            break
    if early:
        winner = 2 if early == 1 else 1
        print(f"Player {early} jumped the gun! Point to player "
              f"{winner}.")
        scores[winner] += 1
        continue

    beep(1200, 60)
    print("GO!")
    t0 = time.ticks_ms()
    winner = 0
    while not winner:
        k = keydown(1)
        if k == ord("a"):
            winner = 1
        elif k == ord("l"):
            winner = 2
    ms = time.ticks_diff(time.ticks_ms(), t0)
    print(f"Player {winner} takes it -- {ms} ms!")
    scores[winner] += 1

print(f"\nFinal score  P1: {scores[1]}   P2: {scores[2]}")
champ = 1 if scores[1] > scores[2] else 2
print(f"CHAMPION: player {champ}")
beep()
```

Five rounds means no ties; the false-start rule is one `while` loop
doing double duty (waiting *and* watching); and the reaction times it
prints are real — 180 ms is sharp, 250 ms is human, anything under
120 ms is a cat. Note `flush()` at each round's start: polling code
must often *wait for silence* before it waits for signal, or a held
key from the last round decides this one. That tiny function is a
whole class of bug, pre-squashed.

Part III closes here. The machine now draws, sings and listens —
everything a game needs except the game. Part IV is next, and it
starts by naming the shape all games share.

## Experiments

1. The six-key ceiling: hold down keys one at a time while a loop
   prints `keydown(0)`. Where does it stop counting? (Some keyboards
   manage fewer on certain combinations — "ghosting" — and now you can
   *measure* your hardware's honesty.)
2. Modifier spy: `keydown(7)` is a bitmap. Print it while holding
   Shift, then Ctrl+Shift. Which bit is which? (Chapter 7's `and` — 
   bitwise cousin `&` — checks a single bit: `keydown(7) & 8`.)
3. Event vs poll, felt: with the `spy` handler installed, hold a key.
   The stream of repeats is the *event* view. Now print `keydown(1)`
   in a loop and hold the same key: one steady value — the *state*
   view. Same finger, two philosophies.
4. Give paint a double-click feature: `mouse("D")` flood-fills
   (`d.flood`) at the cursor with the current colour. One latched
   event, one chapter-15 tool, enormous satisfaction.
5. Gesture logger: loop over `("SWL", "SWR", "SWU", "SWD", "TAP",
   "HOLD", "DTAP")` (a tuple — chapter 10) printing any that fire.
   Then wave at your machine like it's 2030.

## Challenges

1. **Whack-a-mole.** A 3×3 grid of squares; one lights up at a random
   moment; click it (cursor inside the square — a bounding-box test,
   chapter 18 taught you the shape) before it dims. `ticks_diff`
   scores speed, `scorelib` keeps legends.
2. **The photo frame, gestured.** Chapter 16's `frame.py` plus this
   chapter: swipe left/right to change photo, tap to pause the
   slideshow, hold to delete (with an on-screen "really?" — `input()`
   has no place here; poll for `y`/`n` keys). Three philosophies, one
   gadget.
3. **Typing tutor.** `on_key` collects what's typed while `ticks_ms`
   times it: show a sentence, measure words-per-minute and accuracy.
   The event handler just appends codes to a list; the main loop does
   the thinking — that division of labour is the whole art of
   event-driven code.
4. **One keyboard, two heroes.** Rebuild chapter 18's gallery for two
   players: WASD moves one crosshair, arrows the other, `held()`
   consulted for both. First to ten ducks. (Six keys at once — you
   now know exactly why this works, and when it won't.)
