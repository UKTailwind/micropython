# Chapter 12 — Strings and files

Every program you have written so far suffers the same amnesia: switch
off, and everything it gathered is gone. Chapter 2 taught you *why*
(workspace forgets, flash remembers); this chapter finally moves your
data to the remembering side. It comes in two halves that belong
together: **strings** — because data on its way to and from storage
travels as text, and you need the tools to slice and scrub it — and
**files**, the full story behind the `open()` spell you have been
trusting since chapter 4. By the end, your high-score table survives
power-off, and the machine keeps a diary.

## Strings are sequences

Chapter 10 promised that string slicing would earn its keep. First,
notice how much you already know, because a string behaves like a list
of characters: `len()` counts them, positions fetch them, slices cut
them, `in` searches them, `for` walks them:

```python
>>> word = "Pico Computer"
>>> word[0]
'P'
>>> word[5:]
'Computer'
>>> "Comp" in word
True
```

One crucial difference: strings are like tuples, not lists — **fixed at
birth**. You never change a string; you make a *new* one and, usually,
store it back under the same name. Keep that in mind as the methods
arrive, because it explains their one surprise.

## String methods: the text workshop

Strings carry a workshop of methods (chapter 9's word — commands after
the dot). The everyday bench:

| Method | Does |
|---|---|
| `s.upper()`, `s.lower()` | a SHOUTED / whispered copy |
| `s.strip()` | copy with spaces (and line-ends) trimmed off both ends |
| `s.replace(old, new)` | copy with every `old` swapped for `new` |
| `s.startswith(x)`, `s.endswith(x)` | True/False questions |
| `s.count(x)` | how many times `x` appears |
| `s.find(x)` | position of the first `x` (−1 if absent) |

```python
>>> shout = "stand by for launch"
>>> shout.upper()
'STAND BY FOR LAUNCH'
>>> shout
'stand by for launch'
```

There is the surprise, in daylight: `upper()` **returned** a new string
and left the original untouched — unlike `list.sort()`, which reworks
the list in place. To keep the result, catch it: `shout =
shout.upper()`. Forgetting this is a rite of passage; now it's a short
one.

Two of these methods solve standing problems from earlier chapters.
**`.lower()` fixes the fussy quiz**: compare `answer.lower() ==
a.lower()` and capital letters stop mattering. And **`.strip()` is
input hygiene**: people type stray spaces, and `input(...).strip()`
quietly forgives them. Make it a habit today — half the "why doesn't it
match?!" bugs in beginner programs are an invisible space.

> **Coming from MMBasic:** the string functions came along —
> `UCASE$`/`LCASE$` are `.upper()`/`.lower()`, `INSTR` is `.find()`,
> and `MID$`/`LEFT$`/`RIGHT$` all fell into slicing: `MID$(s, 3, 4)` is
> `s[2:6]` (mind the 0-base), `LEFT$(s, 4)` is `s[:4]`, `RIGHT$(s, 4)`
> is `s[-4:]`.

## Split and join: strings ↔ lists

The bridge between this chapter's two worlds. `.split()` cuts a string
into a **list** of pieces:

```python
>>> command = "take rusty sword"
>>> command.split()
['take', 'rusty', 'sword']
>>> "91,Ada".split(",")
['91', 'Ada']
```

Bare `split()` cuts at spaces; `split(",")` cuts at commas — the shape
of a thousand data files. (Chapter 10's shop challenge, which begged
for `take sword` as one input: now you can. `words = command.split()`,
then `words[0]` is the verb.)

`.join()` is the return trip — it belongs to the *separator*, and glues
a list into one string:

```python
>>> ", ".join(["eggs", "bread", "coffee"])
'eggs, bread, coffee'
```

Split to work on the pieces, join to put them away: you will use this
pair every time data enters or leaves a file, which is the cue for the
second half.

## Files, the full story

Chapter 4 gave you three lines on trust. Here is what they meant, and
the professional form you will actually use.

Opening a file is asking the filing system for a connection to it —
`open(path, mode)` — and the **mode** says what you intend:

| Mode | Means | If the file exists | If it doesn't |
|---|---|---|---|
| `"r"` | read (the default) | opens it | error |
| `"w"` | write | **wipes it clean** | creates it |
| `"a"` | append | adds at the end | creates it |

Respect `"w"`: opening for write is already an act of destruction —
the old contents are gone before you write a byte. When adding to
something (a diary, a log, a score history), `"a"` is the mode you
meant.

And here is the professional form — new keyword, `with`:

```python
with open("notes.txt", "w") as f:
    f.write("Remember the milk\n")
    f.write("Feed the turtle\n")
```

`with` does what chapter 4's `f.close()` did, but *guarantees* it —
the file is closed when the block ends, even if something goes wrong
inside. It matters on this machine more than most: a file left open is
how data gets lost at power-off. From today, `open` always arrives
wrapped in a `with`; consider `close()` retired along with the spell.

Note also `"\n"` at the end of each `write`: unlike `print`, `write`
starts no new lines for you. What you write is exactly what lands.

Reading back, the loop *is* the natural form — a `for` over a file
visits its lines:

```python
with open("notes.txt") as f:
    for line in f:
        print(line.strip())
```

Each `line` arrives still wearing its `"\n"`, which is why `.strip()`
appears here and will appear in every file-reading loop you ever write.
(For small files, `f.read()` grabs the whole thing as one string
instead.)

One more fact, then the projects. Opening a *missing* file for reading
raises an `OSError`. Handling that gracefully is genuinely chapter 13's
business (it is the chapter's whole subject), but the scores project
below can't wait, so it borrows two lines of `try:`/`except:` the way
chapter 4 borrowed `open()` — copy today, understand on the next page.

> **Coming from MMBasic:** `OPEN "notes.txt" FOR APPEND AS #1` /
> `PRINT #1, ...` / `CLOSE #1` maps to `with open("notes.txt", "a") as
> f:` / `f.write(...)` — and the file-number bookkeeping is gone. The
> `with` block is the `CLOSE` you cannot forget.

## Project: the diary

A program that remembers is a different kind of thing — it accumulates
a past. `edit("diary.py")`:

```python
def add_entry():
    text = input("Today: ").strip()
    if text:
        with open("diary.txt", "a") as f:
            f.write(text + "\n")
        print("Kept.")

def read_diary():
    print("-" * 30)
    with open("diary.txt") as f:
        for i, line in enumerate(f):
            print(f"{i + 1:3}  {line.strip()}")
    print("-" * 30)

while True:
    choice = input("(w)rite, (r)ead or (q)uit? ").strip().lower()
    if choice == "w":
        add_entry()
    elif choice == "r":
        read_diary()
    elif choice == "q":
        break
```

Everything from Part II shakes hands here: functions carve it into
`add_entry`/`read_diary`, the `while True`/`break` menu is chapter 8's,
`enumerate` numbers the entries, `.strip().lower()` forgives typing,
and `"a"` mode means each day *adds*. Write an entry, **switch the
machine off**, switch on, read the diary. That moment — your words
coming back — is this chapter.

(First `r` before any `w`? An `OSError`, as promised. Let it happen,
read the last line, and carry the itch into chapter 13 — or write a
first entry and move on.)

## Project: scores that survive — a library

Chapter 10's hall of fame, made permanent *and* made a component:
chapter 11 taught you that any file is a module, so build it once and
every game you ever write gets a high-score table for one `import`.
`edit("scorelib.py")`:

```python
# scorelib.py -- persistent high scores.
#   import scorelib
#   scorelib.add(120, "Ada"); scorelib.show()

FILE = "/scores.csv"

def load():
    """Return the saved list of (points, name), best first."""
    scores = []
    try:
        with open(FILE) as f:
            for line in f:
                points, name = line.strip().split(",")
                scores.append((int(points), name))
    except OSError:
        # no file yet: first ever run -- empty list
        pass
    scores.sort(reverse=True)
    return scores

def save(scores):
    with open(FILE, "w") as f:
        for points, name in scores:
            f.write(f"{points},{name}\n")

def add(points, name):
    """Record a result. Returns its rank (1 = a new best)."""
    scores = load()
    scores.append((points, name))
    scores.sort(reverse=True)
    save(scores)
    return scores.index((points, name)) + 1

def show(top=5):
    print("=" * 24)
    print("   HALL OF FAME")
    print("=" * 24)
    for i, (points, name) in enumerate(load()[:top]):
        print(f"{i + 1}. {name:12} {points:5}")
```

Read `load` and `save` as a pair — they are split and join's whole
philosophy applied to storage. Saving: each tuple becomes a line,
`f"{points},{name}\n"`. Loading: each line splits at the comma, and
`int()` undoes what the file did to the number (files hold *text* —
the `"91"`-versus-`91` lesson of chapter 6, now structural). The
`try`/`except OSError` is the borrowed incantation: *if the file isn't
there yet, start empty* — chapter 13 makes it make sense.

Take it for a spin at the prompt — then power-cycle and `show()` again:

```python
>>> import scorelib
>>> scorelib.add(120, "Ada")
1
>>> scorelib.add(200, "Grace")
1
>>> scorelib.add(64, "Alan")
3
>>> scorelib.show()
```

`add` even reports the rank it earned — a returned value (chapter 11)
your games will turn into "NEW HIGH SCORE!" fanfares in Part IV. This
file is the first entry in your permanent toolkit: `/lib`-worthy.

## Jumping around: `seek`, `tell` and fixed-length records

Everything so far has treated a file like a cassette tape: start at the
beginning, run to the end. In truth every open file has a **pointer** —
the position where the next read or write will happen — and you can
move it at will:

- `f.tell()` — where is the pointer now? (In bytes from the start.)
- `f.seek(n)` — put the pointer at byte `n`.
- `f.seek(n, 2)` — put it `n` bytes relative to the *end* (so
  `f.seek(-20, 2)` is "20 bytes before the end").
- `f.read(n)` — read just `n` bytes from wherever the pointer stands.

And one more mode for the chapter's table: `"r+"` opens for reading
*and* writing **without wiping anything** — unlike `"w"`, the file's
contents survive, and writes land wherever the pointer points.

What is this for? The classic answer is the **fixed-length record
file** — a file where every entry is padded to exactly the same size,
because then *arithmetic replaces searching*: record number `i` lives
at byte `i × size`, and you can jump straight to it, read it, or
overwrite it in place, without touching the rest of the file:

```python
REC = 20                             # 12 name + 7 score + newline

def format_rec(name, points):
    # padding makes EVERY record 20 bytes
    return f"{name:12}{points:7}\n"

# read record number 2 directly -- no loop, no loading the file
with open("scores.dat") as f:
    f.seek(2 * REC)
    line = f.read(REC)
    name, points = line[:12].strip(), int(line[12:19])

# update record number 1 in place, leaving its neighbours
# untouched
with open("scores.dat", "r+") as f:
    f.seek(1 * REC)
    f.write(format_rec("Grace", 999))
```

The chapter's f-string padding (`{name:12}`) turns out to be the whole
technology: same-width records in, slice-and-`strip` out.

So when would you use this instead of `scorelib`'s load-everything,
save-everything? **Mostly, you wouldn't** — for dozens or hundreds of
entries, rewriting the whole file is simpler, and simpler survives.
Fixed records earn their keep when the file gets *big*: a thousand
game-world tiles on the SD card, a season of sensor readings — cases
where loading everything into memory is wasteful and updating one entry
shouldn't mean rewriting a megabyte. File the technique under "good to
own, rarely the first tool."

One honest caution: `seek` counts **bytes**, and exotic characters
(accents, emoji) occupy *more than one byte* each, which would knock
the arithmetic askew. Keep fixed-record files to plain unaccented text
and the characters-equal-bytes assumption holds.

> **Coming from MMBasic:** this is `OPEN ... FOR RANDOM`, `SEEK #1` and
> friends — except the record length lives in your arithmetic rather
> than the OPEN statement. `tell` ≈ `LOC()`, and seeking past old data
> to append is legal here too.

## One more gift: `sort` meets your functions

Chapter 10, experiment 2 left a wound open: `["banana", "Apple",
"cherry"]` sorts capitals-first. The fix is one of the prettiest ideas
in Python. `sort` accepts a **function** as its `key`, and sorts by
whatever that function returns for each item:

```python
>>> words = ["banana", "Apple", "cherry"]
>>> words.sort(key=str.lower)
>>> print(words)
['Apple', 'banana', 'cherry']
```

Read it: *sort them as if lowercased* (the items themselves are
unchanged — the key is only the yardstick). Note what got passed:
`str.lower` with **no brackets** — the function itself, not a call.
Functions are values, as chapter 11's challenge 2 hinted; handing one
tool to another tool is where that idea starts paying compound
interest.

## What you now hold

- **Strings are sequences** — index, slice, `in`, `for` — but fixed:
  methods return *copies* (`s = s.upper()` to keep one).
- **The bench**: `.lower()` kills case bugs, `.strip()` kills
  invisible-space bugs — `input(...).strip()` as a reflex — plus
  `.replace`, `.startswith`, `.find`.
- **`.split()`** turns a string into a list; **`sep.join(list)`**
  turns it back. Together they are a file format.
- **Files**: `open(path, mode)` with `"r"`/`"w"` (destroys!)/`"a"`;
  always inside `with` (the close you can't forget); `write` adds no
  `"\n"` for you; read with `for line in f:` + `.strip()`.
- **Data survives as text**: save = format with f-strings, load =
  `split` + `int()`. `scorelib.py` is the pattern, reusable forever.
- **The file pointer**: `tell`/`seek` jump anywhere; `"r+"` updates
  without wiping; same-width records make jumping *arithmetic* — the
  big-file tool, not the first tool.
- **`sort(key=function)`** — functions are values you can hand over.

## Experiments

1. `"  Grace Hopper  ".strip()`, `"banana".count("na")`,
   `"mississippi".replace("ss", "SS")` — predict each, then check. Then
   the party trick: `"".join(reversed("stressed"))` (`reversed` walks
   anything backwards; `join` reassembles the letters) — and use it to
   test whether `"rotator"` is a palindrome.
2. Go fix the quiz for real: `quiz2.py`'s comparison becomes
   case-blind, *and* answers get `.strip()`ed. Two edits, both
   one-liners, both permanent habits.
3. Word statistics: read `diary.txt` and report lines, words
   (`len(line.split())`, summed) and characters. You have written `wc`
   — a tool Unix ships to this day.
4. Break `scorelib`'s format on purpose: edit `scores.csv` (with
   `edit`!) and add a line with no comma. Run `show()`. Read the error,
   name the line that died, fix the file. Data from outside is never to
   be trusted — a scar worth having before chapter 13 sells you the
   bandage.
5. What does `"3,4".split(",") == [3, 4]` return, and why? (Chapter 6
   knew.)

## Challenges

1. **The shop, speaking properly.** Chapter 10's inventory now takes
   `take rusty sword` in one line: `split()`, the verb is `[0]`, and
   the item is `" ".join(words[1:])` — two-word treasures welcome.
2. **Timestamps for the diary.** `gettime()` returns a tuple starting
   `(year, month, day, hour, minute, ...)` — unpack it and prefix each
   entry with `[2026-07-15 14:30]`, zero-padded via the format spec
   `{month:02}`. (Manual, section 11, has the details.)
3. **Flashcards.** A `cards.txt` of `question,answer` lines and a
   quizzer that reads it — chapter 10's quiz with the data moved *out
   of the program entirely*. Editing the deck now needs no programmer.
   Ship it with a starter deck: Python vocabulary from this book's
   glossary.
4. **The Caesar wheel.** `ord("a")` gives a letter's number (97);
   `chr(100)` gives the letter back (`'d'`). Write `encode(text,
   shift)` that slides every letter three along (`z` wraps to `c`),
   `decode` to match, and store a *scrambled* secret in a file. Bonus
   swagger: crack a friend's shift by trying all 26.
5. **The ledger.** A fixed-record savings book: `deposit(rec, amount)`
   seeks to account number `rec`, reads its 20-byte record, adds the
   amount, seeks *back* (`tell` before reading helps) and overwrites it
   in place. Ten accounts, no full-file rewrites, and `cat` the file
   afterwards to admire the perfectly aligned columns.
