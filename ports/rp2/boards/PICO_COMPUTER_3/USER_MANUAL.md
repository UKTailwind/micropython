# Pico Computer 3 — MicroPython User Manual

**Firmware:** MicroPython (RP2350B port) for the Pico Computer 3 — version **0.8** (test release).
The REPL banner reports the version:

```
MicroPython v1.29.0-preview on PICO COMPUTER 3 v0.8 with RP2350B
```

This is a customised build of MicroPython that turns the Pico Computer 3 into a
self-contained computer: HDMI display, USB keyboard/mouse/touch, SD card, audio,
a real-time clock and Wi-Fi, all usable directly from the Python prompt. Much of
the functionality is ported from the PicoMite MMBasic firmware.

For the core Python language and the standard library, see the official
MicroPython documentation: **https://docs.micropython.org/** (choose the version
shown in the banner). This manual documents only what is *specific to the Pico
Computer 3*.

---

## 1. Getting started

There are two consoles, and both are active at once:

- **Serial console** — UART1 on **GP8 (TX)** / **GP9 (RX)**, **115200 8N1**.
  Connect a USB-serial adapter and open a terminal (TeraTerm, PuTTY, `screen`,
  `mpremote`). The Delete key works as forward-delete.
- **On-screen console** — text appears on the HDMI monitor and a USB keyboard
  drives it. This starts automatically at boot.

**`console(target)`** routes console *output* (like MMBasic's
`OPTION CONSOLE`); keyboard input — USB and serial — always works:

```python
console("both")     # HDMI screen + serial port (the power-up default)
console("serial")   # serial only: nothing prints on the HDMI screen —
                    #   ideal while testing graphics/sprites
console("screen")   # HDMI screen only: the serial port stays silent
console("none")     # no console output anywhere — also removes the
                    #   blinking cursor from full-screen graphics
console(fg=0x00FF00, bg=0)   # both, with green-on-black screen text
```

`console(False)` is shorthand for `"serial"`, `console()` for `"both"`. The
setting is not persisted — every power-up starts as `"both"`, and RESET
recovers from an invisible `"none"`/`"serial"` state (or type `console()`
blind — input always works). Full-screen programs typically switch to
`"serial"` or `"none"` while running (the on-screen console's cursor blinks
over the artwork otherwise) and restore `console()` in a `finally:`. XMODEM
transfers use the serial port directly and work in any mode.

The USB device port is disabled (there is no USB-CDC prompt); the USB port is a
**host** port for keyboards, mice and touch panels.

At the prompt you can type Python directly, or use the shell-style helpers
(section 4). To run a program from the SD card or flash:

```python
run("/sd/myprog.py")
```

**Auto-run at boot:** standard MicroPython behaviour applies — if `/boot.py`
and/or `/main.py` exist on the flash they run automatically at every start-up
(after the display, keyboard and SD card are ready). Install a program with
`cp("prog.py", "/main.py")`, stop a running one with **Ctrl-C**, uninstall
with `rm("/main.py")`.

**Import search path:** `sys.path` is `['', '.frozen', '/lib']` — the current
directory (`run()` sets this to the program's own folder, so a module next to
the program is always found, on flash or SD), the frozen firmware modules,
and `/lib` on the flash. `import` takes a module name, never a file path; to
search additional folders (e.g. on the SD card) use
`sys.path.append("/sd/mylibs")` — put it in `/boot.py` to apply at every
start-up. Note that modules are cached per session: after editing one, press
Ctrl-D (soft reset) so the next `import` re-reads it.

---

## 2. Pin allocations

The Pico Computer 3 is based on the RP2350B (48 GPIO). The following pins are
dedicated to on-board functions. Pins marked **reserved** are protected and
cannot be claimed with `machine.Pin()`.

| Function | Pin(s) | Notes |
|---|---|---|
| Console UART1 | **GP8** TX, **GP9** RX | 115200 8N1 — *reserved* |
| HDMI (HSTX DVI) | **GP12–GP19** | CLK GP13/12, D0 GP15/14, D1 GP17/16, D2 GP19/18 (differential pairs) — *reserved* |
| SD card (SPI1) | SCK **GP30**, MOSI **GP31**, MISO **GP28**, CS **GP33** | also `machine.SPI(1)` — *reserved* |
| Audio I2S (PCM5102 DAC) | BCLK **GP10**, LRCK **GP11**, DIN **GP22** | SCK pin grounded on the board |
| RTC (DS3231, I2C0) | SDA **GP20**, SCL **GP21** | 400 kHz, address 0x68 |
| PSRAM chip-select | **GP47** | 8 MB PSRAM |
| Wi-Fi / Bluetooth (CYW43) | REG_ON **GP23**, DATA **GP24**, CS **GP25**, CLK **GP29** | wireless chip over PIO-SPI — *reserved* |
| Status LED | CYW43 **GPIO0** | on the wireless chip (not an RP2350 pin) — named pin `"LED"` |
| USB host | native USB port | keyboard / mouse / touch |

The **status LED** is on the CYW43 wireless chip, not on an RP2350 GPIO. It is
exposed as the named pin `"LED"` (an "extended" pin) and driven like any output —
the driver relays the value to the chip over the internal SPI link:

```python
from machine import Pin
led = Pin("LED", Pin.OUT)
led.on(); led.off(); led.toggle()   # also aliased as Pin("WL_GPIO0")
```

Reserved GPIOs (unavailable to `machine.Pin`): **8, 9, 12–19, 23, 24, 25, 28,
29, 30, 31, 33**. Of these, **23/24/25/29** are the CYW43 Wi-Fi/Bluetooth
interface. All other GPIOs are free for your own use with `machine.Pin`,
`machine.ADC`, `machine.PWM`, `machine.I2C`, `machine.SPI`, `rp2.PIO`, etc.

### System configuration

- **CPU clock:** 252 MHz from boot (can be raised to 315 or 378 MHz — see
  `screen()` in section 5). `machine.freq()` reports it. The clock is coupled to
  the HDMI pixel clock, so it must be changed *together with* the display mode via
  `screen(mode, clock)` — the raw `machine.freq(hz)` setter is disabled and raises
  an error pointing you to `screen()`.
- **Floats:** double precision (64-bit). Integers are arbitrary precision.
- **Flash filesystem:** 12 MB LittleFS mounted at `/`.
- **SD card:** FAT, mounted at `/sd`, hot-swappable (section 12).
- **PSRAM:** 8 MB — the Python heap lives here, so large data/images/audio
  buffers are fine.

---

## 3. Modules imported automatically at boot

At start-up the board adds a set of names to the interactive namespace (`__main__`)
so they are available without an `import`. A program launched with `run()`
inherits the same names. The most useful are:

**Modules / objects:** `os`, `machine`, `Pin` (= `machine.Pin`), `framebuf`,
`hdmi`, `Display`, `Turtle` (turtle graphics — section 5), and the named colour
palette (`RED`, `GREEN`, `BLUE`, `WHITE`, `BLACK`, `YELLOW`, `CYAN`, `MAGENTA`, …).

**Shell commands:** `ls`, `run`, `edit`, `pwd`, `cd`, `mkdir`, `rmdir`, `rm`,
`cat`, `cp`, `mv`, `cls`.

**Display / settings:** `screen`, `palette`, `keymap`, `keymaps`, `console`.

**Audio:** `play`, `volume`, `beep`, `stop`, `is_playing`, `pause`, `resume`,
`tone`, `sound`, `mod_sample`.

**Clock:** `settime`, `gettime`, `synctime`.

**Network / time:** `wifi`, `ntpsync`, `tz`.

**Images:** `draw_jpg`, `draw_bmp`, `draw_png`, `save_image`, `load_image`.

**Input devices:** `touch`, `mouse`, `mouse_speed`, `keydown`, `pccursor`
(the visible mouse pointer — section 7).

**File transfer:** `xrecv`, `xsend` (XMODEM over the serial console).

Each is described in the sections below.

---

## 4. Shell commands

Unix-like helpers for working with the filesystem from the prompt. Paths may be
on the flash (`/…`) or the SD card (`/sd/…`).

| Command | Description |
|---|---|
| `cls()` | Clear the console screen (MMBasic's `CLS`) |
| `ls([path])` | List a directory. Accepts glob patterns: `ls("/sd/*.mp3")` |
| `cd(path)` / `pwd()` | Change / show the working directory |
| `cat(path)` | Print a text file, a page at a time (any key = next page, `q` = stop; `cat(path, False)` dumps it all) |
| `cp(src, dst)` / `mv(src, dst)` | Copy / move (rename) a file. A wildcard in `src` acts on every match, e.g. `cp("*.py", "/sd")` (`dst` must be a directory) |
| `rm(path)` | Delete a file. Accepts a wildcard: `rm("*.tmp")` |
| `mkdir(path)` / `rmdir(path)` | Create / remove a directory |
| `run(path)` | Run a `.py` program (fresh namespace, inherits the REPL helpers) |
| `edit(path)` | Open the full-screen **pye** editor (see below) |
| `autosave(path)` | Capture what you paste/type at the console into a file (see below) |
| `fm(path)` | Full-screen file manager — browse, run, play, view, edit (see below) |

### Getting a program onto the board by pasting — `autosave`

`autosave("prog.py")` (MMBasic's `AUTOSAVE`) is the quickest way to drop a small
program onto the board with **no file transfer at all** — no XMODEM, no SD-card
shuffling. Run it, then **paste** (or type) your program into the terminal;
everything you send is written straight to the file. End with **Ctrl-Z** (or
Ctrl-D) to save, or **Ctrl-C** to cancel. Line endings are normalised, so a
paste from any editor works:

```python
autosave("hello.py")     # then paste your code and press Ctrl-Z
run("hello.py")
```

It reads from whichever console you're on (a serial terminal or the USB keyboard
on the HDMI screen). If pasted text appears doubled, turn *off* local echo in
your terminal (the board echoes what it receives).

### File manager — `fm`

`fm()` (MMBasic's `FM`) is a **dual-panel** file manager that makes the board feel
like a computer: two directory panes side by side, so you can copy and move
between folders, and **open a file just by selecting it** — the manager runs,
plays, shows or views it according to type.

```python
fm()          # both panes start in the current directory
fm("/sd")     # both panes start on the SD card
```

- **Two panes:** **Tab** toggles the active pane, or **←/→** select the left/
  right pane directly (its path bar highlights). Each pane browses independently
  — point one at the source folder and the other at the destination.
- **Navigate:** **↑/↓** (and PgUp/PgDn, Home/End) move the highlight; **Enter**
  opens — a directory is entered, a file is acted on by type; **Backspace** goes
  up a directory. Cursor moves are instant (only the changed rows repaint).
- **Open by type on Enter:** `.py` → **run** it (fm restores its screen mode
  afterwards, even if the program changed it); `.wav/.mp3/.flac/.mod` → **play**
  (in the background); `.bmp/.jpg/.png` → **show** the image (any key returns);
  text files (`.txt/.csv/.json/.md/.bas`…) → **view**, a page at a time. So a
  single key does the right thing per file — there's no separate view/play key.
- **Select several files:** **Space** selects/deselects the highlighted file
  (shown in yellow; the status line counts the selection) and steps down a row,
  so holding Space sweeps a range. **C**, **M** and **D** then act on the whole
  selection in one go. The selection belongs to its pane and clears when that
  pane changes directory. Only files can be selected, not directories.
- **Copy / move between panes:** **C** copies the selected file(s) to the
  *other* pane's directory; **M** moves them.
- **Manage:** **E** edit (opens `pye`), **D** delete (confirms; a multi-file
  delete confirms once with the count), **R** rename, **N** new directory.
  **S** stops audio, **+/-** adjust volume.
- **Q** exits.

The bottom **status line shows the selected file's full name** (so a name too
long to fit its pane is still readable), and the **key legend** just above wraps
to fit the screen width, keeping every command visible even at 40 columns. It
works the same on the HDMI screen (USB keyboard) and over a serial terminal; the
default 80-column screen gives each pane a comfortable 40 columns.

### Editing files with `pye`

`edit("/sd/prog.py")` opens the built-in full-screen editor **pye** (robert-hh's
MicroPython-Editor). If the file does not exist it starts an empty buffer and
creates the file on the first save; `edit()` with no argument opens a blank
buffer you can save to a name of your choice.

It works both on the HDMI screen with a USB keyboard and over a serial terminal.
Use the arrow / `Home` / `End` / `PgUp` / `PgDn` keys where you have them; the
`Ctrl-` shortcuts below do the same job on a plain terminal. The editor
**syntax-colours `.py` files** using MMBasic's colour scheme — keywords cyan,
strings magenta, comments yellow, numbers green — and its status bar and
selection are coloured too, on the HDMI screen and a colour serial terminal
alike. Set `pye.Editor.syntax = False` to turn colouring off.

**Files**

| Key | Action |
|---|---|
| `Ctrl-S` | Save |
| `Ctrl-Q` (or `Esc`) | Quit — prompts `y/N/f` if there are unsaved changes (`f` = quit without saving) |
| `Ctrl-O` | Insert another file at the cursor |

**Moving around**

| Key | Action |
|---|---|
| Arrow keys | Move the cursor |
| `Home` / `End` | Start / end of line |
| `PgUp` / `PgDn` | Up / down one screen |
| `Ctrl-←` / `Ctrl-→` | Word left / right |
| `Ctrl-G` | Go to a line number |
| `Ctrl-T` / `Ctrl-B` | Top / bottom of the file |
| `Ctrl-K` | Jump to the matching bracket |

**Editing**

| Key | Action |
|---|---|
| `Backspace` / `Delete` | Delete the character left / right |
| `Tab` / `Shift-Tab` | Indent / unindent the line (or selection) |
| `Ctrl-P` | Toggle Python comment on the line (or selection) |
| `Ctrl-Z` / `Ctrl-Y` | Undo / redo |
| `Ctrl-A` | Toggle editor options (auto-indent, tabs) |

**Select & clipboard**

| Key | Action |
|---|---|
| `Ctrl-L` | Set the selection mark, then move the cursor to extend it |
| `Ctrl-C` | Copy the line (or selection) |
| `Ctrl-X` | Cut the line (or selection) |
| `Ctrl-V` | Paste |

**Search**

| Key | Action |
|---|---|
| `Ctrl-F` | Find (type a pattern) |
| `Ctrl-N` | Find next |
| `Ctrl-R` | Replace |

After saving a `.py` file, run it with `run("/sd/prog.py")` or import it.

---

## 5. Display (HDMI)

The HDMI output is a hardware DVI signal generated by the RP2350 HSTX and scanned
out on core 1. Four modes are available:

| Mode constant | Resolution | Colour |
|---|---|---|
| `hdmi.RGB640` | 640 × 480 | 8-bit (256 colours) — **default** |
| `hdmi.RGB320` | 320 × 240 | 16-bit (RGB565), pixel-doubled to 640 × 480 |
| `hdmi.RGB512` | 512 × 300 | 16-bit, doubled to 1024 × 600 |
| `hdmi.RGB1024` | 1024 × 600 | 4-bit (16 colours), **native** resolution |
| `hdmi.RGB640_4` | 640 × 480 | 4-bit (16 colours) — the **fast game mode**: the 150 KB framebuffer is half the video SRAM, so `hdmi.create()`'s F buffer takes the other half (SRAM, not PSRAM) — double-buffered composing and `copy("F","N")` never touch the slower PSRAM |

The framebuffer lives in SRAM. In `RGB640` each pixel is one byte (RGB332); in
the two 16-bit modes each pixel is an RGB565 half-word; in `RGB1024` two pixels
share a byte (4-bit palette index). `RGB1024` gives a true, crisp 1024 × 600 —
unlike `RGB512`, which reaches 1024 × 600 by doubling a 512 × 300 image — at the
cost of 16 colours. The on-screen console draws into this same buffer, and the
image decoders (section 10) work in `RGB1024` too — each pixel maps to the nearest
of the 16 palette colours, with optional Floyd-Steinberg / Atkinson dithering for
photos (`draw_jpg`/`draw_bmp` `dither=` argument).

### Changing mode: `screen()` — the easy, persistent way

`screen(mode, clock=252)` is the recommended way to change the display. It stops
and restarts the scanout, saves the choice, and re-establishes the on-screen
console at the new geometry:

```python
screen(hdmi.RGB320)        # 320x240 16-bit, 252 MHz
screen(hdmi.RGB640, 378)   # 640x480 8-bit at 378 MHz
screen(hdmi.RGB512)        # 1024x600 16-bit, doubled (fixed at 252 MHz)
screen(hdmi.RGB1024)        # 1024x600 4-bit, native 16 colours (fixed at 252 MHz)
screen()                   # returns the saved (mode, clock)
```

The optional `clock` is the CPU/pixel clock and applies **only to `RGB640` and
`RGB320`**, which accept one of **252**, **315** or **378** MHz (default 252).
`RGB512` and `RGB1024` are fixed at 252 MHz — passing any other clock for them
raises a `ValueError`. The chosen mode/clock is saved and restored on the next boot
(section 13). If a saved mode does not suit your monitor, the serial console
still works — use `screen(hdmi.RGB640)` to reset it.

> **Monitor re-lock delay.** Changing resolution puts out a new video signal,
> and the monitor takes a couple of seconds to lock onto it and start showing
> pixels again. If a program switches mode and immediately starts drawing, that
> first drawing happens while the monitor is still blank and is missed. In a
> program that changes mode, add `time.sleep(3)` right after the `screen()` /
> `hdmi.init()` call before drawing anything you need to be seen. (At the REPL
> this doesn't matter — you're already several seconds past the switch by the
> time you type the next command.)

### Low-level control: `hdmi.init()` / `hdmi.deinit()`

`screen()` is a wrapper around the `hdmi` module. You can drive it directly for a
**one-off** (non-persisted) change, but you must stop the current scanout before
starting a new mode:

```python
hdmi.deinit()              # stop the scanout (frees core 1, aborts its DMA)
hdmi.init(hdmi.RGB320)     # start a new mode (default 640x480 @ 252)
import pcconsole; pcconsole.console()   # re-attach the on-screen console
```

- `hdmi.init(mode=hdmi.RGB640, clock=252)` — start the scanout. Raises
  `ValueError` on a bad mode or clock. **No-op if already running** — call
  `hdmi.deinit()` first to switch modes.
- `hdmi.deinit()` — stop the scanout: halts core 1, cleanly aborts the DMA and
  stops the HSTX output. After this the screen goes blank until the next
  `hdmi.init()`.

Because a raw `hdmi.init/deinit` does **not** move the console or save the
setting, prefer `screen()` unless you specifically want a temporary change or are
managing the framebuffer yourself.

### Drawing

`hdmi.fb()` returns a **`Display`** object — a `framebuf.FrameBuffer` subclass
with all the usual `framebuf` methods (`pixel`, `hline`, `vline`, `line`, `rect`,
`fill_rect`, `ellipse`, `text`, `blit`, `scroll`, …) plus a colour helper:

```python
d = hdmi.fb()
d.fill(d.colour(0x000080))              # 24-bit RGB -> current format
d.rect(10, 10, 100, 60, d.colour(RED))  # named palette colour
d.text("Hello", 20, 30, d.colour(WHITE))
```

Rebuild the `Display` (call `hdmi.fb()` again) after any mode change, because the
geometry and pixel format change.

**Extra primitives** (beyond `framebuf`'s line/rect/ellipse/poly), on the
`Display`:

| Method | Description |
|---|---|
| `d.line(x1, y1, x2, y2, colour, w)` | a line `w` pixels **thick** (`w=1` is the normal framebuf line) |
| `d.rbox(x, y, w, h, r, colour, fill=None)` | a **rounded rectangle**, corner radius `r`, optional fill |
| `d.arc(x, y, r1, r2, a1, a2, colour)` | a filled **arc / ring segment** between radii `r1`–`r2`, angles `a1`–`a2`° (0°=up, clockwise); `a1==a2` = full ring; thin arc = `r1=r-1, r2=r` |
| `d.bezier(points, colour)` | a **Bézier curve** through a list of `(x, y)` control points (2 or more) |
| `d.flood(x, y, colour, border=None)` | **flood fill**: no `border` = replace the seed pixel's colour (paint bucket); with `border` = fill outward to that boundary colour |
| `hdmi.polyfill(points, colour, pattern=0)` | **polygon fill** on the current write target (MMBasic turtle's scanline fill): `points` is a flat int16 array of x,y pairs (as `framebuf.poly` takes); `pattern` 0 = solid, 1..31 = the MMBasic 8×8 texture set. The turtle's `end_fill()` uses this |

```python
d.line(0, 0, 200, 120, d.colour(CYAN), 5)     # 5px-thick line
d.rbox(20, 20, 120, 70, 12, d.colour(WHITE), d.colour(0x002040))
d.arc(160, 120, 40, 60, 0, 270, d.colour(YELLOW))   # 3/4 ring
d.bezier([(10, 200), (60, 100), (140, 260), (200, 160)], d.colour(GREEN))
d.rect(50, 50, 60, 40, d.colour(RED))          # outline a shape...
d.flood(60, 60, d.colour(RED))                 # ...then fill inside it
```

Colours are native-format, same as the framebuf methods — wrap in `d.colour(...)`.
`flood()` works on the current HDMI write target (i.e. the buffer `hdmi.fb()`
draws to).

**Colours must be converted first.** The `framebuf` drawing methods take a colour
in the framebuffer's *native* pixel format, so always wrap a colour in
`d.colour(...)`: `d.colour(0xRRGGBB)` or `d.colour(r, g, b)` converts a 24-bit
RGB value (including the named palette constants `RED`, `WHITE`, …) to the current
format. Passing a raw 24-bit value straight to `rect`/`text`/etc. gives the wrong
colour. (`d.color(...)` is an accepted US-spelling alias.)

Called the framebuf way — `d.text(s, x, y, c)` — `d.text()` uses framebuf's
built-in **8×8** font at a fixed size. For larger, crisper or differently-shaped
text there are **nine MMBasic bitmap fonts**; pass `font=` (and optionally
`scale=`/`bg=`) to `d.text()`, or call `hdmi.text()` directly:

```python
d = hdmi.fb()
d.text("Hello", 20, 20, d.colour(WHITE), font=3)          # 16x24 font
d.text("BIG", 20, 60, d.colour(YELLOW), font=1, scale=4)  # 4x-scaled 8x12
hdmi.text("100", 20, 120, d.colour(GREEN), -1, 2, 6)      # font 6 = big digits
```

The fonts (1-based numbers, matching MMBasic's font numbers):

| # | Size | Notes |
|---|---|---|
| 1 | 8×12 | the console/default font |
| 2 | 12×20 | |
| 3 | 16×24 | large, good for headings |
| 4 | 10×16 | |
| 5 | 24×32 | very large |
| 6 | 32×50 | **digits `0`–`9` and `:` only** (clocks, scores) |
| 7 | 6×8 | small |
| 8 | 4×6 | tiny |
| 9 | 8×10 | |

`hdmi.fonts()` returns the table as `(number, width, height, first_char, count)`
tuples so code can lay text out. Every font is fixed-width, so a string of `n`
chars in font *f* at `scale` is `n × width × scale` pixels wide. Characters
outside a font's range (e.g. a letter in the digits-only font 6) draw nothing.
The font path (`font=` or `scale>1`) draws to the current HDMI **write target**
and returns the x just past the string; `bg=-1` (default) is transparent.

### `hdmi` module reference

| Member | Description |
|---|---|
| `hdmi.RGB640` / `RGB320` / `RGB512` / `RGB1024` | mode constants (see the table above) |
| `hdmi.init(mode=RGB640, clock=252)` | start the scanout (no-op if running) |
| `hdmi.deinit()` | stop the scanout (call before re-`init()` for a new mode) |
| `hdmi.fb()` | a `Display` (framebuf subclass) over the framebuffer — rebuild after a mode change |
| `hdmi.framebuffer()` | the current write target's raw pixels as a writable `memoryview` (for `framebuf.FrameBuffer` or the image decoders) |
| `hdmi.width()` / `hdmi.height()` | current logical width / height in pixels |
| `hdmi.rgb565()` | `True` if the framebuffer is 16-bit RGB565 (RGB320/RGB512); `False` for RGB332 (RGB640) or RGB1024 |
| `hdmi.bpp()` | bits per framebuffer pixel: `4` (RGB1024), `8` (RGB640) or `16` (RGB320/RGB512) |
| `hdmi.palette([i[, rgb]])` | RGB1024 16-colour palette: no args lists all 16 (RGB888); `palette(i)` reads entry `i` (0–15); `palette(i, 0xRRGGBB)` sets it (takes effect at once). Use `palette()` (below) to also persist. |
| `hdmi.fill(colour)` | fast fill of the whole framebuffer with a **native-format** colour (e.g. `hdmi.fill(hdmi.fb().colour(BLUE))`) |
| `hdmi.scroll(dy, colour=0, y0=0, height=None)` | fast vertical scroll of the pixel band `[y0, y0+height)` (default: whole screen) by `dy` pixels — positive moves content up (blank at the bottom), negative moves it down — filling the exposed edge with `colour` (native format) |
| `hdmi.flood(x, y, colour, border=-1)` | flood fill from `(x,y)` (C); `border<0` = replace the seed colour, else fill to the `border` colour. Prefer the `Display.flood()` wrapper |
| `hdmi.tilemap(map, cols, rows, tileset, tpr, tw, th, vx, vy, sx, sy, vw, vh, skip=-1, dst=None)` | render a tile map viewport (C); prefer the `TileMap` wrapper (section 5) |
| `hdmi.putc(x, y, ch, fg, bg)` | blit one 8×12 console glyph at pixel `x,y` (native-format `fg`/`bg`) |
| `hdmi.text(s, x, y, fg, bg=-1, scale=1, font=1)` | draw a string in font `font` (1–9) at pixel `x,y`; `bg=-1` is transparent, `scale` enlarges each glyph pixel into a `scale`×`scale` block. Returns the x just past the string |
| `hdmi.fonts()` | list the fonts as `(number, width, height, first_char, count)` tuples |
| `hdmi.test()` | draw an 8-bar colour test pattern |
| `hdmi.gen()` | mode-change counter (used internally by the console) |

Note that `hdmi.fill()`, `hdmi.scroll()` and `hdmi.putc()` take colours already
in the framebuffer's native format — convert with `hdmi.fb().colour(...)` first.

The on-screen text console uses an 8×12 font (80 columns × 40 rows at 640×480)
and understands **ANSI colour** escapes — the standard 16-colour SGR codes work
on both the HDMI screen and a serial terminal:

```python
print("\x1b[91mred\x1b[0m and \x1b[1;97;44m white on blue \x1b[0m")
```

(Foreground `30`–`37`/`90`–`97`, background `40`–`47`/`100`–`107`, `1` bold,
`7` reverse, `0` reset.)

### Overlay layer and off-screen buffer

Three drawing targets are available (MMBasic's `FRAMEBUFFER` model):

| Target | What it is |
|---|---|
| `"N"` | the **N**ormal display — always exists |
| `"L"` | the **L**ayer — an overlay merged over the display live, **RGB320 mode only** |
| `"F"` | an off-screen **F**ramebuffer in PSRAM — draw or decode into it invisibly, then copy |

| Command | Description |
|---|---|
| `hdmi.layer(transparent=0x000000)` | enable the layer (RGB320 only). `transparent` is an RGB888 colour; layer pixels of that colour show the display through, anything else overlays it. The layer starts fully transparent |
| `hdmi.create()` | allocate the off-screen F buffer (display-sized; PSRAM, except in `RGB640_4` where it takes the free half of the video SRAM — much faster) |
| `hdmi.write("N"/"L"/"F")` | select where ALL drawing goes — `fb()`, `fill`, `text`, the console and the image loaders. `hdmi.write()` returns the current target |
| `hdmi.copy(src, dst)` | block-copy one whole buffer to another, e.g. `hdmi.copy("F", "N")` |
| `hdmi.blit(x, y, w, h, x1, y1, src=None, dst=None, skip=-1)` | copy the `w`×`h` rectangle at `(x,y)` of `src` to `(x1,y1)` of `dst` — see below |
| `hdmi.close("L"/"F")` | remove the layer / free the F buffer (`hdmi.close()` = both) |

The layer lives in the second half of the video memory (only RGB320 leaves it
free — 2 × 320×240×16-bit exactly fills it), so it costs no extra RAM and the
merge happens **per scanline in hardware-speed C on core 1**: moving a sprite on
the layer never disturbs the artwork underneath. A mode change closes both
targets.

```python
screen(hdmi.RGB320)
d = hdmi.fb()                       # the display
draw_jpg("/sd/background.jpg")      # scenery on the display

hdmi.layer()                        # overlay, black = transparent
hdmi.write("L")                     # draw to the layer now
s = hdmi.fb()                       # a Display over the LAYER
s.text("SCORE 100", 8, 8, s.colour(YELLOW))
s.fill_rect(60, 100, 16, 16, s.colour(RED))   # a "sprite"
s.fill(0)                           # clear the layer -> scenery intact below

hdmi.write("N")                     # back to drawing on the display

hdmi.create()                       # off-screen buffer in PSRAM
hdmi.write("F"); draw_jpg("/sd/next_level.jpg")  # decode invisibly
hdmi.write("N"); hdmi.copy("F", "N")             # flip it onto the screen
```

Notes: while `hdmi.write("L")` (or `"F"`) is selected, **console output also
goes to that target** (as in MMBasic) — switch back to `"N"` (or run with the
console printing little) when overlaying live. Drawing the transparent colour
itself onto the layer erases to see-through; pick a transparent colour your
artwork doesn't use.

### Blitting rectangles — `hdmi.blit()`

`hdmi.blit(x, y, w, h, x1, y1, src=None, dst=None, skip=-1)` copies the
`w`×`h` rectangle at `(x,y)` to `(x1,y1)` — within one buffer or between any
two (MMBasic `BLIT`):

- `src` / `dst` are target letters `"N"`, `"L"`, `"F"`; leave them out (or
  `None`) to use the **current write target** for both.
- `skip` is a **native-format** colour (an `fb.colour(...)` value, like
  `fill`/`putc`) that is *not* copied: source pixels of that colour leave the
  destination untouched — cut-out sprites in one call. Default `-1` copies
  everything.
- Rectangles partly off-screen are clipped automatically; overlapping copies
  within one buffer are safe in any direction (so you can shift a region over
  itself, e.g. for horizontal scrolling).

```python
# stamp a sprite sheet cell from the off-screen buffer, magenta = cut-out
hdmi.blit(64, 0, 16, 16, px, py, "F", "N", d.colour(MAGENTA))

# scroll the top half of the screen 4 px left, within the display
hdmi.blit(4, 0, hdmi.width() - 4, hdmi.height() // 2, 0, 0)

# grab what's under the sprite first, restore it later
hdmi.blit(px, py, 16, 16, 0, 0, "N", "F")     # save patch into F
hdmi.blit(0, 0, 16, 16, px, py, "F", "N")     # put it back
```

Works in every mode (8/16-bit blits use fast row copies; 4-bit RGB1024 and
all skip-colour blits go pixel-by-pixel, fine at sprite sizes).

`src`/`dst` can also be a **`(buffer, w, h)` tuple** — any bytearray in the
mode's pixel format acts as an off-screen surface. That is how the sprite
engine stores its images, and how you can keep any number of small graphics
in RAM: `hdmi.blit(0, 0, 16, 16, x, y, (img, 16, 16), "N", skip)`.

### Frame timing — `hdmi.vsync()`

`hdmi.vsync()` waits for the start of the next **vertical blanking** interval.
Draw right after it returns and the update is never caught mid-frame by the
scanout; calling it once per loop also paces a game to the refresh rate
(60 Hz, or 75 Hz at `clock=315`). USB input, audio and Ctrl-C keep running
while it waits.

### Sprites — `import pcsprite`

A full sprite engine with MMBasic's game semantics — layers, collisions,
walls, scrolling — but driven by Python objects. Sprites are drawn over the
scenery and erased without disturbing it (in RGB320 they composite on the
overlay layer; in other modes the F buffer holds a scenery snapshot — taken
automatically, refresh with `sp.snapshot()` after redrawing the background).

```python
import pcsprite as sp

draw_jpg("/sd/stars.jpg")                       # scenery first
ships = sp.sheet("/sd/ships.png", 16, 16, count=4,
                 transparent=hdmi.fb().colour(MAGENTA))
ship = ships[0].show(150, 100)                  # visible at next update()
rock = sp.grab(0, 0, 20, 20)                    # sprite from screen pixels
wall = sp.wall(0, 220, 320, 20)                 # invisible collision rect

while keydown(1) != keyboard.ESC:
    ship.x += 1                                 # nothing drawn yet...
    for a, b in sp.update(vsync=True):          # ...one pass draws it all
        if b is wall:
            print("hit the floor")
        elif b == "right":
            ship.x = 0                          # wrapped off the right edge
```

| API | Description |
|---|---|
| `sp.Sprite(img, w, h, transparent=None)` | a sprite from a mode-format `bytearray`; `transparent` = native colour that isn't drawn |
| `sp.sheet(path, w, h, count, transparent=None)` | load an image file and cut `count` sprites from it, row-major (uses F as scratch — load sheets **first**) |
| `sp.grab(x, y, w, h, transparent=None)` | a sprite copied from the current write target's pixels |
| `s.show(x, y, layer=1)` / `s.hide()` / `s.visible` | visibility (committed at `update()`) |
| `s.x`, `s.y`, `s.layer` | position (deferred) and collision layer; **layer 0** collides with every layer and scrolls with the scenery |
| `s.top()` / `s.flip("h"/"v"/"hv")` | raise in z-order / mirrored copy |
| `s.frame(index)` | for a sheet-backed sprite (from `Image.sprites()` — section 10): show frame `index` of the sheet, for animation |
| `sp.update(vsync=False)` | commit everything in one pass; returns new collision events `(sprite, other)` where `other` is a Sprite, a Wall or `"left"/"right"/"top"/"bottom"` |
| `sp.wall(x, y, w, h)` | static collision rectangle (not drawn); `w.remove()` deletes |
| `sp.scroll(dx, dy, blank=None)` | scroll the scenery right/up with wraparound (or fill exposed edges with `blank`); layer-0 sprites and walls travel with it |
| `sp.on_collision(cb)` | `cb(events)` called from `update()` on new collisions |
| `sp.snapshot()` / `sp.reset()` | re-capture scenery (F mode) / hide all and forget walls |

Collisions are bounding-box, reported **edge-triggered** (only when contact
begins), and partitioned by layer exactly as MMBasic: sprites collide with
their own layer and with layer 0, plus screen edges and walls. Sprites on the
same layer draw in z-order (`show`/`top` order). A `screen()` mode change
resets the engine.

The engine composites *live* (fast, minimal drawing), which is smooth for
modest numbers of sprites. For flicker-free animation of **many or large**
sprites, double-buffer in your own loop with the buffer primitives (section 5,
"Overlay layer and off-screen buffer"): compose each frame into the off-screen
`F` buffer, then `hdmi.vsync(); hdmi.copy("F", "N")` to flip it onto the screen
in one fast copy. `tests/demo_asteroids.py` shows this with `load_image()`.

### Turtle graphics — `Turtle`

A turtle-graphics engine (MMBasic's `TURTLE`), injected as `Turtle`. A turtle
draws on the HDMI screen from a pen with a position and heading:

```python
t = Turtle()                       # draws on hdmi.fb()
t.reset()                          # clear screen; turtle centred, facing up
for _ in range(36):                # a spirograph-ish star
    t.forward(100)
    t.right(160)
```

Conventions (as MMBasic): coordinates are **screen pixels**, **home** is the
screen centre, and the heading is **0 = up, 90 = right** (clockwise), so
`right()` turns clockwise. Colours are 24-bit RGB — use the palette names
(`RED`, `WHITE`, …) or `0xRRGGBB`; they convert to the current mode's format.

| Method | Description |
|---|---|
| `forward(d)` / `fd` , `back(d)` / `bk` | move (and draw if the pen is down) |
| `right(a=90)` / `rt` , `left(a=90)` / `lt` | turn clockwise / counter-clockwise |
| `goto(x, y)` , `setx(x)` , `sety(y)` | move to an absolute pixel |
| `setheading(a)` / `seth` , `home()` | set heading / return to the centre facing up |
| `penup()` / `pu` , `pendown()` / `pd` | lift / lower the pen |
| `pencolor(rgb)` , `pensize(w)` | pen colour (RGB) and width (1–50) |
| `arc(radius, angle)` | move along a circular arc, turning `angle`° total |
| `bezier(d1,a1, d2,a2, d3,a3)` | Bézier curve; control/end points as (distance, angle) from the turtle |
| `circle(r)` , `dot(size)` , `fcircle(r)` | circle outline / filled dot / filled circle at the turtle |
| `rectangle(w, h)` | rectangle centred on the turtle (filled if a fill colour is set) |
| `wedge(radius, start, end)` | filled pie slice |
| `fillcolor(rgb)` / `nofill()` | set fill colour (enables filling) / disable |
| `fillpattern(n)` / `fp(n)` | texture fill (MMBasic `TURTLE FILL PATTERN`): 0 = solid, 1..31 = the 8×8 pattern set (checkerboards, stripes, crosshatch, weave, dots...). Patterns are screen-anchored so adjacent shapes tile; a pattern's gaps leave the background. No argument reads it back |
| `begin_fill()` / `end_fill()` | record the turtle's path and fill it as a polygon (colour + pattern), then re-stroke the outline in the pen colour if the pen is down — MMBasic's end-fill sequence, keeping the border crisp |
| `stamp(size=12)` | stamp a small triangle showing position + heading |
| `push()` / `pop()` | save / restore position + heading |
| `position()` , `xcor()` , `ycor()` , `heading()` | read the state |

```python
t = Turtle(); t.reset()
t.pencolor(CYAN); t.pensize(2)
t.fillcolor(0x002040)
t.begin_fill()
for _ in range(5):                 # a filled star
    t.forward(90); t.right(144)
t.end_fill()
```

Make a new `Turtle` after a `screen()` mode change (the pixel format differs
per mode). A turtle draws on the current write target, so `hdmi.write("F")` +
a `Turtle()` draws off-screen.

### On-screen GUI — `pcgui`

`pcgui` is a small widget toolkit — MMBasic's GUI controls (Micromite Plus) as
Python objects. A `GUI` manager owns the controls, draws them, and dispatches
input: a **USB mouse** or **touch panel** (section 7/8) for clicking/dragging,
and the **USB keyboard** (section 6) for typing into text boxes. When a mouse
is connected, `start()` shows a visible **mouse pointer** and `poll()` keeps
it tracking the mouse (`pccursor`, section 7; pass `start(cursor=False)` to
opt out). You create controls, then call `poll()` from your loop:

```python
import pcgui
from pcgfx import GREEN, RED, YELLOW

g = pcgui.GUI()                     # draws on hdmi.fb()
g.start()                           # start capturing the keyboard (for text boxes)

g.frame(8, 26, 150, 74, "Pump", font=2)
led = g.led(18, 48, 8, "Run", GREEN)
g.switch(18, 66, 80, 26, "ON|OFF", callback=lambda sw: setattr(led, "value", sw.value))

bar = g.bargauge(255, 44, 20, 70, lo=0, hi=100)
g.slider(168, 110, 130, 18, lo=0, hi=100, callback=lambda s: setattr(bar, "value", s.value))

g.button(8, 208, 90, 26, "QUIT", fg=YELLOW, bg=RED, callback=my_quit)

while running:
    g.poll()                        # read the pointer + keyboard, fire callbacks
```

#### Building an application

A GUI program has four parts: set the screen mode, create the `GUI`, add controls
(each with an optional callback), then loop on `poll()`. `poll()` is
non-blocking, so the same loop can do other work — read sensors, update gauges,
talk to the network. A complete skeleton:

```python
import time
import hdmi
import pcgui
from pcgfx import GREEN, RED, YELLOW

screen(hdmi.RGB320)                 # set the mode first (persisted)
time.sleep(3)                       # let the monitor lock before drawing
console("serial")                   # keep REPL prints off the GUI screen
hdmi.fill(0)                        # clear to black

g = pcgui.GUI()                     # draws on the current screen (hdmi.fb())
g.start()                           # capture the USB keyboard (for text boxes)
done = [False]

def quit_app(b):
    done[0] = True                  # (note: lists have no .__setitem__
                                    #  attribute on MicroPython)

g.caption(120, 4, "Pump Control", fg=YELLOW, font=2)
level = g.gauge(80, 100, 40, lo=0, hi=100, fg=GREEN, font=2)
g.slider(20, 170, 200, 20, lo=0, hi=100,
         callback=lambda s: setattr(level, "value", s.value))
g.button(120, 205, 90, 28, "QUIT", fg=YELLOW, bg=RED, callback=quit_app)

try:
    while not done[0]:
        g.poll()                    # dispatch touches/clicks + typing
        # ... your own periodic work here ...
        time.sleep_ms(10)
finally:
    g.stop()                        # release the keyboard
    console("both")
```

Build the GUI **after** setting the mode, and make a fresh `GUI` if you change
mode later (the pixel format differs). `g.cls()` clears the screen and redraws
every control; `g.redraw()` redraws them without clearing; `g.remove(c)` deletes
one. Route the console to serial while a GUI owns the screen so stray prints
don't land on it.

#### Responding to input

Two mechanisms, usable together:

- **Per-control callbacks** — give a control `callback=fn`, and `fn(control)` is
  called when the user changes it (button click, switch/check-box toggle,
  radio/list select, slider/spinner move, Enter in a box). Read the new state
  from the control passed in: `def on(sw): led.value = sw.value`. This is the
  usual way to react.
- **Global touch hooks** — the equivalent of MMBasic's `GUI INTERRUPT TouchDown,
  TouchUp`. `g.on_touch(down=fn, up=fn, move=fn)` registers callbacks called with
  the screen `(x, y)` on every touch-down, drag and release, **regardless of**
  which control (if any) was hit, and *in addition* to per-control callbacks.
  Handy for custom gestures or a drawing surface (see also the `area` control).

Read or set any control's state through **`.value`** (assigning it redraws):
`led.value = 1`, `if sw.value:`, `n = nb.number`, `sel = lb.text`. Turn a control
off/on with `.disable()` / `.disable(False)`, or hide/show it with `.hide()` /
`.hide(False)`.

The controls (all coordinates in screen pixels; colours are RGB such as the
`pcgfx` palette constants; `font=` selects a bitmap font, default 1):

| Factory | Control |
|---|---|
| `g.caption(x, y, text, fg, bg, font, just)` | a text label; `just` is `"LT"`/`"CT"`/`"RB"`… (left/centre/right + top/middle/bottom) |
| `g.frame(x, y, w, h, title, fg, font)` | a titled box to group controls |
| `g.button(x, y, w, h, text, ..., callback)` | a push button (`callback` on click) |
| `g.switch(x, y, w, h, "ON\|OFF", value, ..., callback)` | a two-state toggle showing the on/off label |
| `g.checkbox(x, y, size, label, value, ..., callback)` | a check box + label |
| `g.radio(x, y, r, label, group, value, ..., callback)` | a radio button; radios sharing a `group` are mutually exclusive |
| `g.led(x, y, r, label, colour, value)` | an indicator lamp (bright when `value`) |
| `g.gauge(x, y, r, value, lo, hi, fg, font)` | a circular gauge (centre `x,y`, radius `r`) |
| `g.bargauge(x, y, w, h, value, lo, hi, fg)` | a bar gauge (horizontal if `w≥h`, else vertical) |
| `g.slider(x, y, w, h, value, lo, hi, ..., callback)` | a draggable slider |
| `g.textbox(x, y, w, h, text, ..., callback)` | an editable text box; tapping it opens an on-screen keyboard |
| `g.numberbox(x, y, w, h, value, ..., callback)` | a number box (tapping opens a numeric keypad; `.number` returns a float) |
| `g.displaybox(x, y, w, h, text, ...)` | a read-only box that shows a value (set `.value`) |
| `g.spinner(x, y, w, h, value, lo, hi, step, ..., callback)` | a number box with up/down arrows; tap them to step by `step` |
| `g.listbox(x, y, w, h, items, selected, ..., callback)` | a scrolling list; tap a row to select, drag to scroll (`.value` = index, `.text` = string) |
| `g.fmtbox(x, y, w, h, value, fmt, ..., callback)` | a number box shown through a format string, e.g. `fmt="%.2f"` (`.number` = float) |
| `g.area(x, y, w, h, callback)` | an invisible touch region; the callback fires on touch/drag with `.value` = `(x, y)` relative to it |

Tapping a **text box** or **number box** pops up an on-screen keyboard (alpha) or
keypad (numeric) docked at the bottom of the screen, so the GUI is fully usable
from a touch panel with no physical keyboard — tap keys, then **OK** commits (and
fires the callback) or **Esc** cancels. A USB keyboard also works while the
pop-up is open (type, Enter/Esc). `callback` fires on commit.

`g.cls()` clears the screen and redraws every control; `g.remove(ctrl)` deletes
one; `g.stop()` releases the keyboard when you're done. Build the GUI **after**
setting the screen mode (make a fresh `GUI` if you change mode — the pixel
format differs). See `tests/test_gui.py` for a full control panel.

> This is a cleaner reimagining of MMBasic's GUI, not a byte-exact clone: the
> control **set and behaviour** match, but drawing uses rounded shapes and the
> bitmap fonts.

### Tile maps — `TileMap`

`TileMap` (MMBasic `TILEMAP`) draws large scrolling backgrounds out of a
**tileset** — one image holding a grid of equal-size tiles — and a grid of tile
indices. The per-tile rendering is done in C (`hdmi.tilemap()`), so even a
full-screen redraw every frame is fast enough to scroll smoothly. Tile index
**0 is empty** (nothing drawn); 1..N pick tiles from the sheet left-to-right,
top-to-bottom.

```python
from pcimage import load_image

sheet = load_image("/sd/tiles.png")          # the tileset, into memory
tm = TileMap(sheet, 16, 16, cols=64, rows=32) # 16x16 tiles; a 64x32 map
tm.set(3, 5, 12)                              # cell (3,5) shows tile 12
tm.view(0, 0)
tm.draw()                                     # render the viewport to the screen
```

The tileset is any in-memory surface: a `load_image()` image (usual), or a
`(buffer, w, h)` tuple you drew yourself. It must be in the **current screen's
pixel format** (load it in the mode you'll draw in). `tiles_per_row` defaults to
`sheet_width // tile_w`. Provide the map up front with `data=` (a list of rows,
or a flat sequence), or build it with `set()`; `TileMap.load("map.csv")` reads a
comma/space map file into rows for `data=`.

| Method | Purpose |
|---|---|
| `tm.set(col, row, tile)` / `tm.get(col, row)` / `tm.fill(tile)` | edit map cells |
| `tm.view(x, y)` / `tm.scroll(dx, dy)` | move the viewport (world pixels) |
| `tm.clamp(vw, vh)` | stop the viewport scrolling past the map edges |
| `tm.draw(sx=0, sy=0, vw=None, vh=None, skip=-1, dst=None)` | render the viewport (default: whole screen) to `dst` (default the current write target); `skip` is a transparent colour |
| `tm.blit_tile(tile, x, y, skip=-1)` | draw one tile at a screen pixel (a player/object without a full sprite) |
| `tm.tile_at(wx, wy)` | the tile index at a world pixel (0 if outside) |
| `tm.set_attr(tile, value)` / `tm.attr(tile)` | tag a tile with an attribute (e.g. a "solid" bitmask) |
| `tm.collide(wx, wy, w, h, mask=None)` | `True` if the world rectangle overlaps a non-empty tile (or `attr & mask` when `mask` is given) — for wall/water collision |

For flicker-free scrolling, compose into the off-screen **F** buffer and flip it
(section 5, "Overlay layer and off-screen buffer"): each frame `hdmi.write("F")`,
`hdmi.fill(0)`, `tm.draw()`, then `hdmi.write("N"); hdmi.vsync(); hdmi.copy("F",
"N")`. Game objects on the map are ordinary **sprites** (`pcsprite`) whose image
is a tile of the same sheet, or a quick `tm.blit_tile()`. See
`tests/test_tilemap.py` for a complete scrolling example (it builds its tileset
in memory, so it needs no asset file).

---

## 6. USB keyboard

A USB keyboard is detected automatically and drives both the on-screen console
and the serial REPL. On connect you will see `USB keyboard -> slot 1`.

### Layouts

```python
keymap("UK")      # set the layout (persisted across reboots)
keymap()          # return the current layout name
keymaps()         # list available layouts
```

Layouts: **US, UK, DE, FR, ES, BE**. The choice is saved and restored at boot.

### Lock LEDs and Num Lock

Caps Lock, Num Lock and Scroll Lock toggle the keyboard's physical LEDs, and the
correct LED state is set when a keyboard is plugged in. Num Lock starts **on**;
turning it off makes the numeric keypad act as a navigation cluster
(arrows/Home/End/PgUp/PgDn/Ins/Del), like a PC.

### Reading keys directly — `keydown()`

Programs (games especially) often need to know which keys are held *right now*
rather than reading typed characters. **`keydown(n)`** reports the live key
state (MMBasic's `KEYDOWN()` function):

| Call | Returns |
|---|---|
| `keydown()` or `keydown(0)` | how many keys are currently held (0–6) |
| `keydown(1)` … `keydown(6)` | code of the nth held key (1 = most recent; 0 = none) |
| `keydown(7)` | modifier bitmap: 1 L-Alt, 2 L-Ctrl, 4 L-GUI, 8 L-Shift, 16 R-Alt, 32 R-Ctrl, 64 R-GUI, 128 R-Shift |
| `keydown(8)` | lock bitmap: 1 Caps, 2 Num, 4 Scroll |

Printing keys report their character code (layout-, Shift- and Caps-aware), so
`keydown(1) == ord("a")` tests the A key. Non-printing keys report the codes in
the `keyboard` module: `keyboard.UP`, `DOWN`, `LEFT`, `RIGHT`, `HOME`, `END`,
`PGUP`, `PGDN`, `INS`, `DEL`, `ENTER`, `ESC`, `TAB`, `BKSP`, `F1`…`F12`.

Note: each `keydown()` call also empties pending console input (as in MMBasic),
so polled keys don't pile up as typed-ahead input at the REPL prompt.

```python
import keyboard, time
while keydown(1) != keyboard.ESC:        # run until Esc is held
    if keydown(1) == ord("a"):           # 'a' key held?
        print("left!")
    time.sleep_ms(20)
```

### Key event callback — `keyboard.on_key()`

`keyboard.on_key(cb)` registers a handler called as `cb(code)` for every
keypress (and auto-repeat) with the same codes `keydown()` reports;
`keyboard.on_key()` (or passing `None`) removes it. The handler runs via the
scheduler (between bytecodes), so it may allocate and print but should return
quickly. The key still goes to the console/REPL input as normal.

---

## 7. USB mouse

A USB mouse is detected automatically (`USB mouse -> slot 2`). It maintains a
virtual cursor position (accumulated from movement, clamped to the screen,
starting centred). Read it with **`mouse(code)`**:

| `mouse(code)` | Returns |
|---|---|
| `"X"` / `"Y"` | cursor position in pixels |
| `"L"` / `"R"` / `"M"` | left / right / middle button (1 = pressed) |
| `"W"` | scroll-wheel accumulator |
| `"B"` | button bitmap (1 = L, 2 = R, 4 = M) |
| `"D"` | left-button double-click (within 500 ms; clears on read) |
| `"T"` | 3 if the mouse has a wheel, else 0 |
| `"PRESENT"` | 1 if a mouse is connected |
| `"SLOT"` | the HID slot (2) |

`mouse_speed(v)` sets movement sensitivity (higher = slower); `mouse_speed()`
returns it. Standard, high-resolution and 16-bit gaming mice are supported.

```python
import time
while True:
    print(mouse("X"), mouse("Y"), mouse("L"))
    time.sleep_ms(100)
```

### The visible pointer — `pccursor`

`mouse("X"/"Y")` tells your *program* where the cursor is; **`pccursor`**
shows the *user* — MMBasic's `GUI CURSOR`, as a module. It is a save-under
sprite: the pixels beneath the pointer are saved before it is drawn and
restored when it moves, so it floats over whatever is on screen without
disturbing it. Shapes are MMBasic's built-in cursors: `pccursor.ARROW`
(hot point at the tip) and `pccursor.CROSS` (hot point at the centre).

| Call | Effect |
|---|---|
| `pccursor.on(shape=ARROW, colour=WHITE)` | show the pointer; call again to change shape/colour |
| `pccursor.refresh()` | follow the mouse — call it from your loop |
| `pccursor.hide()` / `pccursor.show()` | take it off the screen / put it back |
| `pccursor.erase()` | lift it before drawing underneath; the next `refresh()` repaints |
| `pccursor.move(x, y)` | steer it without a mouse (a live mouse overrides on refresh) |
| `pccursor.pos()` | the hot-point position `(x, y)` |
| `pccursor.off()` | remove it and restore the screen |

```python
import time
import pccursor

pccursor.on(pccursor.CROSS, colour=CYAN)
while True:
    pccursor.refresh()                  # erase + repaint if the mouse moved
    time.sleep_ms(10)
```

**`pcgui` does all of this for you** (section 5): `GUI.start()` turns the
pointer on when a mouse is connected, `GUI.poll()` keeps it refreshed, and
control redraws lift it automatically. Pass `start(cursor=False)` to opt out.

---

## 8. USB touch screen

A USB multi-touch panel is detected automatically (`USB touch -> slot 4`). It
begins reporting a couple of seconds after connection. Read it with
**`touch(code)`**. Coordinates are in screen pixels; latched gestures are
one-shot (reading them clears the event).

**Position / contacts**

| `touch(code)` | Returns |
|---|---|
| `"X"` / `"Y"` | first contact, or **-1** when nothing is touching |
| `"DOWN"` / `"UP"` | 1/0 — is the screen being touched |
| `"X2"` / `"Y2"` | second contact, or -1 |
| `"XN", n` / `"YN", n` | nth contact (n = 1…); **n = 0 → number of contacts** |
| `"PRESENT"` / `"SLOT"` | panel connected? / its slot (4) |

**Gestures** (each clears when read)

| `touch(code)` | Returns |
|---|---|
| `"SWIPE"` | 0 none / 1 left / 2 right / 3 up / 4 down |
| `"SWL"` / `"SWR"` / `"SWU"` / `"SWD"` | 1 if that swipe just happened |
| `"TAP"` / `"HOLD"` / `"DTAP"` | tap / long-press / double-tap |
| `"PINCH"` | 0 none / 1 expand / 2 contract |
| `"EXPAND"` / `"CONTRACT"` | 1 if that pinch just happened |
| `"ROTATE"` | 0 none / 1 clockwise / 2 counter-clockwise |
| `"CW"` / `"CCW"` / `"TTAP"` | rotate CW / CCW / two-finger tap |

```python
import time
while True:
    if touch("DOWN"):
        print(touch("X"), touch("Y"))
    s = touch("SWIPE")
    if s:
        print("swipe", s)
    time.sleep_ms(20)
```

---

## 9. Audio

Audio plays through the on-board PCM5102 I2S DAC, in the **background** (the
prompt stays live while a track plays).

| Command | Description |
|---|---|
| `play(path)` | Play a `.wav`, `.mp3`, `.flac` or `.mod` file (dispatched by extension) |
| `play(path, wait=True)` | Play and block until finished |
| `pause()` / `resume()` | Suspend and continue the current playback |
| `stop()` | Stop playback immediately |
| `is_playing()` | True while something is playing |
| `volume(v)` | Set volume 0–100 (perceptual/log taper); `volume()` returns it |
| `beep(freq=880, ms=150)` | Play a short tone |

```python
volume(70)
play("/sd/music/song.flac")
```

WAV, MP3 and FLAC are decoded on the fly. A short sound also plays when a USB
device is plugged in or removed (set `pcaudio.usb_sounds = False` to disable).

### Tracker music + game sound effects — `.mod` files

Amiga **MOD tracker modules** play with the hxcmod engine (as MMBasic's
`PLAY MODFILE`). `play()` returns the song title. `loop=True` repeats the song
forever — ideal for game background music:

```python
play("/sd/game.mod", loop=True)
mod_sample(3)                 # fire one of the song's instrument samples as a
mod_sample(7, effect=2)       # sound effect MIXED OVER the music
```

`mod_sample(sample, effect=1, vol=64, rate=16000)` plays instrument `sample`
(1–32) of the **currently playing** MOD on effect channel 1–4 — so a game can
keep its music running and trigger shots/jumps/pickups from the same file
(MMBasic `PLAY MODSAMPLE`). Raising `rate` pitches the sample up.

### Tones — `tone()`

Two sine-wave channels (left/right), like MMBasic `PLAY TONE`:

```python
tone(440)                     # 440 Hz both channels, until stop()
tone(440, 880)                # different left/right frequencies
tone(262, 262, 500)           # middle C for 500 ms (rounded to whole cycles)
tone(330)                     # retunes LIVE — no click, so melodies work
stop()
```

A timed tone ends at a zero crossing (no click). Calling `tone()` while a tone
is playing retunes it seamlessly; `tone(freq, freq, ms, wait=True)` blocks.

### 4-voice synthesiser — `sound()`

Four independent voices, each with its own waveform per side (MMBasic
`PLAY SOUND`): **S**ine, **Q** (square), **T**riangle, **W** (sawtooth),
**P** (periodic noise), **N** (white noise), **O**ff.

```python
sound(1, "B", "S", 440)        # voice 1: sine 440 Hz on both sides
sound(2, "L", "Q", 110, 15)    # voice 2: square 110 Hz, left only, volume 15
sound(3, "R", "N", 1000)       # voice 3: white noise on the right
sound(2, "L", "O", 1)          # switch one voice off
stop()                         # silence (stops the whole synth)
```

`sound(voice, side, wave, freq=10, vol=25)`: voice 1–4; side `"L"`, `"R"` or
`"B"`; freq 1 Hz–20 kHz; vol 0–25 per voice (25 = max — four voices at full
volume fill the output range). Volume changes ramp over a few ms to avoid
clicks. Voices can be changed live while the synth runs — arpeggios, sirens
and game effects are all a loop of `sound()` calls.

---

## 10. Images

Load pictures onto the HDMI framebuffer, or save the screen to a file.

| Command | Description |
|---|---|
| `draw_jpg(path, x=0, y=0, scale=1, dither=False)` | Draw a JPEG (scale 1/2/4/8 downscales) |
| `draw_bmp(path, x=0, y=0, dither=False)` | Draw a BMP (all common variants) |
| `draw_png(path, x=0, y=0, cutoff=20)` | Draw a PNG (alpha ≤ cutoff = transparent) |
| `save_image(path)` | Save the framebuffer to a 24-bit BMP file |

```python
draw_jpg("/sd/photo.jpg")
save_image("/sd/screen.bmp")
```

A photo drawn plainly in a reduced-colour mode posterises. `draw_jpg` and
`draw_bmp` take a `dither` argument that error-diffuses the image, which helps a
lot in **RGB1024** (16 colours) and **RGB640** (256 colours). It has no effect in
the RGB565 modes (RGB320/RGB512) or on `draw_png` (nearest-colour only).

- `dither=True` — **the easy choice: Atkinson dithering.** Clean flat areas, good
  contrast; the best general pick, and especially so for RGB1024.
- `dither=1` — Floyd-Steinberg. Good on RGB640 (256 colours), but on RGB1024 it
  propagates the full error and — because the RGB121 format has only 2 red / 2 blue / 4 green
  levels — tends to speckle skies/shadows, often looking *worse* than no dithering.
- `dither=2` — Atkinson explicitly (same as `True`). `dither=False`/`0` — off.

```python
screen(hdmi.RGB1024)
draw_jpg("/sd/photo.jpg", dither=True)   # Atkinson — best on 16 colours
```

### Loading images into memory (sprite sheets) — `load_image()`

`draw_*` decode straight onto the screen. **`load_image(path)`** instead decodes
into a **memory buffer** (an `Image`) in the current pixel format, which you can
then blit whole or in pieces — the standard way to hold a **sprite sheet** and
blit individual sprites out of it.

```python
d = hdmi.fb()
sheet = load_image("/sd/enemies.png", transparent=d.colour(MAGENTA))
sheet.cell(2, 0, 16, 16, px, py, skip=d.colour(MAGENTA))  # blit cell (col 2,row 0)
sheet.blit(0, 0)                                          # blit the whole image
```

| `Image` member | Description |
|---|---|
| `img.w`, `img.h` | image size in pixels |
| `img.blit(x, y, sx=0, sy=0, w=None, h=None, dst=None, skip=-1)` | blit a region (default the whole image) to `(x,y)` |
| `img.cell(col, row, cw, ch, x, y, dst=None, skip=-1)` | blit grid cell `(col,row)` of a `cw×ch` sheet |
| `img.surface` | the `(buffer, w, h)` tuple, to pass straight to `hdmi.blit()` |
| `img.sprites(cw, ch, count=None, transparent=None)` | cut into `pcsprite` **Sprites** that share this buffer (no copy) |

`load_image(path, transparent=None, dither=False, cutoff=20, scale=1)`:
`transparent` (a native colour) pre-fills the buffer, so a PNG's transparent
areas become that colour — ready to use as the blit **skip** colour. `dither`
(jpg/bmp) and `cutoff` (png) match `draw_*`; `scale` downsamples a JPEG.

For the sprite engine, `img.sprites()` gives ready-made sprites backed by the
one sheet in memory:

```python
ships = load_image("/sd/ships.png", transparent=d.colour(MAGENTA)).sprites(
    16, 16, count=4, transparent=d.colour(MAGENTA))
ships[0].show(150, 100)          # then move + sp.update() as usual (section 5)
```

> The buffer is in the **current mode's format** (RGB565 / RGB332 / RGB121), so
> reload after a `screen()` mode change. In RGB1024 (4-bit) cell widths and x
> offsets should be even.

---

## 11. Real-time clock (DS3231)

The battery-backed DS3231 keeps time across power cycles. At boot the system
clock is synchronised from it automatically.

| Command | Description |
|---|---|
| `settime(year, month, day, hour, minute, second)` | Set the DS3231 (and system clock) |
| `settime()` | Set the DS3231 from the current system clock |
| `gettime()` | Read the DS3231, returns a `time.localtime`-style tuple |
| `synctime()` | Set the system clock from the DS3231 |

```python
settime(2026, 7, 4, 14, 30, 0)
print(gettime())
```

### The hardware alarm (INT on GP32)

The DS3231 has a daily alarm of its own, independent of any running
program: at the set time it raises a flag and pulls its **INT pin — wired
to GP32** — low until acknowledged. The `ds3231` module drives it:

| Command | Description |
|---|---|
| `ds3231.set_alarm(hour, minute, second=0)` | Arm the daily alarm (fires at that time **every day**) |
| `ds3231.alarm_fired()` | Has it gone off since the last clear? |
| `ds3231.clear_alarm()` | Acknowledge: clear the flag, release INT |
| `ds3231.alarm_off()` | Disarm entirely |
| `ds3231.alarm_pin()` | GP32 as a `Pin` (input, pull-up enabled — INT is open-drain, active low: reads 0 while asserted) |

```python
import ds3231
ds3231.set_alarm(7, 0)                    # 07:00 daily
# poll it...
if ds3231.alarm_fired():
    play_tune(); ds3231.clear_alarm()
# ...or take an interrupt (section 14):
ds3231.alarm_pin().irq(lambda p: schedule_wake(), machine.Pin.IRQ_FALLING)
```

Because the alarm lives in the battery-backed chip, it survives resets and
program restarts — whatever is polling (or waiting on the pin) when the
time comes gets the flag.

### Setting the clock from the internet (NTP)

If the board has Wi-Fi in range, it can set its clock from an internet time
server and keep the DS3231 updated — no manual `settime()` needed.

```python
wifi("MySSID", "MyPassword")   # connect + remember the credentials
tz(1)                          # timezone offset from UTC, in hours (e.g. +1)
ntpsync()                      # fetch the time, apply tz, set the RTC + clock
auto(True)                     # optional: sync automatically at every boot
```

- `wifi("SSID", "pw")` connects and saves the credentials; later `wifi()`
  reconnects using the saved ones.
- `tz(hours)` sets the timezone offset (may be fractional, e.g. `5.5`); NTP time
  is UTC and this makes the clock show local time. `tz()` returns the setting.
- `ntpsync()` connects (if needed), reads the time, applies `tz`, and writes
  **local** time to both the system clock and the battery-backed DS3231 — so the
  time stays correct even offline afterwards.
- `auto(True)` runs `ntpsync()` at boot (adds a few seconds while it connects;
  falls back silently to the DS3231 if Wi-Fi/NTP is unavailable). `auto(False)`
  turns it off.

> **Security note:** the Wi-Fi SSID and password are stored in **plaintext** in
> `/settings.json` on the flash filesystem — this board has no secure storage,
> so anyone with the board or a firmware/SD image can read them. If that matters,
> don't save credentials: call `wifi("SSID", "pw")` and `ntpsync()` each session
> and leave `auto` off. See also section 16 for general Wi-Fi use.

---

## 12. SD card

A FAT-formatted SD card is mounted at **`/sd`**. Cards are **hot-swappable**: a
background check (~twice a second) notices when a card is removed and unmounts it
(`Warning: SDcard removed`); inserting a card mounts it again
(`SDcard inserted`). Use the normal file API and the shell commands on `/sd`.

```python
ls("/sd")
f = open("/sd/data.txt", "w"); f.write("hello"); f.close()
```

---

## 13. File transfer (XMODEM)

Transfer files to and from the board over the **serial console** using the XMODEM
protocol, so you can move programs on and off without an SD card reader. Any
terminal with XMODEM support works (e.g. TeraTerm: **File → Transfer → XMODEM**).

```python
xrecv("/sd/prog.py")     # receive a file INTO the board, then start an XMODEM *Send* in the terminal
xsend("/sd/prog.py")     # send a file FROM the board, then start an XMODEM *Receive* in the terminal
```

- `xrecv(path)` opens `path` for writing, sends `NAK`, and waits (up to ~60 s) for
  the terminal to start sending. Start the XMODEM **Send** in your terminal.
- `xsend(path)` waits for the terminal to start receiving, then transmits the
  file. Start the XMODEM **Receive** in your terminal.
- Works to both `/sd` and the internal flash filesystem (any path).
- While a transfer runs, the serial console is dedicated to the protocol (the
  REPL is paused and nothing is echoed to the HDMI screen); it returns to normal
  when the transfer finishes.
- 128-byte packets; the receiver uses the additive checksum, the sender accepts
  either checksum (`NAK`) or CRC-16 (`C`). Trailing packet padding is trimmed from
  received files, so a transferred `.py` runs as-is.
- On failure it raises `OSError` with a message (`Remote did not respond`,
  `Too many errors`, `Cancelled by remote`, …).

The full names are `xmodem.recv(path)` / `xmodem.send(path)` (`xrecv`/`xsend` are
just convenience aliases injected into the REPL).

---

## 14. Timers, pin interrupts and background events

MicroPython has the same "software interrupt" model as MMBasic: a peripheral or
timer flags an event, and your Python handler runs **between statements** of
whatever the main program is doing (including during `time.sleep()`). Handlers
are ordinary Python functions — no special return statement is needed. If you
are coming from MMBasic, this table is the translation:

| MMBasic | MicroPython equivalent |
|---|---|
| `SETTICK period, sub` | `machine.Timer(period=..., callback=f)` |
| `SETTICK 0` (cancel) | `timer.deinit()` |
| `SETPIN n, INTH/INTL/INTB, sub` | `Pin(n).irq(f, Pin.IRQ_RISING / IRQ_FALLING)` |
| `ON KEY sub` | `keyboard.on_key(f)` (see section 6) |
| `WATCHDOG timeout` | `machine.WDT(timeout=ms)` + `wdt.feed()` |
| COM-port RX interrupt | `machine.UART.irq(f, UART.IRQ_RXIDLE)` |
| `PAUSE` (interrupts still fire) | `time.sleep()` (callbacks still fire) |

### Periodic ticks — `machine.Timer` (SETTICK)

```python
from machine import Timer

def tick(t):                 # the timer object is passed to the callback
    print("tick")

t1 = Timer(period=500, callback=tick)                 # every 500 ms
t2 = Timer(period=2000, mode=Timer.ONE_SHOT,
           callback=lambda t: print("once, 2 s later"))
t1.deinit()                  # cancel (= SETTICK 0)
```

You can run many timers at once (MMBasic allows 4; here ~16). `freq=10` may be
used instead of `period=100`. Callbacks keep firing while the main program
computes, sleeps, or sits at the REPL.

### Pin-change interrupts — `Pin.irq` (SETPIN INTH/INTL/INTB)

```python
from machine import Pin

def button(p):               # the Pin object is passed to the handler
    print("pressed", p)

sw = Pin(2, Pin.IN, Pin.PULL_UP)
sw.irq(button, Pin.IRQ_FALLING)                  # INTL: high -> low
sw.irq(button, Pin.IRQ_RISING)                   # INTH: low -> high
sw.irq(button, Pin.IRQ_RISING | Pin.IRQ_FALLING) # INTB: both edges
sw.irq(None)                                     # remove (= SETPIN n, OFF)
```

By default the handler is *scheduled* like everything else on this page. For
microsecond-latency work add `hard=True` — the handler then runs in the real
hardware interrupt and must not allocate memory (see the MicroPython docs on
ISR rules).

### Serial-port receive — `UART.irq`

```python
from machine import UART
u = UART(0, 115200, tx=machine.Pin(0), rx=machine.Pin(1))
u.irq(lambda uart: print("rx:", uart.read()), UART.IRQ_RXIDLE)
```

`IRQ_RXIDLE` fires shortly after a burst of incoming data stops arriving.

### Watchdog — `machine.WDT`

```python
wdt = machine.WDT(timeout=5000)   # reboot if not fed for 5 s
wdt.feed()                        # call regularly from the main loop
```

Note: once started, a watchdog cannot be stopped — a Ctrl-C back to the REPL
will reboot the board 5 s later unless you keep feeding it.

### Other event callbacks on this board

- `keyboard.on_key(f)` — every keypress/auto-repeat, `f(code)` (section 6).
- `keyboard.on_usb_event(f)` — USB device plugged/unplugged, `f(True/False)`
  (this drives the plug-in sound; replacing it replaces the sound).
- `machine.RTC` has no alarm on this chip — use a `Timer` for scheduled work.

### Rules of thumb for handlers

- Keep them short; they run to completion and delay each other (and the REPL)
  while running. Set a flag or store a value, act on it in the main loop.
- Scheduled events queue up to 8 deep; a burst beyond that is dropped. For
  high-rate sources poll the live state instead — `keydown()`, `touch("X")`,
  `mouse("X")` are the lossless pattern for games.
- An uncaught exception inside a handler is printed and that callback may stop
  firing — wrap risky code in `try/except`.
- For bigger programs, `asyncio` (next subsection) is often a cleaner way to
  structure many concurrent activities than nested callbacks.

### Concurrency — `asyncio` (and a note on threads)

The **`_thread` module is not available on this board**. On the rp2 port a
thread always runs on the second CPU core, and core 1 here is dedicated to
generating the HDMI picture — the two cannot coexist. The supported way to run
several activities "at the same time" is **`asyncio`** (frozen into this
firmware): cooperative tasks that all run on core 0, switching wherever a task
`await`s.

```python
import asyncio, keyboard

async def ticker():                    # background task: once a second
    n = 0
    while True:
        print("tick", n)
        n += 1
        await asyncio.sleep(1)

async def watch_keys():                # foreground task: poll the keyboard
    while keydown(1) != keyboard.ESC:
        await asyncio.sleep_ms(20)

async def main():
    t = asyncio.create_task(ticker())  # start the background task
    await watch_keys()                 # run until Esc is held
    t.cancel()

asyncio.run(main())
```

Things to know:

- Scheduling is **cooperative**: a task runs until it `await`s. A blocking call
  (`time.sleep()`, a long computation, a blocking `read()`) stalls *every*
  task — use `await asyncio.sleep_ms(...)` and the asyncio stream APIs instead.
- Because only one task runs at a time there are no data races — tasks can
  share variables freely, no locks needed.
- The timers, pin interrupts and callbacks from earlier in this section keep
  firing while asyncio runs; they are independent mechanisms and combine well
  (e.g. a `Pin.irq` handler sets an `asyncio.ThreadSafeFlag` a task waits on).
- There is no way to run CPU-heavy Python "in the background" — all Python
  shares core 0, so a long computation pauses everything else regardless of
  how it is structured.
- Full API: https://docs.micropython.org/en/latest/library/asyncio.html

---

## 15. Persistent settings

The keyboard layout, the HDMI mode/clock, and the RGB1024 palette are saved in
`/settings.json` on the flash filesystem and restored at boot. `keymap("UK")`,
`screen(...)` and `palette(i, 0xRRGGBB)` update and persist automatically. Delete
`/settings.json` (`rm("/settings.json")`) to return to the defaults (US keyboard,
640×480 @ 252 MHz, and the default 16-colour palette).

---

## 16. Networking (Wi-Fi / Bluetooth)

Wi-Fi and Bluetooth use the on-board CYW43 chip and the standard MicroPython
APIs — see the MicroPython docs for full details.

```python
import network
wlan = network.WLAN(network.STA_IF)
wlan.active(True)
wlan.connect("ssid", "password")
print(wlan.isconnected(), wlan.ifconfig())
```

Bluetooth is available via the `bluetooth` module and the frozen `aioble`
helper. Networking helpers `mip`, `requests` and `ntptime` (from
bundle-networking) are included.

### HTTPS

TLS is built in (mbedTLS, TLS 1.2, client side). The `requests` module makes
HTTPS requests directly:

```python
import requests
r = requests.get("https://api.github.com")
print(r.status_code)
print(r.json())
r.close()
```

By default the server certificate is **not** verified; for verified HTTPS, pass
your CA certificate(s) to the underlying `ssl` context. See the `ssl` module docs.

### MQTT

The **`umqtt.simple`** and **`umqtt.robust`** MQTT clients are frozen in for
publish/subscribe messaging (over plain TCP or TLS). `umqtt.robust` adds
automatic reconnection.

```python
from umqtt.simple import MQTTClient

c = MQTTClient("picocomputer3", "broker.example.com")
c.connect()
c.publish(b"pc3/status", b"hello")

def on_msg(topic, msg):
    print(topic, msg)

c.set_callback(on_msg)
c.subscribe(b"pc3/cmd")
while True:
    c.check_msg()      # or c.wait_msg() to block
```

(Connect a WLAN first, as in the example above.)

---

## 17. Standard MicroPython modules in this build

All the usual MicroPython modules are present. The definitive list on your board
is `help('modules')`. Notable ones:

- **Core language / library:** `sys`, `gc`, `micropython`, `builtins`, `array`,
  `binascii`, `collections`, `errno`, `hashlib`, `heapq`, `io`, `json`, `math`,
  `cmath`, `os`, `platform`, `random`, `re`, `select`, `struct`, `time`,
  `deflate`, `asyncio`.
- **Hardware / board:** `machine`, `rp2`, `framebuf`, `vfs`.
- **Networking:** `network`, `socket`, `ssl`, `bluetooth`, plus `aioble`, `mip`,
  `requests`, `ntptime`, `umqtt.simple`, `umqtt.robust`.
- **Numerics:** `ulab` (a NumPy/SciPy-compatible array library — `ulab.numpy`,
  `ulab.scipy`).

For the API of every standard module, refer to the MicroPython documentation:
**https://docs.micropython.org/en/latest/library/index.html**

### 3D engine — `draw3d`

MMBasic's `DRAW3D` (the PicoMite 3D engine), as a module. Objects are
polyhedra drawn on the current HDMI write target: backface-culled by surface
normals, painter-depth-sorted, edges and fills in RGB888 colours (converted
per screen mode). See `demos/football.py` — a tumbling truncated icosahedron.

| Call | MMBasic | Notes |
|---|---|---|
| `draw3d.camera(c, viewplane, x=0, y=0, panx=0, pany=0)` | `DRAW3D CAMERA` | cameras 1..3 |
| `draw3d.create(n, nv, nf, cam, vertices, fc, faces, colours, edge=None, fill=None)` | `DRAW3D CREATE` | `vertices` = flat x,y,z per vertex; `faces` = flat vertex-index list in `fc` order; `edge`/`fill` = per-face **index** into `colours` (omit `fill` for wireframe) |
| `draw3d.show(n, x, y, z, nonormals=0, depthmode=0)` | `DRAW3D SHOW` | erases the previous position first; `depthmode` 1 sorts by deepest vertex |
| `draw3d.write(n, x, y, z, ...)` | `DRAW3D WRITE` | as `show` without the erase |
| `draw3d.rotate(q, n, ...)` | `DRAW3D ROTATE` | `q` = `(w, x, y, z, m)`; rotates from the *original* orientation |
| `draw3d.reset(n, ...)` | `DRAW3D RESET` | rotated becomes the new original (cumulative spins) |
| `draw3d.hide(n, ...)` / `hide_all()` / `restore(n, ...)` | `HIDE` / `RESTORE` | erase / redraw as last shown |
| `draw3d.close(n, ...)` / `close_all()` | `CLOSE` | free the object(s) |
| `draw3d.set_flags(n, flag, face, nbr)` | `SET FLAGS` | per-face: 1 hide, 2 red, 4 invert normal, 8 lighting |
| `draw3d.light(n, x, y, z, ambient)` | `DRAW3D LIGHT` | with flag 8; ambient 0..100 |
| `draw3d.q_create(theta, x, y, z)` | `MATH Q_CREATE` | rotation quaternion tuple; theta in radians |
| `draw3d.query(n, "xmin"/"ymax"/"x"/"z"/"distance"/...)` | `DRAW3D()` | last-drawn bounding box / position / mean camera distance |

Not yet ported: `depthmode=2` (the z-buffer hidden-line mode).

### Maths — `ulab` + `pcmath`

Heavy numerical work is covered by **`ulab`**, a NumPy/SciPy-compatible array
library built into this firmware. `import ulab.numpy as np` gives you `ndarray`,
element-wise math, **statistics** (`sum/mean/std/median/min/max/sort`), **linear
algebra** (`np.linalg.inv/det/eig/cholesky/qr/norm`, `np.dot`, `.T`), the **FFT**
(`np.fft.fft/ifft`), interpolation (`np.interp`), `np.cross`, and **complex**
arrays — plus `ulab.scipy` (optimise/signal/special) and the core `complex` +
`cmath` types for scalar complex math. (Arrays are up to 2-D; FFT lengths must be
a power of two.) See the ulab docs: https://micropython-ulab.readthedocs.io/.

**`pcmath`** (`import pcmath`) adds the handful of MMBasic `MATH` verbs that ulab
doesn't have, on top of it:

| Area | API |
|---|---|
| Quaternions | `pcmath.Quat(w,x,y,z)`, `.from_axis(axis,angle)`, `.from_euler(r,p,y)`, `q1*q2`, `.rotate(v)`, `.inverse()`, `.to_euler()`, `.to_matrix()` |
| 3-D vectors | `vcross`, `vdot`, `vmag`, `vunit`, `vrotate(v, axis, angle)` |
| DSP | `window(n, kind)` (hann/hamming/blackman/bartlett/rect), `sinc(x)`, `crossings(a, level)`, `power_spectrum(a)` |
| Statistics | `correl(a, b)` (Pearson r), `chi_square(obs, exp)` → `(chi2, p)` |
| Control | `PID(kp, ki, kd, setpoint, out_min, out_max)` → `.update(measured, dt)` |
| Sensor fusion | `AHRS()` → `.madgwick(ax,ay,az, gx,gy,gz, mx=None,my=None,mz=None, beta=0.5, dt=None)` / `.mahony(..., kp=10, ki=0, dt=None)` → `(roll, pitch, yaw)` in radians (MMBasic `MATH SENSORFUSION`, ported verbatim). Gyro in rad/s; magnetometer optional (omitted = 6-axis IMU); `dt=None` times itself between calls (capped 1 s); `.reset()` re-levels. One `AHRS()` instance per IMU |

```python
import math, pcmath
q = pcmath.Quat.from_euler(0, 0, math.radians(90))
print(q.rotate((1, 0, 0)))                 # ~ (0, 1, 0)
pid = pcmath.PID(2.0, 0.5, 0.1, setpoint=100, out_min=0, out_max=255)
drive = pid.update(temperature, dt)
```

### Plotting — `plot()`

`plot()` (injected) draws data or a function on the HDMI screen, autoscaled with
axes and range labels — handy for maths/science:

```python
plot([1, 4, 9, 16, 25])                 # a list of values
plot(math.sin, (0, 2 * math.pi))        # a function over a range (a, b[, n])
plot([series_a, series_b])              # several series (different colours)
plot(readings, style="bar")             # "line" (default), "scatter" or "bar"
```

Options: `x=` (x values, or the `(a, b[, n])` range for a function), `style=`,
`colour=` (one RGB or a list per series), `box=(x, y, w, h)` (plot area),
`clear=False` (draw over what's there), `axes=False`. It draws on `hdmi.fb()`.

### Game-loop timing — `pcgame.Clock`

For smooth, steady animation and games, `pcgame.Clock` gives a **drift-free**
fixed frame rate (MMBasic's `SYNC`): `tick()` waits until the next frame is due
and returns the elapsed time so movement can be frame-rate-independent.

```python
import pcgame
clock = pcgame.Clock(60)                # target 60 fps
while playing:
    dt = clock.tick()                   # wait for the frame; dt = seconds elapsed
    x += speed * dt                     # move by dt, so speed is per-second
    draw()
    # clock.fps is the measured rate
```

Because it waits until an absolute deadline (advanced by exactly one period each
tick), a slow frame doesn't accumulate drift — the cadence stays anchored to the
clock. Pass `Clock(vsync=True)` to lock to the HDMI refresh instead (tear-free,
at the display's own rate); combine with the F-buffer flip (section 5) for
flicker-free scrolling. `clock.reset()` re-anchors after a pause.

### Board-specific extension modules

These low-level C modules back the friendly commands above and can also be used
directly: `hdmi`, `keyboard`, `mouse`, `touch`, `audio`, `jpeg`, `bmp`, `png`,
`xmodem`.
Most users will prefer the auto-imported helpers (`play`, `draw_jpg`, `touch`,
`mouse`, `screen`, …) rather than these directly.

The **`pcsprite`** module (the sprite engine — `import pcsprite`, section 5) is a
frozen Python module built on `hdmi`; `pcconfig`, `pcconsole` and `pcaudio` are
the frozen Python modules behind `screen`/`console`/`play` and friends.

---

## Quick reference

```python
# Display
screen(hdmi.RGB320)             # change mode (persisted)
screen(hdmi.RGB1024)             # native 1024x600, 16 colours
palette(6, 0x00FF88)            # redefine an RGB1024 colour (persisted)
d = hdmi.fb(); d.text("hi", 0, 0, d.colour(WHITE))
hdmi.text("BIG", 0, 20, d.colour(RED), -1, 4)   # scaled 8x12 text
hdmi.layer(); hdmi.write("L")   # overlay layer (RGB320): sprites over scenery
hdmi.create(); hdmi.copy("F", "N")   # off-screen buffer -> screen (see section 5)
hdmi.blit(0, 0, 16, 16, x, y, "F", "N", d.colour(MAGENTA))  # sprite w/ cut-out
hdmi.vsync()                    # wait for vertical blank (tear-free / pacing)
console("none")                 # no console output/cursor during full-screen graphics
console("serial")               # keep prints off the screen while testing graphics

# Sprites (see section 5)
import pcsprite as sp
ship = sp.grab(0, 0, 16, 16, d.colour(MAGENTA)); ship.show(100, 80)
ship.x += 2
for a, b in sp.update(vsync=True): print("hit", b)   # move + collisions

# Input
touch("DOWN"); touch("X"); touch("SWIPE")
mouse("X"); mouse("L")
keydown(1)                      # code of the key held right now (0 = none)
keymap("UK")

# Timers / interrupts
t = machine.Timer(period=500, callback=lambda t: print("tick"))  # SETTICK
t.deinit()                      # cancel
Pin(2, Pin.IN, Pin.PULL_UP).irq(lambda p: print("edge"), Pin.IRQ_FALLING)

# Audio
volume(70); play("/sd/song.mp3"); pause(); resume(); stop()
play("/sd/game.mod", loop=True); mod_sample(3)   # tracker music + effects
tone(440, 880, 500)                              # sine tones (L, R, ms)
sound(1, "B", "Q", 110)                          # 4-voice synth (see section 9)

# Files / console
cls()                                      # clear the console screen
ls("/sd/*.jpg"); run("/sd/app.py"); edit("/sd/app.py")
xrecv("/sd/app.py"); xsend("/sd/app.py")   # XMODEM over the serial console

# Images / clock
draw_jpg("/sd/pic.jpg"); save_image("/sd/screen.bmp")
settime(2026, 7, 4, 14, 30, 0); print(gettime())
```

*Pico Computer 3 firmware v0.8 — based on MicroPython. See
https://docs.micropython.org/ for the Python language and standard library.*
