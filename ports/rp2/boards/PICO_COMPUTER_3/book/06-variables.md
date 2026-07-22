# Chapter 6 — Variables, numbers and text

Welcome to Part II. You can drive the machine; now you learn to *program*
it, and it starts with the single most important idea in the whole craft:
teaching the machine to **remember**.

You have seen the machine forget. In chapter 2 you made `led`, switched
off, and it was gone. In chapter 3 every answer scrolled away the moment
it was printed. A program that can't hold on to anything can't *do*
anything — no score, no name, no running total. The rememberer is called
a **variable**, and this chapter is a play session with them, ending in
two real programs: one that chats with you, and one that does useful
arithmetic for whoever runs it.

## Naming things

A variable is a name with a value attached. You make one with `=`:

```python
>>> price = 4.20
>>> price
4.2
```

Read `=` as "**becomes**": *price becomes 4.2*. From now on, wherever you
could type the number, you can type the name:

```python
>>> price * 3
12.600000000000001
>>> deposit = price * 2
>>> deposit
8.4
```

(That comedy `12.600000000000001` is chapter 3's float lesson back for a
bow — we will learn to print such numbers politely before this chapter
ends.)

Here is the line that separates programmers from calculators. What do you
suppose this does?

```python
>>> price = price + 1
>>> price
5.2
```

As arithmetic, `price = price + 1` is nonsense — nothing equals itself
plus one. But `=` is not equality; it is *becomes*: work out the right
side (`price + 1` is 5.2), then attach the name to the result. *Price
becomes one more than it was.* Every score ever kept, every level ever
advanced, is this one move. It is so common it has a shorthand you will
see everywhere:

```python
>>> price += 1        # exactly: price = price + 1
>>> price
6.2
```

> **Coming from MMBasic:** assignment works as you expect (`=` is `LET`,
> long retired). What's gone: `DIM` — variables spring into existence on
> first assignment — and the `$` suffix; *any* variable can hold text or
> numbers. What's new: `+=`, and the fact that `Price` and `price` are
> **different variables** — Python's case-sensitivity applies to your
> names too. Pick a style (this book uses all lower-case) and stay with it.

The naming rules: letters, digits and `_`, no spaces, can't start with a
digit. The naming *wisdom*: say what the value means. In a week,
`lives_left` will still explain itself; `x` will not. Long names cost
nothing — Tab completion (chapter 3) types them for you.

## Three kinds of value

You have met all three kinds already; now meet them formally. Ask the
machine what kind a thing is with `type()`:

```python
>>> type(42)
<class 'int'>
>>> type(4.2)
<class 'float'>
>>> type("42")
<class 'str'>
```

An **int** (whole number), a **float** (decimal-point number), and a
**str** (string — text). The kind decides what behaviour makes sense,
which is why `"2" + 2` exploded in chapter 3 but `"ha" * 3` was a joke
that worked.

The kinds can be converted, by using their names as commands:

```python
>>> int("42") + 2
44
>>> float("3.14") * 2
6.28
>>> str(42) + " is the answer"
'42 is the answer'
```

`int()` and `float()` read numbers *out of* text; `str()` turns anything
*into* text. Remember these three — one of them is about to matter
enormously.

## `input()`: programs that listen

So far your programs broadcast; they never listen. `input()` completes
the conversation:

```python
>>> name = input("What is your name? ")
What is your name? Peter
>>> name
'Peter'
```

`input` shows your prompt text, waits for a line of typing ended by
Enter, and hands the typing back — where `=` catches it in a variable.

Now the trap that catches every beginner, laid out in daylight so you can
watch it snap. **`input()` always returns a string.** Always. Type `21`
at an input and you get `"21"` — two characters, not a number:

```python
>>> age = input("How old are you? ")
How old are you? 21
>>> age + 1
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
TypeError: unsupported types for __add__: 'str', 'int'
```

Chapter 3's TypeError, in the wild. The cure is the converter you just
met — wrap the input the moment it arrives:

```python
>>> age = int(input("How old are you? "))
How old are you? 21
>>> age + 1
22
```

(Use `float(input(...))` when a decimal answer is legal, like a
temperature.) File the reflex away: *number wanted, `int(` or `float(`
around the `input`.*

> **Coming from MMBasic:** `INPUT "Prompt", A` is `a = input("Prompt")` —
> but MMBasic quietly converted "21" to a number when `A` was numeric.
> Python refuses to guess; you say what you meant, with `int()` or
> `float()`.

## f-strings: sentences with values inside

To speak its answers, a program needs to weave values into text. You
*could* glue strings with `+` (converting every number with `str()` —
tedious). Python has something better. Put an `f` before the opening
quote, and anything in `{curly braces}` inside the string is *live code*,
evaluated and woven in:

```python
>>> name = "Peter"
>>> age = 21
>>> f"Well {name}, next year you'll be {age + 1}."
"Well Peter, next year you'll be 22."
```

The `f` is for *format*, and there is one formatting trick worth learning
immediately — taming floats. Inside the braces, add `:.2f` for "two
decimal places":

```python
>>> third = 10 / 3
>>> f"Each pays {third:.2f} pounds."
'Each pays 3.33 pounds.'
```

(`:.1f` gives one decimal place, `:.0f` none. There is a whole
mini-language back there — appendix E — but `:.2f` covers most of life,
including every price you will ever print.)

> **Also in MicroPython: the `%` operator.** Older code often formats
> with `%` instead — `"%.2f pounds" % third`, or `"%s is %d" % (name,
> age)`. It is terse, it works here, and despite twenty years of
> predictions that it will be removed, it has not budged. f-strings are
> the modern default and what this book uses, but you will meet `%` in
> the wild, so recognise it: `%s` takes any value, `%d` a whole number,
> `%.2f` a float to two places — the same specs as inside the braces,
> just written on the outside.

## Project: a program that chats back

Time to gather everything this chapter has taught — variables, `input()`,
`int()`, f-strings — into one program. `edit("chat.py")`:

```python
import time

print("PICO COMPUTER 3 -- PERSONALITY MODULE v0.1")
time.sleep(1)

name = input("What is your name? ")
print(f"{name}! Superb name. {len(name)} characters of pure "
      f"style.")

age = int(input(f"And how old are you, {name}? "))
days = age * 365
print(f"So you're roughly {days} days old. I'm told that's "
      f"{age * 7} in dog years.")

colour = input("Last one: favourite colour? ")
print(f"Noted. A {colour} spaceship it is.")
time.sleep(1)

beep()
print(f"Delighted to meet you, {name}. Press up-arrow any time "
      f"you miss me.")
```

Run it — and then run it again with silly answers, because the second run
is the real lesson: **the same program, different data, different
conversation.** The program is a shape; the variables are what flows
through it. That idea is the whole trade.

## Project: a units converter

Chat is charming; now something genuinely *useful* — the program you
write because you are sick of doing the sum in your head.
`edit("convert.py")`:

```python
print("MILES -> KILOMETRES")
miles = float(input("Miles: "))
km = miles * 1.609344
print(f"{miles} miles is {km:.1f} km")
```

Four lines: banner, listen-and-convert, calculate, speak. This shape —
**input, process, output** — is the skeleton under a vast number of
programs, from this converter up to programs that "input" a joystick and
"output" a spaceship. You will build on it for the rest of the book.

Make the program yours: convert what *you* keep converting. Temperature
is the classic (`f = c * 9 / 5 + 32`), money needs `:.2f`, recipe
ounces-to-grams multiplies by `28.35`.

## Experiments

1. Make `a = 10` and `b = 99`, then get the values swapped, using only
   assignments. (Classic puzzle: you'll need a third name — or try
   Python's party trick, `a, b = b, a`, and file it away.)
2. In `chat.py`, the machine flatters everyone equally. Personalise it:
   work the age into one more remark, and use `:.0f` to report the
   user's age in (approximate) *weeks*.
3. Answer `chat.py`'s age question with `twenty-one` and read the
   traceback like a professional: what error, which line, and *why*?
4. In `convert.py`, print the kilometres with `:.0f`, `:.1f` and `:.3f`.
   Which is honest for a car journey? (Is `1.609344` itself more precise
   than any journey you have ever measured?)
5. What is `type(input("x? "))` — before you try it — even if you answer
   with `3.14`?

## Challenges

1. **The bill splitter.** Ask for the bill total and the number of
   people; print each person's share, to the penny. Then the upgrade
   restaurants dream of: ask for a tip percentage and include it.
2. **Mad Libs.** Ask for a noun, a verb, an adjective and a friend's
   name — *then* print a story with the answers woven in where they were
   never meant to go. Strings only; no maths; maximum laughter per line
   of code. (Bonus: end with a `beep()` for dramatic effect.)
3. **The time machine.** Ask for a year of birth. Print, on separate
   lines: their age this year (`2026` will do), the year they turn 100,
   and how many days until that party (rough is fine). One `int(input`,
   the rest is arithmetic and f-strings.
