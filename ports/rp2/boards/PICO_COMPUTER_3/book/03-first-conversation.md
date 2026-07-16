# Chapter 3 — Your first conversation with Python

Your machine is running and you have proved it is alive. This chapter is a
long, unhurried play session at the `>>>` prompt. By the end of it, the
prompt will feel like what it is: a conversation with the machine, where
you say something, it answers instantly, and *mistakes cost nothing*.

There is no program to write in this chapter. That is deliberate. The
prompt is where programmers — beginners and professionals alike — try
things out, and being at home here will pay off in every chapter that
follows.

> The prompt has an official name: the **REPL**, for
> *read–evaluate–print loop* — it **reads** what you type, **evaluates**
> it, **prints** the answer, and **loops** back for more. You will see the
> name in other Python books and in the User Manual; "the prompt" means
> the same thing.

## The world's most overqualified calculator

Type a sum, press Enter, get the answer:

```python
>>> 2 + 3
5
>>> 100 - 7 * 12
16
```

Notice the machine did the multiplication *before* the subtraction — the
same "multiply and divide first" rule you learned at school. When you want
a different order, use brackets, again just like school:

```python
>>> (100 - 7) * 12
1116
```

The symbols are `+`, `-`, `*` for multiply and `/` for divide, and you can
space them out or not, as you please: `2+3` and `2 + 3` are the same. (One
exception, and it is a big one in Python: don't start the *line itself*
with spaces. The start of a line means something special, which chapter 7
makes glorious use of.)

Two more operators are worth meeting today. `**` is "to the power of":

```python
>>> 2 ** 10
1024
```

> **Coming from MMBasic:** power is `**`, not `^`. Integer division `\`
> becomes `//`, and `MOD` becomes `%` — both are just below.

And here is a party trick no pocket calculator can match:

```python
>>> 2 ** 100
1267650600228229401496703205376
```

That is the exact answer, all thirty-one digits of it. Python integers
have no size limit — ask for `2 ** 1000` if you don't believe it.

## Two kinds of division

Try a division:

```python
>>> 10 / 4
2.5
```

`/` always gives a number with a decimal point (a **float**, in
programming vocabulary — the point can "float" along the number). Its
partner `//` divides and *throws the fraction away*, and `%` gives the
remainder:

```python
>>> 10 // 4
2
>>> 10 % 4
2
```

"Ten divided by four is two, remainder two." You will be surprised how
often remainders matter in programs: is a number even (`n % 2` is 0)?
Which column of the screen does the 137th letter land in? What day of the
week is 1000 days from now? All remainder questions.

While we are with floats, type this:

```python
>>> 0.1 + 0.2
0.30000000000000004
```

Not a fault — a fact of life. Computers store decimals in binary, and
one-tenth in binary is like one-third in decimal: 0.33333… forever,
rounded off somewhere. The error is in the seventeenth digit and harmless
in practice, but remember the lesson: *floats are approximations*. (Whole
numbers, as `2 ** 100` showed, are exact.)

## Words, not just numbers

Put something in quotes and the machine treats it as text — a **string**:

```python
>>> "cat" + "flap"
'catflap'
>>> "ha" * 3
'hahaha'
```

`+` glues strings together and `*` repeats them, which is pleasingly
consistent with what they do to numbers. Single and double quotes both
work (`'cat'` is `"cat"`); pick one and be consistent. And the machine
can measure text:

```python
>>> len("Pico Computer 3")
15
```

But it will not blur the line between text and numbers:

```python
>>> "2" + 2
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
TypeError: unsupported types for __add__: 'str', 'int'
```

`"2"` is a character; `2` is a quantity. Adding them is a **TypeError** —
a type mix-up. The wording is a touch cryptic (`__add__` is Python's
internal name for the `+` operation), but the gist is plain: *plus doesn't
work between a `str` and an `int`* — between text and a whole number. The
machine would rather stop and tell you than guess what you meant, and this
fussiness will save your skin repeatedly later.

## `print()`, and who is talking

You have noticed the prompt echoes the value of whatever you type. So
what is `print()` for? Watch the difference in the quotes:

```python
>>> "hello"
'hello'
>>> print("hello")
hello
```

The echo shows you the *value*, quotes and all — it is the machine being
precise with you, the programmer. `print` produces *output for a person* —
no quotes, just the goods. The distinction starts to matter in chapter 5:
a running **program** never echoes anything; if a program has something to
say, it must `print` it.

`print` happily takes several things at once, separated by commas, and
puts spaces between them:

```python
>>> print("2 to the power", 100, "is", 2 ** 100)
2 to the power 100 is 1267650600228229401496703205376
```

> **Coming from MMBasic:** `print` must be lower-case. Python is
> case-sensitive everywhere — `Print` and `PRINT` are *different names*,
> and neither of them exists. If your fingers have decades of `PRINT` in
> them, this will bite for a week and then stop.

## Breaking it on purpose

Time for the most valuable exercise in this book. You are going to make
every common mistake *deliberately*, so that when you make one
accidentally you recognise an old friend. Type these:

```python
>>> 2 +* 3
Traceback (most recent call last):
  File "<stdin>", line 1
SyntaxError: invalid syntax
```

A **SyntaxError** means the *sentence itself* was malformed — the machine
could not even work out what you were asking. Compare:

```python
>>> ptint("hello")
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
NameError: name 'ptint' isn't defined
```

A **NameError** means the sentence was fine but contained a word the
machine has never heard of — nearly always a typo, occasionally a variable
you haven't created yet (you met that one with `led` in chapter 2). And:

```python
>>> 1 / 0
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
ZeroDivisionError: divide by zero
```

A perfectly-formed question with no answer.

Now the reading lesson, because these reports are built back-to-front:
**read the last line first.** It names the kind of problem
(`NameError`, `TypeError`, …) and then describes it, usually naming the
exact word or value at fault. The lines above it say *where* the problem
happened — worthless now, priceless once your programs are fifty lines
long. Nothing in the report is an accusation. The machine is a colleague
saying "I stopped here, and this is why."

## Working comfortably

Four habits that make the prompt feel like home:

**`cls()` wipes the slate.** After all that experimenting your screen is
full of old sums and tracebacks. `cls()` clears it and puts the prompt
back at the top. Nothing is lost but the clutter — your session (and the
arrow-key history below) carries on exactly as before.

**The arrow keys remember.** Press **↑** and your previous line comes
back, ready to edit and re-run. Keep pressing to go further back. Mistyped
a long line? Don't retype it — arrow up, fix, Enter. This is the single
biggest comfort feature of the prompt; use it constantly.

**Tab completes names.** Type `key` and press the **Tab** key:

```python
>>> key
keyboard        keydown         keymap          keymaps
```

The machine lists every name it knows starting with `key`; type one more
letter and Tab again, and it fills in the rest. Half-remembered a command?
Tab is quicker than any manual.

**`help()` knows things.** On its own it prints a general orientation —
the control keys, where the online documentation lives. Its best trick
needs the next section first; hold that thought for two paragraphs.

## Borrowing more mathematics

The prompt knows arithmetic; the serious mathematics lives in a toolbox
called `math` that you fetch with an `import`:

```python
>>> import math
>>> math.sqrt(2)
1.4142135623730951
>>> math.pi
3.141592653589793
```

Read `math.sqrt` as "the `sqrt` that lives in `math`". One `import` lasts
for the whole session, and after it, Tab completion works there too — type
`math.` and press Tab to see the whole toolbox: `sin`, `cos`, `log`,
`floor`, and friends. And here is `help()`'s promised trick:
`help(math)` prints that same toolbox as a *list*, constants with their
values on display — `help()` pointed at any module does this, and it is
the form worth remembering. (Pointed at a single function it answers
only tersely — modules are where it earns its keep.) This one word, `import`, is your gateway to
everything the machine can do — graphics, sound, the internet — and whole
chapters hang off it later. For now, enjoy sixteen digits of π.

## Experiments

1. In one line, how many seconds are there in a year? (No hints — you know
   the numbers, and brackets are optional.)
2. Print the powers of 2: `2 ** 10`, `2 ** 20`, `2 ** 30`. Roughly a
   thousand, a million, a billion — the pattern behind "kilobytes,
   megabytes, gigabytes". Where does your machine's 8 MB of PSRAM fall?
3. Is `0.1 + 0.2` equal to `0.3`? Ask directly: type `0.1 + 0.2 == 0.3`
   (two equals signs — you have just previewed chapter 7).
4. Use `↑` to bring back your seconds-in-a-year line and adjust it to a
   *leap* year, editing instead of retyping.
5. Run `help(math)` and pick a function you don't recognise. Work out
   what it does experimentally — feed it a few test values and study
   the answers. (This is genuinely how programmers learn unfamiliar
   tools; the machine can't be damaged by curiosity.)

## Challenges

1. A sheet of paper is 0.1 mm thick; folding doubles it. Use powers of 2
   to find how many folds reach the Moon (384,400 km). The answer will
   feel wrong. It isn't.
2. Today is day 0 and it is a Tuesday. Use `%` to work out what day of the
   week it will be in 1,000 days. Check yourself with 7, 14 and 15 days
   first.
3. `len("Pico Computer 3")` counted 15. Predict `len("ha" * 100)` before
   you run it, then settle the bet.
