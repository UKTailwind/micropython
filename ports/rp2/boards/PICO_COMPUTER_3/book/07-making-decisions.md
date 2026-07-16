# Chapter 7 — Making decisions: `if`

Every program you have written runs top to bottom, the same way every
time, like a wind-up toy. This chapter installs the fork in the road: the
ability to look at a value and *act differently because of it*. It is the
difference between a music box and a game — and by the end of the chapter
you will have written your first real game, with the machine keeping
secrets from you.

## Questions with yes/no answers

Ask the machine to compare things:

```python
>>> 5 > 3
True
>>> 10 < 2
False
>>> price = 4.2
>>> price <= 5
True
```

`True` and `False` are values — a fourth kind, alongside ints, floats and
strings (`type(True)` reports `<class 'bool'>`, short for *Boolean*).
Every decision a computer has ever made comes down to manufacturing one
of these two words.

The full set of comparisons:

| Ask | Meaning |
|---|---|
| `a > b`, `a < b` | greater / less than |
| `a >= b`, `a <= b` | greater / less than *or equal* |
| `a == b` | **equal** — note the double `=` |
| `a != b` | not equal |

That double `==` deserves a paragraph, because it trips everyone. Single
`=` is chapter 6's *becomes* — it changes things. Double `==` merely
*asks* — it changes nothing. `price = 5` sets the price; `price == 5`
asks whether it is 5, and answers True or False:

```python
>>> name = "Peter"
>>> name == "peter"
False
```

(Strings compare exactly — capital letters and all. Text has its own
comparisons too: `"bat" < "cat"` is alphabetical order, which is how
this machine will sort your high-score table in chapter 10.)

> **Coming from MMBasic:** BASIC's `=` did both jobs and guessed from
> context. Python splits them: `=` assigns, `==` compares. Type `=` where
> Python expects `==` and you get a SyntaxError pointing at the spot —
> annoying for a week, then it saves you from bugs BASIC never caught.

## `if`: the fork itself

A new file, `edit("bouncer.py")`:

```python
age = int(input("How old are you? "))

if age >= 18:
    print("Welcome to the arcade.")
    print("The racing game is free today.")

print("Enjoy your visit.")
```

Run it twice — once as a 21-year-old, once as a 12-year-old. The shape:
`if`, a condition, a colon — then the conditional lines **indented four
spaces**. Both indented lines happen only when the condition is True; the
unindented final line happens for everyone.

Stop and look at that indentation, because this is the moment two earlier
promises come due. **In Python, the indentation *is* the structure.** The
machine knows exactly which lines belong to the `if` because — and only
because — they are pushed right. Where other languages bracket a block
with markers and indent purely for the human reader, Python makes the
human view and the machine view the same thing. You can no longer write
code that looks like it does one thing and does another.

The practicalities: use **four spaces** per level; be consistent; and let
the editor carry the burden — `pye` auto-indents after a `:` line, and
`Tab`/`Shift-Tab` (chapter 5) shift lines or whole selections in and out.
A wrong indent announces itself honestly: `IndentationError: unexpected
indent`, with a line number.

> **Coming from MMBasic:** `IF age >= 18 THEN ... ENDIF` loses both the
> `THEN` and the `ENDIF`. The colon starts the block; *dedenting* ends
> it. Where MMBasic needed `ENDIF` to find the boundary, Python (and you)
> reads it from the shape.

## `else` and `elif`: covering every road

`else` catches everything the `if` didn't:

```python
if age >= 18:
    print("Welcome to the arcade.")
else:
    print("Kids play free -- but home by six!")
```

And `elif` ("else-if") chains checks into a ladder, tested top to bottom,
first match wins, at most one branch runs:

```python
if age >= 65:
    print("Golden joystick discount!")
elif age >= 18:
    print("Full price, full glory.")
elif age >= 13:
    print("Teen tournament on Saturdays.")
else:
    print("Kids play free -- but home by six!")
```

Order matters: test the most specific condition first. (Swap the first
two rungs and every pensioner pays full price — run it in your head and
see.)

## `and`, `or`, `not`

Conditions combine, in plain English:

```python
if age >= 13 and age <= 17:
    print("Teen tournament eligible.")

if colour == "red" or colour == "crimson":
    print("The red ship it is.")

if not raining:
    print("Rooftop arcade is open.")
```

Two subtleties. `and`/`or` connect *complete questions*: it must be
`colour == "red" or colour == "crimson"` — not `colour == "red" or
"crimson"`, which Python reads very differently (a story for a later
chapter; for now, spell both sides out). And for a between-check, Python
allows the maths-textbook shorthand: `13 <= age <= 17` works and reads
beautifully.

## Secrets: the `random` toolbox

A game needs an opponent, and an opponent needs unpredictability:

```python
>>> import random
>>> random.randint(1, 5)
3
>>> random.randint(1, 5)
5
```

`random.randint(1, 5)` returns a whole number from 1 to 5, both ends
included, different (well, unpredictable) every call. Your machine can
now know something you don't.

## Project: the guessing game, round one

`edit("guess.py")`:

```python
import random

secret = random.randint(1, 5)

print("I have chosen a number from 1 to 5.")
guess = int(input("One guess. Steel yourself: "))

if guess == secret:
    beep()
    print("INCREDIBLE. That was it exactly.")
elif abs(guess - secret) == 1:
    print(f"Agonising -- you were one away. It was {secret}.")
elif guess < secret:
    print(f"Too low. It was {secret}.")
else:
    print(f"Too high. It was {secret}.")
```

Everything from this chapter is in play: a secret made with `random`, a
comparison ladder, and one newcomer — `abs()`, which strips the minus
sign off a number, so `abs(guess - secret)` is the *distance* between the
two, whichever is bigger. First match wins, so the "exactly right" rung
must sit above the "one away" rung — swap them and see why.

Play a few rounds. Notice what the game is begging for: *another guess*.
"Too low" is a taunt when the round is over; it wants to be a *clue* in
an ongoing duel. That requires teaching the machine to go back — the
loop — and it is exactly where chapter 8 begins. `guess.py` is not
finished; it is *waiting*.

## Experiments

1. At the prompt, predict then check: `3 == 3.0`, `"3" == 3`,
   `"cat" < "dog"`, `not True`. One of the four is the reason chapter 6
   nagged about `int(input(...))` — which?
2. In `bouncer.py`, un-indent the second `print` so only one line belongs
   to the `if`. Predict both runs before you make them. Then indent the
   *final* line as well and explain what the program has become.
3. Rebuild the arcade ladder with its rungs deliberately in the wrong
   order (the `age >= 18` test first). Find an age that gets the wrong
   answer, and state the rule that prevents it.
4. Upgrade `guess.py`'s taunts: add a rung for a guess that isn't even in
   range (below 1 or above 5 — `or` earns its keep), *above* the other
   rungs. Why above?
5. Roll `random.randint(1, 6)` at the prompt a dozen times (up-arrow!).
   Convince yourself 1 and 6 both actually occur.

## Challenges

1. **Rock, paper, scissors.** The machine picks `random.randint(1, 3)`
   (1 = rock, 2 = paper, 3 = scissors); you type your choice as a number
   too. Print both choices and the verdict. It is one honest ladder —
   draw first, then the three ways to win, `else` you lost. A `beep()`
   for victory is traditional.
2. **The fork in the dungeon.** A two-room adventure: describe a cave,
   `input("left or right? ")`, and give each direction a scene — one of
   which contains a *second* choice (an `if` indented inside an `if`).
   Congratulations: you have invented the adventure game, and you will
   want chapter 8 badly.
3. **The honest quiz.** Three questions of your own devising, a `score`
   variable that starts at 0 and earns `score += 1` per correct answer
   (chapter 6's move), and a final verdict ladder: 3 right, 2, fewer.
   Mind the types: `input()` of a number needs its `int()`.
