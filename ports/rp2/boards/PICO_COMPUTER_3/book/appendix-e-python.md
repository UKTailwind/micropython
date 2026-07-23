# Appendix E — Python, the taught subset

Lookup form for everything the course covered, with the chapter that
teaches it.

## Values and operators (3, 6, 7)

Kinds: `int` (exact, any size) · `float` (approximate — `0.1 + 0.2`!)
· `str` · `bool` · `None` (the no-answer answer). Convert with
`int() float() str()`; ask with `type()`.

`+ - * /` (`/` always gives float) · `//` floor-divide · `%`
remainder · `**` power · `+=` and kin. Compare: `== != < <= > >=`
(chain: `0 <= x < 10`) · combine: `and or not` · membership: `in`.
`x if cond else y` folds an if into an expression (14).

## Strings (3, 6, 12)

Quotes either way; `+` joins, `*` repeats; `len(s)`, `s[0]`,
`s[-1]`, slices `s[2:5]`. Methods return **copies**: `upper lower
strip replace startswith endswith count find split join` — reflexes:
`input(...).strip()`, compare `.lower()`. `ord(c)`/`chr(n)`. Bytes
(30): `b"..."`, `.decode()`, `.encode()`.

**f-strings** (6): `f"{x}"`; specs `:.2f` (decimals) `:02` (pad) `:,`
(thousands) `{name:12}` (width) `{x!r}` (repr). `\n` newline, `\\`
backslash.

## Collections (10)

```
lst=[1,2]; lst[0]; lst[-1]; lst[a:b]; lst.append(x); lst.remove(x)
lst.pop(); lst.insert(i,x); lst.sort(reverse=.., key=fn); x in lst
tup = (score, name)            # fixed; unpack: s, n = tup
# no order!
d = {"key": v}; d["key"]; "key" in d; d.items(); d.keys()
```
Empty containers are `False` (`while cards:`). Comprehension (24):
`[b for b in bullets if b[4] > 0]`.

## Control flow (7, 8)

```
if c:  ... elif c2:  ... else: ...
while c: ...                        while True: + break
for x in seq: ...       for i in range(start, stop, step):
break  continue                     for i, x in enumerate(seq):
```
Indentation *is* the block — 4 spaces.

## Functions (11)

```
def f(a, b=default): ... return v        # return ends the call
f(1, b=2)                                # keyword arguments
lambda t: expr                           # tiny nameless function
```
Assigned names are local (the sealed room); functions are values —
`sort(key=str.lower)`, callbacks, dicts of functions. Loop-capture
fix (27): `lambda b, c=ch: ...`. Factories/closures (27):
`def setter(key): def apply(c): ...; return apply`.

## Classes (14; appendix I for more)

```
class Hero:
    def __init__(self, name, hp=20):
        self.name = name; self.hp = hp
    def hit(self, n): self.hp -= n
    def __str__(self): return f"{self.name}: {self.hp}"
h = Hero("Ada"); h.hit(3); print(h)
```
`self` = the thing before the dot. Objects live happily in lists.

## Errors (13)

```
try:
    risky()
# catch precisely -- never bare except:
except ValueError as e:
    ...
finally:
    # runs on ANY exit, Ctrl-C included (17)
    tidy_up()
raise ValueError("why")      # guard your own functions
```
The gallery: `SyntaxError IndentationError NameError TypeError
ValueError IndexError KeyError OSError AttributeError
ZeroDivisionError KeyboardInterrupt MemoryError`. Read tracebacks
bottom-up; deepest line naming *your* file.

## Files (12)

```
# "r" read / "w" WIPE+write / "a" append
with open(path) as f:
    # "r+" update-in-place (+ seek/tell)
    for line in f:
        line.strip()
    f.read()   f.write(s + "\n")
```

## Modules (11)

`import math` → `math.sqrt` · `from shapes import star` · any `.py`
file is a module; search path `sys.path` (`''` = program's folder,
`/lib` = the shelf). Cached per session — **Ctrl-D after editing a
library**. `if __name__ == "__main__":` = run-vs-import (20).

## Built-ins the book used

`print input len range type int float str bool list dict tuple
enumerate sorted reversed min max abs sum round open help ord chr
hex isinstance` — and `id`-free living. For everything else:
https://docs.micropython.org/

## MicroPython differences met in this book

No step slices (`s[::-1]` → use `"".join(reversed(s))`) · dicts truly
unordered · `list.__setitem__` not an attribute · error texts differ
slightly from CPython · epoch is 2000 · `_thread` absent on this
board · builtin modules can't be monkey-patched.
