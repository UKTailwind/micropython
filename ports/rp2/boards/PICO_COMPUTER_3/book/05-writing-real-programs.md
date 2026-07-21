# Chapter 5 — Writing real programs: the editor

Everything you have typed so far vanished the moment it ran. That was fine
for conversation, but a *program* — the list of instructions from
chapter 1 — needs to be written down, kept, changed, and run again
tomorrow. In this chapter you will write your first program files with the
machine's built-in editor, run them, bring programs in from a PC, and
finally make one run itself every time the machine is switched on. It
completes your driving lessons: after this, the book is about programming.

## Your first program file

Type:

```python
>>> edit("hello.py")
```

The screen changes: this is **pye**, the machine's full-screen text
editor. The file `hello.py` doesn't exist yet, so you are looking at an
empty page with a status bar. Type this program — Enter at the end of each
line, Backspace to fix slips, arrow keys to move around, exactly as you
would hope:

```python
print("Hello from my first program!")
print("I can calculate:", 6 * 7)
beep()
```

Now the two keystrokes that matter most: **Ctrl-S saves** (it will confirm
the name — press Enter) and **Ctrl-Q quits**, back to the familiar
prompt. Run your program:

```python
>>> run("hello.py")
Hello from my first program!
I can calculate: 42
```

— and it beeps. Three things deserve a look before we move on:

- The file is real: `ls()` shows `hello.py`, it survives power-off, and
  `cat("hello.py")` prints your instructions back.
- Chapter 3's prediction came true: `6 * 7` alone on a line would have
  done nothing visible. **Programs never echo** — anything a program wants
  seen, it must `print`.
- Everything you can do at the prompt works in a program: `beep()`,
  `cls()`, the maths, all of it. A program *is* the conversation, saved.

> **Coming from MMBasic:** `edit` is `EDIT`, and the workflow (edit, save,
> run, edit again) is the same. There is no `SAVE` command — the editor's
> Ctrl-S does that — and no line numbers to type, ever.

> **Pico Computer 3 specific.** `edit()`, `run()`, `autosave()` and the
> `/main.py` auto-run are this machine's own — plain MicroPython has no
> built-in editor or `run` command. On other boards you write `.py` files
> in an editor on your PC and copy them across with a tool like `mpremote`,
> then `import` or `exec()` them. Here the machine is self-contained,
> editor and all.

## Getting comfortable in the editor

You already know the survival keys: type, arrows, Backspace, **Ctrl-S**,
**Ctrl-Q**. (Quitting with unsaved changes asks `y/N/f` — `y` saves
first, `f` abandons the changes.) The rest earn their keep as programs
grow. The ones worth learning this week:

| Key | Action |
|---|---|
| `Ctrl-Z` / `Ctrl-Y` | Undo / redo — fearlessness on tap |
| `Ctrl-F` / `Ctrl-N` | Find / find next |
| `Ctrl-G` | Go to a line number |
| `Tab` / `Shift-Tab` | Indent / unindent the line (or selection) |
| `Ctrl-P` | Toggle `#` comment on the line (or selection) |
| `Ctrl-L`, then move | Start a selection (then `Ctrl-C` copy, `Ctrl-X` cut, `Ctrl-V` paste) |
| `Ctrl-K` | Jump to the matching bracket |
| `Ctrl-T` / `Ctrl-B` | Top / bottom of the file |

Two of these need a word now. **`Tab` indents** — chapter 7 will reveal
why indentation is sacred in Python, and this key manages it. And
**`Ctrl-P` comments**: a line starting with `#` is a **comment** — the
machine ignores it completely; it exists for humans. You will use comments
to leave notes to your future self, and `Ctrl-P` to temporarily switch a
line off while testing.

You will also notice the editor colours your code — keywords cyan, text
strings magenta, comments yellow, numbers green. This is nothing you need
manage; it just makes structure visible, and after a week you will notice
a misspelled keyword *because* it fails to change colour.

(The full key list is in the User Manual, section 4. And a shortcut worth
knowing: in `fm`, **E** opens the highlighted file in the editor.)

## When programs go wrong: your first debugging workflow

Errors in a file are more interesting than errors at the prompt, because
now the *location* matters. Deliberately break your program:
`edit("hello.py")`, change the second `print` to `pint`, save, quit, run:

```python
>>> run("hello.py")
Hello from my first program!
Traceback (most recent call last):
  File "<stdin>", line 1, in <module>
  File "pcshell.py", line 101, in run
  File "hello.py", line 2, in <module>
NameError: name 'pint' isn't defined
```

Read it bottom-up, as chapter 3 taught: a `NameError`, name `pint` — and
one line up, the address: **`File "hello.py", line 2`**. (The lines above
that trace the journey: your keystroke at the prompt, then the machine's
own program-launcher, `pcshell` — you can always ignore everything except
the *deepest* line that names one of *your* files.) Notice also the first
line printed before the crash: a program runs *until* the problem line.
So the workflow, which will serve you for the rest of this book:

1. Run. Read the traceback's last line for *what*, the line above for
   *where*.
2. `edit("hello.py")`, **Ctrl-G**, type the line number.
3. Fix, save, run again.

Fix the `pint` before moving on — no program left behind broken.

## A program with rhythm

Programs run their lines in order, at full speed — millions of
instructions per second. To make something unfold in *human* time, you
pause it. Create `fanfare.py`:

```python
import time

print("Ready...")
time.sleep(1)
beep(440, 200)
time.sleep(0.3)
beep(550, 200)
time.sleep(0.3)
beep(660, 400)
print("Ta-da!")
```

`time.sleep(1)` stops the program dead for one second (`0.3` for
three-tenths). The `import time` line fetches the toolbox, exactly as
`import math` did in chapter 3 — note that in a *file*, imports
conventionally go at the top. Run it: a one-second dramatic pause, three
rising notes, ta-da. It is five seconds of theatre, and it is *yours* —
change the notes, the timing, the words.

One more fact about `run()` worth filing away: each run starts
*fresh-ish*. Your program's own variables evaporate when it finishes —
nothing it did leaks back to your prompt, and nothing stale leaks into
the next run. But "fresh" stops at your program's front door: an
imported library stays cached (chapter 11 has the gotcha that follows
from this), hardware you switched on stays on, and settings you saved
persist — a few things even survive a reset. A program's output is what
it prints, plays, and saves to files; the machine around it keeps its
own memory.

## Passing arguments to a program

A good program is worth running twice — with different instructions each
time. Anything you add to `run()` after the filename is handed to the
program as its **arguments**. Create `countdown.py`:

```python
import sys
import time

seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 5

for n in range(seconds, 0, -1):
    print(n, "...")
    beep(440, 100)
    time.sleep(1)
print("LIFT OFF!")
beep(880, 600)
```

```python
>>> run("countdown.py")        # the classic five
>>> run("countdown.py", 10)    # mission control wants ten
```

The program finds its arguments in `sys.argv`, a list: entry `0` is the
program's own name, and the rest are what you passed — **always as
strings**, which is why the program says `int(sys.argv[1])` before
counting with it. The `if len(sys.argv) > 1 else 5` supplies a default
so the bare `run()` still works: accepting an argument shouldn't mean
*demanding* one.

This is not a local custom. Desktop Python programs receive their
command line in exactly this way, so every `sys.argv` recipe on the web
works here unchanged — and the habit transfers straight to any Python
you write on a PC.

> **Coming from MMBasic:** this is `RUN "prog", cmdline` and
> `MM.CMDLINE$` — except the arguments arrive already split into a
> list, no `FIELD$` chopping required.

## Getting programs in from a PC

You will not type everything on the machine. Programs come from PCs — your
own, or code from books and the web. Four routes, in order of ceremony:

**The SD card** — chapter 4's sneakernet. Save the `.py` on the card from
the PC, walk it over, `run("/sd/prog.py")` (or copy it to flash with `fm`).
Best for lots of files.

**`autosave()` — paste over the console.** If the USB-C port is connected
to a PC and a terminal program is open (chapter 2's lifeline), this is the
zero-fuss route for one program:

```python
>>> autosave("game.py")
```

Everything you now *paste* (or type) into the terminal is written straight
into `game.py`. Paste your program, press **Ctrl-Z**, and it reports what
it saved; Ctrl-C cancels instead. Copy a program from anywhere — an
editor, a web page — and it is on the machine in two seconds. (If pasted
text appears doubled on screen, turn off "local echo" in the terminal —
the board already echoes what it hears.)

**XMODEM — proper file transfer, both directions.** For files that must
arrive byte-perfect, or for getting files *off* the machine without an SD
card, the serial console speaks the classic XMODEM protocol:

```python
>>> xrecv("game.py")     # then File > Transfer > XMODEM > Send in the terminal
>>> xsend("game.py")     # then File > Transfer > XMODEM > Receive
```

Any old-school terminal (TeraTerm, for instance) has these menu items.
Details in the User Manual, section 13.

**`mpremote` — the standard MicroPython tool.** For real project work —
copying many files, keeping a backup, editing on the PC and pushing the
changes over — the MicroPython project's own `mpremote` is the grown-up
choice. It runs on your PC (`pip install mpremote`), connects over the
same USB-C serial link, and gives you file copying, whole-folder
transfer, and even *mounting* the board as a drive so your PC editor
saves straight onto it:

```text
mpremote cp game.py :          # copy one file onto the board
mpremote cp -r mygame/ :       # copy a whole folder, recursively
mpremote mount .               # show the current PC folder AS the board's drive
mpremote                       # just the REPL, like a terminal
```

It is what to graduate to once a project outgrows paste-and-go; the
MicroPython docs have the full command set. Unlike the first three
routes, `mpremote` is not a Pico Computer 3 command — it is a standard
part of the MicroPython world, and the same skills serve any board.

> **Coming from MMBasic:** `autosave()` is `AUTOSAVE` (Ctrl-Z ends it
> there too), and `xrecv`/`xsend` are `XMODEM RECEIVE`/`SEND`. Old habits
> transfer directly.

> **A note on PC editors.** Two popular tools — **Thonny** and the
> MicroPython **VSCode** extensions — talk to boards directly, and many
> people like them. Be warned, though: they take over the serial console
> to manage files, and that can quietly interfere with a program that is
> itself using the console (the screen REPL, `input()`, paste mode). If a
> program misbehaves *only* while such an editor is connected, disconnect
> it and try again from a plain terminal — that alone explains a
> surprising share of mysteries.

## Making a program run at switch-on

The final piece turns your Pico Computer from a machine that *has*
programs into a machine that *is* one. At every power-up, after the system
starts, the machine looks for a file called **`/main.py`** on the flash —
and if it exists, runs it. No key presses, no `run()`: switch on, program
runs.

```python
>>> cp("fanfare.py", "/main.py")
```

Switch off, switch on: the machine now boots with a fanfare, then drops to
the prompt as usual. Any program can be installed this way — when you have
written the Breakout of chapter 23, `cp` it to `/main.py` and you have
built a games console.

Two safety notes. If `main.py` misbehaves (or never ends — many programs
are deliberate forever-loops, as you will see in chapter 8), **Ctrl-C**
stops it and returns you to the prompt; the machine underneath is always
intact. And to uninstall, simply `rm("/main.py")`. Keep your program under
its own name and treat `/main.py` as a copy — then changing your mind is
always one `cp` or `rm` away.

> **Coming from MMBasic:** `/main.py` is `OPTION AUTORUN`, in file form.

## Where you now stand

Part I is complete. You can flash firmware, drive the screen, keyboard and
files, write and edit programs, move them on and off the machine, and
install one at boot. The machine is fully yours. What remains is the
actual art: Part II teaches you to *think in Python* — and it starts with
teaching the machine to remember things, in chapter 6.

## Experiments

1. Add a fourth, deeper note to the fanfare, and a final line that prints
   your name in a frame of `=` signs (chapter 3's string-repeat trick —
   `"=" * 40` — earns its keep at last).
2. Break `fanfare.py` on purpose: delete the `import time` line and run
   it. Read the traceback — what is the error, and *which line* does it
   point to? Notice it is not line 1. Why not? Then put the import back.
3. Practise the paste route: with a terminal connected, `autosave` a
   program you copy from anywhere, and run it. This will be your main
   supply line for the rest of the book.
4. Install the fanfare as `/main.py`, power-cycle to enjoy your new boot
   sound, then decide: keep it, replace it, or `rm` it. (Owners of
   machines that greet them with a fanfare report a 100% smile rate. Data:
   invented, but test it.)

## Challenges

1. Write `card.py`: a birthday card for someone real — their name framed
   in a border, a message, a tune. Sequential statements, sleeps and beeps
   are all it takes to make something genuinely givable.
2. Write `menu.py` that prints a numbered list of your programs, then runs
   one of them with `run()`. (Yes — programs can run programs. What
   happens when the inner one finishes?) In chapter 7 you will let the
   user pick the number; for now, hard-wire your favourite.
3. Investigate: what happens to `fanfare.py`'s dramatic pause if you move
   `import time` to the *bottom* of the file? Predict, then test, then
   explain the rule "imports at the top" to an imaginary friend.
