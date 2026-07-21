# Chapter 4 — Finding your way around: files and the shell

Everything you will ever make on this machine — programs, pictures, game
scores, notes — will live in a **file**. This chapter is about looking
after them: seeing what you have, organising them into folders, copying
and renaming and deleting, and moving them between the machine and an SD
card. First with typed commands, and then with `fm`, a full-screen file
manager that makes it all feel like a desktop computer.

## Two drives

The machine has two places to keep files:

- **The flash drive**, called `/`. This is the 12 MB of storage built into
  the board — the machine's filing cabinet. It survives power-off (you
  proved that in chapter 2: your keyboard setting lives here, in a file).
- **The SD card**, called `/sd`. It exists only while a card is in the
  slot, and can be swapped without switching off. Its superpower: a PC can
  read and write the same card, so it is the bridge between your machine
  and the rest of the world — and cheap, roomy storage for music, images
  and whole projects.

A file's full name — its **path** — spells out where it lives, with `/`
between the steps: `/notes.txt` is a file on the flash;
`/sd/games/breakout.py` is a file in the `games` folder of the SD card.

> **Coming from MMBasic:** paths replace drive letters — `A:` is the
> flash `/`, `B:` is the SD card `/sd`. And the commands you know are all
> here under Unix-style names: `FILES` → `ls`, `KILL` → `rm`, `COPY` →
> `cp`, `RENAME` → `mv`, `MKDIR` → `mkdir`, `CHDIR` → `cd`.

## Where am I?

The machine always has a notion of the folder you are "standing in" — the
**current directory**. Two commands orient you:

```python
>>> pwd()
/
>>> ls()
       211  2026-07-12 09:30  settings.json
1 item
```

`pwd` — *print working directory* — says where you are; a freshly started
machine stands at `/`, the top of the flash drive. `ls` **lists** what is
here: so far, one file — `settings.json`, a couple of hundred bytes, which
is where your keyboard layout from chapter 2 is remembered. Each line
shows the size in bytes, when the file last changed (the battery-backed
clock at work), and the name.

One thing before we go on: these commands are MicroPython, so the brackets
are required, and **file names go in quotes** — they are strings, exactly
like `"hello"` in chapter 3. `ls` alone names the command; `ls()` runs it.

> **Pico Computer 3 specific.** These shell-style commands — `ls`, `cd`,
> `pwd`, `cp`, `mv`, `rm`, `mkdir`, `cat`, and the `fm` file manager — are
> conveniences this machine adds. Standard MicroPython does the same jobs
> through the `os` module (`os.listdir()`, `os.remove()`, `os.rename()`,
> `os.mkdir()`, `os.chdir()`…), which works here too if you prefer it. The
> friendly names are for comfort at the prompt; the `os` names are what
> travel to other boards.

## Making some files to practise on

A brand-new machine is nearly empty, so let's create a practice file. The
polite way to make files is chapter 12's business, but here is a
three-line preview — a spell to copy now and understand later:

```python
>>> f = open("notes.txt", "w")
>>> f.write("Remember the milk\n")
18
>>> f.close()
```

(That `18` is the echo you know from chapter 3: `write` reports how many
characters it wrote — seventeen letters and spaces, plus one invisible
"start a new line" character, spelled `\n`.)

Now look around, and read your file back with `cat`:

```python
>>> ls()
        18  2026-07-15 10:14  notes.txt
       211  2026-07-12 09:30  settings.json
2 items
>>> cat("notes.txt")
Remember the milk
```

`cat` prints a text file to the screen. For a file longer than the screen
it stops at each screenful with `PRESS ANY KEY ...` — any key for the next
page, **q** to give up. (You will appreciate this the first time you `cat`
a thousand-line program.)

## Folders, copies, and moves

A filing cabinet with everything in one drawer is no filing cabinet at
all. `mkdir` **makes a directory** (folder), and `cd` **changes** into it:

```python
>>> mkdir("lists")
>>> cd("lists")
>>> pwd()
/lists
>>> cd("/")
```

`cd` with no path takes you home to `/` from anywhere — worth remembering
when you feel lost.

`cp` **copies** — give it the file and the destination. If the destination
is a folder, the copy lands inside; if it is a name, the copy takes that
name:

```python
>>> cp("notes.txt", "lists")            # a copy inside the lists folder
>>> cp("notes.txt", "backup.txt")       # a second copy, new name
```

`mv` **moves** — same idea, but the original goes away. Moving a file to a
new name *is* renaming it, and there is no separate rename command:

```python
>>> mv("backup.txt", "shopping.txt")    # renamed
>>> mv("shopping.txt", "lists")         # ...and filed away
```

And `rm` **removes**. Respect this one: there is no recycle bin and no
undo on this machine — a removed file is gone, immediately and forever:

```python
>>> rm("notes.txt")
```

(`rmdir` removes a directory, and insists it be empty first — a small
safety catch.)

## Wildcards: acting on many files at once

Every command so far handled one file. A **wildcard** pattern handles a
crowd: `*` stands for "anything here", `?` for "any single character".

```python
>>> ls("*.txt")                 # only the .txt files
>>> cp("*.py", "/sd/backup")    # copy every Python program to the SD card
>>> rm("*.tmp")                 # delete all the .tmp files
```

`cp` and `mv` with a wildcard need the destination to be a folder (the
machine cannot copy five files onto one name). Matching ignores capital
letters, so `*.txt` also finds `NOTES.TXT`. (The flash filesystem itself
is **case-sensitive**, though: `notes.txt` and `NOTES.TXT` would be two
genuinely different files. It is only the wildcard *matching* that is
relaxed about case, as a convenience.)

Treat `rm` plus wildcard as the chainsaw it is. `ls` accepts the *same
patterns*, so the professional habit is: **`ls` the pattern first**, see
exactly what matches, then arrow-up, change `ls` to `rm`, and run it.

## The SD card and your PC

Slide a card in and it appears as `/sd`; take it out (when nothing is
using it) and `/sd` vanishes. No unmount ceremony required.

The workflow this enables is wonderfully old-school: fill a card with
music, images or code on a PC, sneaker it over, and
`ls("/sd")`. Everything you learned above works across both drives —
`cp("/sd/photo.jpg", "/")` pulls a file in from the card,
`cp("game.py", "/sd")` publishes one back out. Chapter 5 adds a way to get
programs across *without* the card (the USB-C console), but for bulk —
a folder of MP3s, a sprite sheet collection — the card is king.

## `fm` — the file manager

Typed commands are precise, scriptable (a program can call `cp` too — file
in that thought for later), and mandatory knowledge. They are also, for a
morning of reorganising, slow. Enter:

```python
>>> fm()
```

The screen becomes two side-by-side panes, each showing a folder. The
controls, which are also listed at the bottom of the screen:

| Key | Action |
|---|---|
| **↑/↓**, PgUp/PgDn, Home/End | move the highlight |
| **Tab** (or **←/→**) | switch the active pane |
| **Enter** | open: enter a folder, or act on a file *by type* |
| **Backspace** | go up one folder |
| **Space** | select/deselect the file, and step down a row |
| **C** / **M** | copy / move the selected file(s) **to the other pane** |
| **R** / **D** | rename / delete (delete asks first) |
| **N** | new folder |
| **E** | edit the file (chapter 5's editor) |
| **S**, **+/-** | stop audio / volume |
| **Q** | quit back to the prompt |

Three ideas make `fm` more than a pretty `ls`. First, the **two panes**:
point the left one at a source (say `/sd`) and the right one at a
destination (say `/`), and **C** copies whatever is highlighted from one
to the other. Filing a card full of downloads takes seconds.

Second, the **Space bar selects several files at once**. Each press marks
the highlighted file — it turns yellow, the status line counts the
haul — and steps down a row, so a run of presses sweeps up a whole block
of files (press Space again on a file to change your mind). Then one
**C**, **M** or **D** copies, moves or deletes *the lot*: a multi-file
delete asks once, "Delete 12 selected files?". The selection belongs to
its pane and is forgotten when that pane changes folder.

Third, **Enter does the right thing per file**. A `.py` program *runs* —
and when it finishes, `fm` is waiting where you left it. Music (`.mp3`,
`.wav`, `.mod`) *plays*, and keeps playing in the background while you
browse on. An image (`.jpg`, `.bmp`, `.png`) is *shown* — any key returns.
A text file is *viewed*, a page at a time. The file manager is thus also
your programs menu, jukebox and photo viewer — many owners live in it.

> **Coming from MMBasic:** `fm()` is MMBasic's `FM`, keys and all.

Start `fm("/sd")` to open both panes on the card, or plain `fm()` for the
current directory.

## Experiments

1. Rebuild the practice file (the `open` spell), then use only typed
   commands to end with `lists/todo.txt` as its sole surviving copy —
   no strays left anywhere. `ls` is your audit tool.
2. Do the same job entirely inside `fm` — create a folder with **N**, copy
   with **C**, rename with **R**, delete with **D**. Which way suits you?
   (There is no right answer; there is a reason both exist.)
3. With an SD card and a PC: put three files on the card from the PC, then
   use `fm`'s two panes to bring them onto the flash — Space, Space,
   Space, **C**, one trip. Note what the PC calls the card versus what
   this machine calls it.
4. Run `ls("/sd/*.??3")` on a card with music on it. Work out what the
   pattern matches before you look.

## Challenges

1. Set up a folder structure for your life with this book — something like
   `/projects`, `/projects/games`, `/scraps` — and re-file what you have
   so `ls("/")` shows folders, `settings.json` and nothing else. Future
   chapters assume tidy habits, not this exact layout; make one you'll
   keep.
2. In `fm`, highlight `settings.json` and press Enter. Predict what
   happens first (it is a text file). Then explain the contents from what
   you set in chapter 2.
3. A puzzle from chapter 2's troubleshooting table: what would
   `rm("/settings.json")` do, and when might that be exactly what you
   want? (Don't run it idly — is your keymap worth re-typing?)
