# Programming the Pico Computer 3
### A complete course in Python — from your first keystroke to games, graphics and real applications

**Book outline — draft 1 (targets firmware v0.8)**

Audience: complete newcomers to programming, plus readers arriving from
MMBasic/PicoMite. No PC required beyond the initial firmware flash — every
chapter is written to be done *on the machine itself*, at the screen and
keyboard.

Pedagogical rules for every chapter:

- Each chapter is built around something you *make* (a picture, a sound, a
  toy, a game, an app) — never a feature tour.
- New language concepts are introduced only when the project needs them.
- Every chapter ends with **Experiments** (guided tweaks) and **Challenges**
  (open-ended, with hints in Appendix F).
- MMBasic readers get a *"Coming from MMBasic"* margin note wherever a
  concept maps onto something they already know (`CLS`→`cls()`,
  `SETTICK`→`machine.Timer`, `FRAMEBUFFER`→overlay/buffer, …).
- Full API details are deferred to the User Manual / appendices; the book
  teaches the 90% you use daily.

---

## Part I — Meet Your Computer

**1. What is the Pico Computer 3?**
A stand-alone computer that boots straight into Python. The RP2350B chip and
what's on the board: HDMI out, USB host port (keyboard/mouse/touch), SD card
slot, audio, DS3231 real-time clock, Wi-Fi/Bluetooth. What "MicroPython" is
and why Python. A note for MMBasic users: same hardware family, same spirit,
a new language.

**2. Getting started: flashing and first boot**
Downloading the firmware `.uf2`. Holding BOOTSEL, dragging the file, the
reboot. Plugging in HDMI, keyboard, SD card. The boot banner and the `>>>`
prompt. The two consoles (screen and serial) and when you'd care.
Troubleshooting: blank screen, keyboard not detected, wrong monitor mode
(`screen()`).

**3. Your first conversation with Python**
The REPL as a calculator and a magic notebook: numbers, text, `print()`.
Making mistakes on purpose — reading error messages without fear.
`cls()`, arrow-key history, Tab completion.

**4. Finding your way around: files and the shell**
The flash drive and the SD card. Shell helpers: `ls`, `cd`, `pwd`, `cat`,
`cp`, `mv`, `rm`, `mkdir`. The dual-panel file manager `fm` — copying,
moving and browsing like a desktop. Wildcards.

**5. Writing real programs: the editor**
`pye` — opening, editing, saving, the key bindings that matter. Writing
`hello.py`, running it with `run()`. Getting programs onto the machine:
`autosave()` (paste from a terminal) and XMODEM. `main.py` and making a
program start at boot.

---

## Part II — Learning Python (with instant pictures)

*The language course. Concepts are taught through the screen — every idea
becomes something you can see.*

**6. Variables, numbers and text**
Naming things. Integers, floats, strings. `input()` and f-strings. Project:
a chat-back program and a units converter.

**7. Making decisions: `if`**
Booleans, comparisons, `and`/`or`/`not`, `elif`/`else`. Project: a
guess-the-number game (with `random`).

**8. Repeating yourself: loops**
`while` and `for`, `range()`, `break`/`continue`. First taste of graphics:
`plot()` from a loop; drawing rows of stars. Project: times-table quizzer.

**9. Turtle graphics: loops you can see**
The `Turtle` — forward, turn, pen colour. Squares, stars, spirals,
polygons; nested loops as flower patterns. This chapter cements loops
visually. Project: a spirograph.

**10. Collections: lists, tuples and dictionaries**
Storing many things; indexing and slicing; looping over collections;
dictionaries for lookups. Project: a high-score table and a quiz from a
question list.

**11. Functions: building your own commands**
`def`, parameters, return values, scope. Refactoring the spirograph into
reusable functions. Project: a shapes library you import from other
programs.

**12. Strings and files**
String methods, splitting and joining. Reading and writing files on flash
and SD. Project: a diary/notes app; saving and loading the high-score
table.

**13. When things go wrong: errors and debugging**
Exceptions, `try`/`except`, tracebacks. A debugging mindset: print,
simplify, isolate. Common beginner traps (indentation, `=` vs `==`, types).

**14. Objects: a gentle introduction to classes**
Why objects — you've been using them all along (`Turtle`, strings, files).
Writing a simple class; attributes and methods. Project: a bank-account /
character-sheet class. (Enough OOP to *use and extend* the board's modules;
no more.)

---

## Part III — Graphics, Sound and Input

**15. The screen up close**
Screen modes and `screen()`; resolutions and colour depths; RGB colour and
the colour helpers. Pixels, lines, boxes, circles, arcs, polygons, filled
shapes. Text on screen: fonts, sizes, transparency. Project: a poster /
clock face.

**16. Bitmap images and sprite sheets**
Loading and saving images; `load_image()` and cutting a sprite sheet;
`hdmi.blit()` and the "skip colour" for transparency. Where to find and how
to make artwork. Project: a photo-frame slideshow.

**17. Smooth motion: buffers and the overlay**
Why animation flickers, and the fix: draw off-screen, then show
(`hdmi.vsync()`, off-screen buffer, overlay layer). Scrolling. Project: a
bouncing-ball demo that goes from flickery to silky.

**18. Sprites**
The `pcsprite` engine: creating sprites, moving them, layers, collisions.
Project: a shooting-gallery.

**19. Tile maps: big worlds from small pieces**
`TileMap` — tile sets, maps bigger than the screen, scrolling a camera.
Project: a top-down explorable world.

**20. Sound and music**
`beep()` and `tone()`; the 4-voice synthesiser `sound()`; playing `.mod`
tracker music and using it for game soundtracks and sound effects. WAV
playback. Project: a tune player and a sound-effects board.

**21. Reading the player: keyboard, mouse and touch**
Three input styles and when each fits: blocking `input()`, key events
(`keyboard.on_key()`), and polling `keydown()` for games. Mouse position
and buttons; the touch screen. Project: a paint program (mouse/touch) and
a two-player reaction duel (`keydown()`).

---

## Part IV — Making Games

*The payoff part: everything from Parts II–III combined, one complete game
per chapter, each introducing one new discipline.*

**22. Anatomy of a game**
The game loop: read input → update world → draw. Frame timing with
`pcgame.Clock` — why fixed timing matters and how `tick()` keeps motion
steady. State (title / playing / game-over). Project: **Pong** built from
nothing, step by step.

**23. Breakout**
Rectangle collisions, ball physics off a paddle, levels, lives and score,
sound effects, difficulty ramp. (The machine ships a `breakout.py` to
compare against.)

**24. A scrolling shooter — Asteroids**
Vectors and rotation (a little `pcmath`), wrapping space, sprite groups,
particle-ish explosions, `.mod` background music.

**25. A tile-map adventure**
Combining `TileMap`, sprites and a camera; simple NPCs; a state machine
for dialogue; saving progress to SD.

**26. Game design interlude: polish**
Title screens, pause, high-score persistence, screen shake, difficulty
curves, playtesting your friends.

---

## Part V — Real Applications

**27. Building desktop-style apps: the GUI toolkit**
`pcgui` — windows, buttons, labels, text boxes, check boxes, sliders;
laying out a screen; responding to clicks and touch. Project: a calculator
and a settings screen.

**28. Time and schedules: the real-time clock**
The DS3231, reading and setting time, `time` module. Project: an alarm
clock / kitchen timer with the GUI and sounds.

**29. Going online: Wi-Fi and the internet**
Connecting to Wi-Fi; setting the clock from NTP; fetching web data with
HTTPS; a taste of MQTT for talking between machines. Project: a live
weather display.

**30. Numbers at speed: maths, plotting and science**
`pcmath` helpers, `ulab` arrays, `plot()` for graphing functions and data.
Project: graphing sensor/maths experiments — a maths lab.

**31. Talking to the outside world: GPIO**
The spare pins: `machine.Pin`, buttons and LEDs, PWM, I2C/SPI/UART for
add-on modules. Project: a physical game controller / desk gadget.

**32. Standing on others' shoulders**
The vast third shelf: the libraries other people wrote, and how to
find, judge and install them — `mip`, Awesome MicroPython,
`micropython-lib`. Worked examples across the range: a QR-code
generator drawn on screen (pure software, no extra hardware), a GPS
receiver on UART, an I2C sensor driver — plus a checklist for telling a
sound library from a shaky one (single- vs double-precision included).
Project: a scannable QR code of the project repository on screen.

---

## Part VI — Under the Hood (advanced)

**33. Doing several things at once**
Timers (`machine.Timer`), pin interrupts (`Pin.irq`), UART events, the
watchdog; rules for writing safe handlers. `asyncio` for cooperative
multitasking — a game that animates *while* downloading. When (not) to
use threads.

**34. Performance and memory**
Why some code is slow; measuring with `time.ticks_*`; memory and `gc`;
`ulab` and blitting instead of per-pixel loops; `const`, buffers,
pre-allocation. Making chapter 24's shooter faster.

**35. The system itself**
Boot sequence, `main.py`, persistent settings, `console()` routing,
the on-device test suite, updating firmware, backing up your work.
Where to go next: MicroPython docs, contributing, the community.

---

## Appendices

**A. Cheat sheet** — one page: shell commands, editor keys, the
prompt's block sign-off (Backspace+Enter; one statement per entry), core
drawing / input / sound calls. (Derived from the manual's Quick
Reference.)

**B. Coming from MMBasic** — a full translation table: MMBasic
statement/function → Pico Computer 3 Python equivalent, with notes on the
idioms that differ.

**C. Module reference pointers** — every `pc*` module and board built-in,
one paragraph each, with a pointer into the User Manual for the full API.

**D. Hardware reference** — pin allocations, screen modes table, SD card
notes, serial console wiring, DS3231 battery.

**E. Python language quick reference** — the subset taught in the book, in
lookup form.

**F. Challenge hints and answers.**

**G. Glossary** — plain-English definitions of every term the book uses.

**H. Troubleshooting** — symptoms → causes → fixes (boot, display, USB,
SD, Wi-Fi, "my program doesn't run").

**I. Classes beyond the cottage** *(written)* — the guided tour past
chapter 14: inheritance and `super()` (with the is-a/has-a test), custom
exceptions, `__repr__`, operator hooks (`__add__`, `__lt__`-driven
sorting), `__len__`/`__getitem__`, class attributes, `@property`,
`@classmethod`/`@staticmethod`, the underscore treaty (and MicroPython's
lack of name mangling), duck typing, and when not to use any of it.
