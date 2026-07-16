# Appendix I — Classes beyond the cottage

Chapter 14 stopped deliberately: the cottage version of classes is all
this machine *requires* of you. But Python built the whole
neighbourhood, you will meet the rest in other people's code, and some
of it will genuinely improve your own. This appendix is the guided
tour — one section per idea, one runnable example each, and, most
importantly, *when to reach for it*. Read it any time after chapter 14;
skim it now and return when a section's problem becomes yours.

Everything here runs on the Pico Computer 3 — each example has been
tested on this firmware's MicroPython. The one CPython difference that
matters is flagged in the privacy section.

## Inheritance: blueprints from blueprints

A class can be built *on top of* another, inheriting everything and
changing only what differs:

```python
class Hero:
    def __init__(self, name, health=20):
        self.name = name
        self.health = health

    def describe(self):
        return f"{self.name} ({self.health} HP)"

class Goblin(Hero):                      # a Goblin IS a Hero, plus...
    def __init__(self, name):
        super().__init__(name, health=8)  # run Hero's setup first
        self.loot = 3

    def describe(self):                   # ...and describes differently
        return super().describe() + " [goblin]"

g = Goblin("Grumble")
print(g.describe())          # Grumble (8 HP) [goblin]
```

`class Goblin(Hero):` reads "a Goblin is a kind of Hero". Goblins get
`take_damage`, `is_alive` and friends for free; where the subclass
defines its own method (`describe`), that **overrides** the parent's,
and `super()` reaches the parent's version when the override wants to
*extend* rather than replace — the `super().__init__(...)` call in
`__init__` is near-mandatory practice, so the parent's setup always
runs.

**When to reach for it:** less often than the big books imply. The test
is the sentence — *a Goblin is a Hero* rings true; *a Game is a Screen*
does not (a game **has** a screen: make it an attribute). Prefer
has-a (composition) when in doubt; it stays easier to change. The
honest everyday uses of is-a are the next section, and extending a
library class to tweak one behaviour.

`isinstance(g, Hero)` answers `True` for subclasses too — occasionally
useful, though the duck-typing section below is the more Pythonic
instinct.

## Custom exceptions: inheritance you'll actually use

The most immediately profitable subclass in Python is two lines:

```python
class GameOver(Exception):
    pass

# deep inside the game...
if hero.health == 0:
    raise GameOver("the goblin won")

# at the top level:
try:
    play()
except GameOver as e:
    print(f"GAME OVER -- {e}")
    scorelib.add(score, name)
```

Chapter 13 taught you to catch precisely; defining your *own* exception
types is how your programs grow precise things to catch. `GameOver`
cannot be confused with a `ValueError` from bad input — each gets its
own `except`, and the alarm can be raised five functions deep and
caught once, cleanly, at the top. Name them after the *situation*
(`OutOfFuel`, `SaveFileCorrupt`), inherit from `Exception`, and `pass`
is a complete implementation.

## `__repr__`: the other voice

Chapter 14 gave objects a `__str__` for humans. Its sibling
`__repr__` speaks to *programmers* — it is what the REPL's echo and
error messages use, and the convention is that it looks like the code
to rebuild the object:

```python
class Vec2:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def __repr__(self):
        return f"Vec2({self.x}, {self.y})"
```

```python
>>> Vec2(3, 4)
Vec2(3, 4)
```

Debugging a list of objects goes from `<Vec2 object at 2000a730>` ×10
to something you can actually read. Rule of thumb: `__str__` when your
object faces users, `__repr__` always — and if you define only
`__repr__`, printing falls back to it.

## Operator hooks: teaching `+` and `<` your types

The double-underscore pattern extends to the operators themselves. Two
earn their keep constantly on this machine:

**`__add__` and friends** — arithmetic for your types. Game positions
and velocities beg for it:

```python
class Vec2:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def __add__(self, other):
        return Vec2(self.x + other.x, self.y + other.y)

    def __eq__(self, other):
        return self.x == other.x and self.y == other.y

    def __repr__(self):
        return f"Vec2({self.x}, {self.y})"

pos = Vec2(100, 50)
vel = Vec2(3, -1)
pos = pos + vel          # readable physics
```

The menagerie maps one operator each: `__sub__` (`-`), `__mul__`
(`*`), `__eq__` (`==`), `__lt__` (`<`), and so on. Define only what
makes sense — a `Vec2` you can add is wonderful; a `Hero` you can
multiply is a code review comment.

**`__lt__` unlocks sorting.** Chapter 10's `sort` compares with `<`,
so one hook makes your objects sortable, no `key=` required:

```python
class Hero:
    ...
    def __lt__(self, other):
        return self.health < other.health

party.sort()             # weakest first, ready for triage
```

(Both routes are respectable: `__lt__` bakes in *the* natural order,
`sort(key=...)` picks an order per occasion. When a type has one
obvious ordering, bake it.)

## `__len__` and `__getitem__`: passing for a list

Give a class these two and it can stand wherever a sequence stands:

```python
class Deck:
    def __init__(self, cards):
        self.cards = cards

    def __len__(self):
        return len(self.cards)

    def __getitem__(self, i):
        return self.cards[i]

d = Deck(["ace", "two", "three"])
print(len(d), d[1])       # 3 two
for card in d:            # yes -- __getitem__ alone makes it loopable
    print(card)
```

That last line is a pleasant surprise: `for` will happily drive
`__getitem__` with 0, 1, 2... until an `IndexError` says stop. Your
`Deck`, `ScoreTable` or `TileRow` can *be* loop-able and index-able
while keeping its rules — a guarded list, which is frequently the
class you actually wanted.

## Class attributes: one value, all instances

Attributes made in `__init__` belong to each object. Names assigned
*directly in the class body* belong to the **class** — one copy,
shared:

```python
class Counter:
    made = 0                     # class attribute: one for ALL Counters

    def __init__(self):
        Counter.made += 1

Counter(); Counter(); Counter()
print(Counter.made)              # 3
```

Reach for them for constants shared by every instance
(`Goblin.max_loot = 3`) and tallies like the above. One trap to know:
*reading* `self.made` finds the class attribute, but *assigning*
`self.made = 5` quietly creates a per-object attribute that shadows
it — which is why the increment above says `Counter.made`, naming the
class on purpose. When a value should be per-object, set it in
`__init__`; when truly shared, address it by class name.

## `@property`: computed attributes

Sometimes a "field" is really a calculation. A **property** is a
method in attribute's clothing — read without brackets:

```python
class Circle:
    def __init__(self, r):
        self._r = r

    @property
    def area(self):
        return 3.14159 * self._r ** 2

c = Circle(2)
print(c.area)            # 12.56636 -- no brackets; computed on demand
```

The `@property` line is a **decorator** — an annotation that changes
how the `def` beneath it behaves; you have now met the species, and
two more of its members close this appendix. Properties can also
guard assignment:

```python
    @property
    def r(self):
        return self._r

    @r.setter
    def r(self, value):
        if value <= 0:
            raise ValueError("radius must be positive")
        self._r = value
```

Now `c.r = -1` raises, yet callers still write plain, honest
`c.r = 3` — chapter 14's bank-grade rules without asking anyone to
call methods. The deeper gift: **start with a plain attribute**, and
if rules or computation arrive later, upgrade it to a property —
every line of calling code continues to work unchanged. That promise
is why Python has no culture of `get_x()`/`set_x()` boilerplate.

## `@classmethod` and `@staticmethod`

Two more decorators, both about methods that don't concern one
particular object. A **classmethod** receives the *class* (as `cls`)
instead of an instance — its star role is the alternate constructor:

```python
class Score:
    def __init__(self, points, name):
        self.points = points
        self.name = name

    @classmethod
    def from_line(cls, line):
        points, name = line.strip().split(",")
        return cls(int(points), name)

    @staticmethod
    def valid(line):
        return "," in line

s = Score.from_line("91,Ada")     # called on the CLASS, not an object
```

`Score.from_line(...)` reads beautifully next to `Score(91, "Ada")` —
two doors into the same class, one for fresh values, one for parsing
chapter 12's file lines. A **staticmethod** is simply a related
function stored in the class for tidiness — no `self`, no `cls`; the
class is acting as its namespace.

## Privacy: the underscore treaty

Python has no `PRIVATE` keyword. The convention carrying its weight is
the single leading underscore: `self._r`, `_helper()` — meaning *"mine;
not part of the promise; touch at your own risk."* Nothing stops
outsiders; everything warns them, and in practice the treaty holds.

A CPython footnote that does **not** hold here: CPython slightly
scrambles double-underscore names (`self.__hidden` becomes
`_ClassName__hidden` — "name mangling") to keep subclasses from
colliding. **MicroPython does not mangle** — `self.__hidden` is plainly
reachable on this machine. Write to the treaty, not the mangling: one
underscore, meant sincerely.

## Duck typing: the interface is the behaviour

A question chapter 14 left implicit: how does code *know* it received
the right kind of object? Python's answer — it doesn't check; it
*tries*. Any object with the right methods passes:

```python
class NullTurtle:
    """Draws nothing -- for testing shape code without a screen."""
    def forward(self, d): pass
    def right(self, a=90): pass
    def pencolor(self, c): pass

shapes.flower(NullTurtle(), 12, 70)    # runs happily; draws nowhere
```

`flower` never demanded a genuine `Turtle` — only something that walks
and turns. ("If it quacks like a duck...") This is why chapter 11
passed `t` as a parameter, and it is a real technique: a `NullTurtle`
tests geometry without a screen; a `LoggingAccount` with the same
methods as `Account` can stand in during debugging. Reach for
`isinstance` only when behaving differently *by type* is the actual
requirement; most days, the duck suffices.

## When *not* to use any of this

The most advanced class technique is restraint:

- A pile of related functions with no shared data — `handy.py` — is a
  **module**, and better for it.
- Pure data with no rules riding along is a **tuple or dictionary**.
- One instance, made once, used everywhere? A module with variables
  often serves more simply than a class with one object.
- And inheritance is the *last* tool, after an attribute (has-a) has
  been given first refusal.

The heuristics of chapter 14 scale all the way up: bundle when data
and rules travel together; guard the rules where the data lives; and
choose the simplest shape that holds what you actually have.

## Where to read further

- MicroPython's documentation (differences from CPython included):
  **https://docs.micropython.org/**
- The full data model — every `__hook__` Python offers — lives in the
  CPython reference; explore it once `__init__` and `__str__` feel like
  old friends.
