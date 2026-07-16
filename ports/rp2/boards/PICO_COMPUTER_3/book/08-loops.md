# Chapter 8 — Repeating yourself: loops

Here is an unflattering truth about your machine: it has no imagination
whatsoever, but it will repeat anything you tell it two hundred and
fifty-two million times a second without complaint, without boredom and
without error. Every impressive thing a computer does — animating a
game, searching a library, drawing a photograph pixel by pixel — is a
tiny act repeated preposterously often. The **loop** is how you command
repetition, and it is the last big piece of machinery you need before
programs become genuinely powerful.

Two promises fall due in this chapter: chapter 2 owes you an LED that
blinks itself, and chapter 7's guessing game is waiting for its rematch.

## `while`: repeat as long as this is true

`edit("countdown.py")`:

```python
n = 10
while n > 0:
    print(n)
    n -= 1

print("LIFT OFF!")
beep(220, 400)
```

Read it aloud: *while n is greater than zero, print it and knock one
off.* The shape is pure chapter 7 — a condition, a colon, an indented
block — but where `if` runs its block at most once, `while` runs it,
checks the condition again, runs it again... and only walks past when
the condition finally answers False. The unindented lines then get their
turn: lift off.

The loop works because line 4 *changes something the condition looks
at*. That is the anatomy of every honest `while` loop: set up (line 1),
test (line 2), do work (line 3), **make progress** (line 4). Delete the
`n -= 1` line and you get the famous **infinite loop** — ten printed
forever, or at least until Ctrl-C. Try it; you need to see one on
purpose. When you interrupt it you'll be shown:

```
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
  File "pcshell.py", line 101, in run
  File "countdown.py", line 3, in <module>
KeyboardInterrupt:
```

A `KeyboardInterrupt` is not the machine complaining — it is the
machine confirming *you* stopped it. Read it with chapter 5's rule
(deepest line naming *your* file): the program was on `countdown.py`
line 3 when you pulled the cord — which is diagnostic gold, because
that's where a stuck program is spending its time.

> **Coming from MMBasic:** `DO WHILE ... LOOP` is `while ...:` plus
> indentation. `EXIT DO` is `break`, coming up shortly.

## The forever loop, on purpose

Sometimes "repeat forever" is exactly the specification. The promised
blinker, `edit("blink.py")`:

```python
import time

led = Pin("LED", Pin.OUT)

while True:
    led.toggle()
    time.sleep(0.5)
```

`True` is never False, so the loop never ends: toggle, wait half a
second, forever — a heartbeat for your machine, and your finger (chapter
2, experiment 2) is relieved of duty. Stop it with **Ctrl-C**, exactly
as chapter 5 taught for a runaway `main.py` — and notice that `while
True:` is not a beginner's blunder but a professional idiom: every game,
every server, every device you own runs one. The skill is not avoiding
forever-loops; it is *owning the exit*.

## The rematch: finishing the guessing game

Chapter 7's game could taunt but not duel. What it lacked was exactly a
`while`. `edit("guess.py")` and rebuild it:

```python
import random

secret = random.randint(1, 100)
tries = 0
guess = 0

print("I have chosen a number from 1 to 100.")

while guess != secret:
    guess = int(input("Your guess: "))
    tries += 1
    if guess < secret:
        print("Higher...")
    elif guess > secret:
        print("Lower...")

beep()
print(f"Got it in {tries} tries!")
if tries <= 7:
    print("Seven or fewer. Champion form.")
```

One to a hundred now, because the clues make big ranges beatable. Note
the small ceremony on line 5: `guess = 0` exists so the `while` has
something to compare on its very first look (0 can never be the secret,
so the loop always starts). Setting up a variable *so that* a loop can
begin is called priming, and you will do it often.

Play it properly. Then reflect on what seven lines of loop bought:
state (`tries`), interaction, adaptive feedback, and an ending the
player *earns*. This is a complete game, and — chapter 7's challenge 1
notwithstanding — your first with real strategy in it. (Always guess
the middle of what remains. That instinct has a name, *binary search*,
and it is one of the great algorithms; a challenge below turns the
tables on it.)

## `for` and `range()`: when you know how many

`while` repeats *until something happens*. Just as often you want
*exactly ten of these*, and that is `for`:

```python
>>> for i in range(5):
...     print("I will not overheat the toaster.")
```

`range(5)` deals out 0, 1, 2, 3, 4 — five numbers, starting at 0 — and
`for i in` runs the block once per number, with `i` holding the current
one.

### Typing a block at the prompt — read this before it bites

Something new happened in that transcript: after the `:` line the prompt
changed to `...` and indented for you. You are *inside the block*, and
knowing how to get out again is a hand skill every Python user learns
once. Here it is, keystroke by keystroke:

```
>>> for i in range(5):          you type the for line, press Enter
...     print("I will not...")  the indent appears BY ITSELF; type the body, Enter
...                             now press Backspace (the indent vanishes), then Enter
(the loop runs, five lines print)
>>>                             a fresh prompt -- ready for your NEXT command
```

Two rules, worth committing to memory:

1. **A block ends with Enter on a line with *no* indentation.** The
   prompt keeps helpfully re-indenting blank lines, so pressing Enter on
   a line that *looks* empty may not end anything — it still contains
   invisible spaces. **Backspace swallows a whole indent level in one
   press**, so the reliable sign-off is always: *Backspace until the
   cursor sits right after the `...`, then Enter.* (Hammering Enter does
   get there eventually — it just takes more presses than you expect.)
2. **One entry, one statement.** Each `>>>` accepts a single statement
   *or* a single block. Do not type your next command at the `...`
   prompt after the loop body — it becomes an illegal second statement
   inside the same entry, and the whole thing is refused with a
   `SyntaxError`. Finish the loop, let it run, and type the next command
   at the fresh `>>>`.

None of this applies inside a file — the editor has no `...` prompt, and
programs hold as many statements as you like. That is one more reason
blocks of any real size live in files; the prompt's block support is for
quick experiments. When the count itself matters, give range a start and stop:

```python
>>> for n in range(1, 11):
...     print(n, "squared is", n * n)
```

That prints the 1-to-10 story — and exposes the rule that ambushes
everyone: **the stop value is not included.** `range(1, 11)` is 1
through 10. Think of it as "from 1, *up to* 11". A third number sets
the step: `range(10, 0, -1)` counts 10 down to 1 (our countdown,
re-invented), `range(0, 101, 5)` counts by fives.

`for` happily walks along a string too — `for letter in "PICO":` visits
`P`, `I`, `C`, `O` — a first hint that `for` is really about *walking
along collections*, which is chapter 10's big idea.

> **Coming from MMBasic:** `FOR n = 1 TO 10` is `for n in range(1, 11)`
> — mind that 11 — and there is no `NEXT`; the indentation ends the
> block, as ever. `STEP -1` is range's third argument.

## Loops you can see

The screen makes repetition visible. Ten rows of stars, growing:

```python
>>> for i in range(1, 11):
...     print("*" * i)
*
**
***
(and so on, to ten)
```

Chapter 3's string-repeat, promoted from party trick to graphics
engine. And for something genuinely beautiful, let a loop *make data*
and hand it to the plotter:

```python
values = []
for n in range(21):
    values.append(n * n)

plot(values)
```

Two lines are on advance loan from chapter 10 — `values = []` makes an
empty **list**, and `.append(...)` adds each result to it (you saw this
borrowing style with `open()` in chapter 4). Save it as a program and
`run` it — or, at the prompt, remember the sign-off drill: after the
`append` line it's *Backspace, Enter* to run the loop, and only then, at
the fresh `>>>`, type `plot(values)`. Either way: `plot()` draws the
list on the HDMI screen, autoscaled, with axes — and there is the
graceful upward sweep of the squares, *as a picture*. Try `n * n * n`,
or `n % 7`. Any key scrolls you back to the console; `plot()` has a
whole toolbox (functions, styles, colours) waiting in the User Manual
when you want it.

## `break` and `continue`: steering mid-loop

`break` abandons a loop from inside — the professional exit for a
`while True`:

```python
while True:
    word = input("A word, or 'stop' to finish: ")
    if word == "stop":
        break
    print(f"'{word}' has {len(word)} letters.")

print("Nice words. Goodbye.")
```

This ask-until-done shape is everywhere: menus, games ("play again?"),
and half the projects in Part IV and V. Its rarer sibling `continue`
skips the rest of *this* lap and starts the next one:

```python
for n in range(1, 21):
    if n % 3 == 0:
        continue        # not a fan of multiples of three
    print(n)
```

You will write `break` weekly and `continue` monthly — but recognise
both on sight.

## Project: the times-table dojo

The chapter's machinery, drilling *you* for a change.
`edit("times.py")`:

```python
import random

score = 0
print("TIMES-TABLE DOJO -- 5 questions. Begin!")

for q in range(5):
    a = random.randint(2, 12)
    b = random.randint(2, 12)
    answer = int(input(f"Q{q + 1}: what is {a} x {b}? "))
    if answer == a * b:
        score += 1
        print("Correct!")
    else:
        print(f"No -- {a} x {b} is {a * b}.")

print(f"Final score: {score} out of 5.")
if score == 5:
    beep()
    print("Flawless. The dojo bows to you.")
elif score >= 3:
    print("Solid. Return tomorrow.")
else:
    print("The dojo suggests... practice.")
```

A `for` for the fixed drill, `random` setting the questions, an `if`
marking each answer, a primed `score` keeping the tally, and a verdict
ladder to finish — chapters 6, 7 and 8 in one small, useful, slightly
stern program. Hand the keyboard to a child of your acquaintance and
watch a loop you wrote teach a human being.

## Experiments

1. Make the countdown go 10, 8, 6, ... two ways: once by editing the
   `-=` line, once with `range` and its step. Which reads better?
2. Predict, then print: `list(range(4))`, `list(range(2, 8))`,
   `list(range(20, 0, -5))`. (Wrapping a range in `list(...)` shows its
   numbers all at once.)
3. Reshape the star triangle: right-aligned (print spaces *then* stars —
   `" " * (10 - i) + "*" * i`), then a hollow square. Graph paper helps.
4. Blink SOS: three short flashes, three long, three short, using three
   `for` loops. Then wrap the whole signal in `while True` and you have
   built a distress beacon.
5. In the dojo, count the *misses* too, and report both. Then make the
   questions harder with each correct answer (a growing `randint` upper
   limit — a variable, not a number).

## Challenges

1. **The ancient sum.** Add up every number from 1 to 100 with a loop
   and a running total. Legend says Gauss did it aged seven, without a
   loop — once your loop agrees with `(100 * 101) // 2`, you'll know
   his trick.
2. **Ninety-nine bottles.** Sing it, correctly, including the awkward
   final verses ("1 bottle", not "1 bottles" — an `if` inside the loop).
   Bonus: a descending `beep` per verse.
3. **The tables turn.** *You* think of a number from 1 to 100; the
   machine guesses. It offers its guess; you answer `h` (higher), `l`
   (lower) or `y` (got it). Make it guess the middle of what remains —
   it will never need more than seven tries, and you will have taught a
   machine binary search. (Stuck? Appendix F.)
4. **Dojo deluxe.** Replace the fixed five questions with "play until
   the student types stop" — a `while True`, a `break`, and a final
   report card.
