# Appendix F — Challenge hints

Nudges, not answers — each is the sentence that unsticks. Chapters
whose challenges need no rescue are absent without shame.

**1.1 (BBC Micro speed)** Rule of thumb: the ARM core runs about *one*
instruction per clock, while the 6502 took roughly *four* cycles per
instruction. So 252 MHz is ~252 million instructions a second, and 2 MHz
is ~½ million — a ratio near 500×. A BBC Micro's full second of work
therefore lands in about 2 ms here. All very approximate — the estimating
*is* the exercise.

**3.1 (fold to the Moon)** Each fold doubles: after n folds the stack
is `0.1 * 2 ** n` mm. Try n at the prompt until it passes 384,400 km
in mm (three hundred and eighty-four *billion*). The answer is under
fifty.

**3.2 (day of week)** 1,000 % 7 is 6: count six days on from Tuesday.

**8.3 (the machine guesses)** Keep `low` and `high`; guess
`(low + high) // 2`; the human's `h`/`l` answer replaces `low` or
`high` with `guess ± 1`. Seven guesses always suffice for 1–100
because 2⁷ = 128.

**9.1 (filled star)** `t.fillcolor(GOLD)` then `begin_fill()`,
the five-line star, `end_fill()`. The midnight background is a filled
rectangle the size of the screen, drawn first.

**10.1 (menu)** `programs[choice - 1]` — humans count from 1, lists
from 0; the `- 1` is the treaty between them.

**11.4 (guess library)** Move everything except the top-level calls
into `def play(low, high):` ending `return tries`. The tournament
does `for round in range(3): tries = guess.play(1, 100)`.

**12.5 (ledger)** `pos = f.tell()` *before* `f.read(REC)`, then
`f.seek(pos)` before writing back. Read–modify–write is three lines
around one seek.

**13.1 (unbreakable calculator)** One `while True` with *three*
guarded zones: `float()` of each number (ValueError), the operator
lookup (`in` a dict), the divide (ZeroDivisionError). Each failure
`continue`s.

**14.4 (the vault)** Append to `self.history` as the *last* line of
`withdraw`, after every guard has passed — order inside a method is
the whole answer.

**16.3 (Ken Burns)** Load once; each frame
`img.blit(0, 0, sx, sy, 640, 480)` with `sx += 1`. The photo must be
bigger than the screen or there's nowhere to pan.

**17.1 (corner watch)** You already set `hit` per axis — make two
flags, `hx` and `hy`, and a corner is `hx and hy` in the same frame.
Expect to wait; that's the folklore.

**18.3 (sheepdog)** Distance² < 40² → the sheep moves by the *sign*
of (sheep − dog) on each axis. Penning is `tm`-free: three walls and
a test rectangle.

**19.3 (Sokoban stone)** When the hero's *next* cell holds the rock:
compute the rock's next cell too; if it's GRASS, `tm.set` both moves,
else block the hero. One extra lookahead.

**22.3 (Pong for one AI)** `p2y += min(PADDLE_MAX * dt, abs(by -
centre)) * sign` — the cap *is* the difficulty knob.

**23.1 (power-ups)** Falling capsules are chapter 24's crowd pattern:
a list of `[x, y, kind]`, moved by `dt`, culled below the paddle,
caught by rectangle overlap. Ten-second effects are `ticks_diff`
timers (21).

**24.1 (saucer)** `math.atan2(py - y, px - x)` gives the angle from
saucer to ship (radians — `pt()`'s inverse: feed it back through
sin/cos to make the bullet's velocity).

**25.2 (the cave)** Two `TileMap`s and a variable `where`; every use
of `tm` becomes `maps[where]`. The door is a `tile_at` check that
flips `where` and teleports `px, py`.

**26.3 (attract mode)** A function returning fake "held" keys:
`demo_held(code)` consults the ball's position instead of
`keydown()`. Swap which function the game calls; ten idle seconds
(`ticks_diff`) flips the swap.

**27.2 (file manager)** `os.listdir(path)` into the listbox; a
selection ending in nothing is a directory candidate —
`os.stat(p)[0] & 0x4000` says for sure (chapter 4's `ls` uses
exactly this).

**28.1 (cron junior)** Chapter 25's `fired_at` guard, per line: a
dict `{line_no: (h, m)}` of already-run jobs, cleared when the
minute moves on.

**29.3 (remote lamp)** Subscribe, then in `on_msg`: `Pin("LED",
Pin.OUT).value(1 if msg == b"on" else 0)`. The phone app publishes
to the same topic. That's the whole thing — genuinely.

**30.1 (calculus)** `dx = x[1] - x[0]`; slope = `(y[1:] - y[:-1]) /
dx` — one element shorter, so plot it against `x[:-1]`.

**31.4 (PID)** `pid = pcmath.PID(kp, ki, kd, setpoint, 0, 65535)` (the
`0, 65535` clamp the PWM output), then each loop:
`pwm.duty_u16(int(pid.update(light.lux, dt)))`, where `light` is the
TSL2591 and `setpoint` a target lux. Start with ki = kd = 0 and raise
kp until it oscillates, then halve it.

**32.3 (event bus)** `handlers = {}`; `on` appends to
`handlers.setdefault(name, [])`… or chapter-10 honestly: `if name
not in handlers: handlers[name] = []`. `emit` loops the list. The
asyncio task drains a plain list used as a queue.

**33.2 (viper plasma)** The frame is `bytearray(320 * 240 * 2)`
blitted as `(buf, 320, 240)`. Viper wants
`ptr16(buf)` and integer maths — precompute a 256-entry sine table
(a `bytes` object) and index it; no floats inside the loop.

**34.3 (gift build)** The menu is chapter 10's challenge 1 verbatim;
the kindness is testing it with the *keyboard unplugged and
replugged* — their first day will include that.
