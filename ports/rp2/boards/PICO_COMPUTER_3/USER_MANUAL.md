# Pico Computer 3 — MicroPython User Manual

**Firmware:** MicroPython (RP2350B port) for the Pico Computer 3 — version **0.3** (test release).
The REPL banner reports the version:

```
MicroPython v1.29.0-preview on PICO COMPUTER 3 v0.3 with RP2350B
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
  drives it. This starts automatically at boot. `console(False)` detaches it,
  `console()` re-attaches it.

The USB device port is disabled (there is no USB-CDC prompt); the USB port is a
**host** port for keyboards, mice and touch panels.

At the prompt you can type Python directly, or use the shell-style helpers
(section 4). To run a program from the SD card or flash:

```python
run("/sd/myprog.py")
```

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
`hdmi`, `Display`, and the named colour palette (`RED`, `GREEN`, `BLUE`, `WHITE`,
`BLACK`, `YELLOW`, `CYAN`, `MAGENTA`, …).

**Shell commands:** `ls`, `run`, `edit`, `pwd`, `cd`, `mkdir`, `rmdir`, `rm`,
`cat`, `cp`, `mv`, `console`.

**Display / settings:** `screen`, `keymap`, `keymaps`.

**Audio:** `play`, `volume`, `beep`, `stop`, `is_playing`.

**Clock:** `settime`, `gettime`, `synctime`.

**Images:** `draw_jpg`, `draw_bmp`, `draw_png`, `save_image`.

**Input devices:** `touch`, `mouse`, `mouse_speed`, `keydown`.

**File transfer:** `xrecv`, `xsend` (XMODEM over the serial console).

Each is described in the sections below.

---

## 4. Shell commands

Unix-like helpers for working with the filesystem from the prompt. Paths may be
on the flash (`/…`) or the SD card (`/sd/…`).

| Command | Description |
|---|---|
| `ls([path])` | List a directory. Accepts glob patterns: `ls("/sd/*.mp3")` |
| `cd(path)` / `pwd()` | Change / show the working directory |
| `cat(path)` | Print a text file, a page at a time (any key = next page, `q` = stop; `cat(path, False)` dumps it all) |
| `cp(src, dst)` / `mv(src, dst)` | Copy / move (rename) a file. A wildcard in `src` acts on every match, e.g. `cp("*.py", "/sd")` (`dst` must be a directory) |
| `rm(path)` | Delete a file. Accepts a wildcard: `rm("*.tmp")` |
| `mkdir(path)` / `rmdir(path)` | Create / remove a directory |
| `run(path)` | Run a `.py` program (fresh namespace, inherits the REPL helpers) |
| `edit(path)` | Open the full-screen **pye** editor (see below) |

### Editing files with `pye`

`edit("/sd/prog.py")` opens the built-in full-screen editor **pye** (robert-hh's
MicroPython-Editor). If the file does not exist it starts an empty buffer and
creates the file on the first save; `edit()` with no argument opens a blank
buffer you can save to a name of your choice.

It works both on the HDMI screen with a USB keyboard and over a serial terminal.
Use the arrow / `Home` / `End` / `PgUp` / `PgDn` keys where you have them; the
`Ctrl-` shortcuts below do the same job on a plain terminal.

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

**Colours must be converted first.** The `framebuf` drawing methods take a colour
in the framebuffer's *native* pixel format, so always wrap a colour in
`d.colour(...)`: `d.colour(0xRRGGBB)` or `d.colour(r, g, b)` converts a 24-bit
RGB value (including the named palette constants `RED`, `WHITE`, …) to the current
format. Passing a raw 24-bit value straight to `rect`/`text`/etc. gives the wrong
colour. (`d.color(...)` is an accepted US-spelling alias.)

`d.text()` uses framebuf's built-in **8×8** font at a fixed size. For larger or
crisper text, use **`hdmi.text(s, x, y, fg, bg=-1, scale=1)`**, which draws the
**8×12** console font and can scale it up (`scale=2` doubles it, etc.). Its
`fg`/`bg` are already-converted native-format colours, and `bg=-1` draws with a
transparent background:

```python
d = hdmi.fb()
hdmi.text("BIG", 20, 20, d.colour(YELLOW), -1, 4)   # 4x-scaled 8x12 text
```

### `hdmi` module reference

| Member | Description |
|---|---|
| `hdmi.RGB640` / `RGB320` / `RGB512` / `RGB1024` | mode constants (see the table above) |
| `hdmi.init(mode=RGB640, clock=252)` | start the scanout (no-op if running) |
| `hdmi.deinit()` | stop the scanout (call before re-`init()` for a new mode) |
| `hdmi.fb()` | a `Display` (framebuf subclass) over the framebuffer — rebuild after a mode change |
| `hdmi.framebuffer()` | the raw framebuffer as a writable `bytearray` (for `framebuf.FrameBuffer` or the image decoders) |
| `hdmi.width()` / `hdmi.height()` | current logical width / height in pixels |
| `hdmi.rgb565()` | `True` if the framebuffer is 16-bit RGB565 (RGB320/RGB512); `False` for RGB332 (RGB640) or RGB1024 |
| `hdmi.bpp()` | bits per framebuffer pixel: `4` (RGB1024), `8` (RGB640) or `16` (RGB320/RGB512) |
| `hdmi.palette([i[, rgb]])` | RGB1024 16-colour palette: no args lists all 16 (RGB888); `palette(i)` reads entry `i` (0–15); `palette(i, 0xRRGGBB)` sets it (takes effect at once). Use `palette()` (below) to also persist. |
| `hdmi.fill(colour)` | fast fill of the whole framebuffer with a **native-format** colour (e.g. `hdmi.fill(hdmi.fb().colour(BLUE))`) |
| `hdmi.scroll(rows, colour=0)` | fast vertical scroll up by `rows` pixels, filling the exposed bottom with `colour` (native format) |
| `hdmi.putc(x, y, ch, fg, bg)` | blit one 8×12 console glyph at pixel `x,y` (native-format `fg`/`bg`) |
| `hdmi.text(s, x, y, fg, bg=-1, scale=1)` | draw a string in the 8×12 console font at pixel `x,y`; `bg=-1` is transparent, `scale` enlarges each glyph pixel into a `scale`×`scale` block. Returns the x just past the string |
| `hdmi.test()` | draw an 8-bar colour test pattern |
| `hdmi.gen()` | mode-change counter (used internally by the console) |

Note that `hdmi.fill()`, `hdmi.scroll()` and `hdmi.putc()` take colours already
in the framebuffer's native format — convert with `hdmi.fb().colour(...)` first.

The on-screen text console uses an 8×12 font (80 columns × 40 rows at 640×480).

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
| `play(path)` | Play a `.wav`, `.mp3` or `.flac` file (dispatched by extension) |
| `play(path, wait=True)` | Play and block until finished |
| `stop()` | Stop playback immediately |
| `is_playing()` | True while a track is playing |
| `volume(v)` | Set volume 0–100 (perceptual/log taper); `volume()` returns it |
| `beep(freq=880, ms=150)` | Play a short tone |

```python
volume(70)
play("/sd/music/song.flac")
```

WAV, MP3 and FLAC are decoded on the fly. A short sound also plays when a USB
device is plugged in or removed (set `pcaudio.usb_sounds = False` to disable).

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

### Board-specific extension modules

These low-level C modules back the friendly commands above and can also be used
directly: `hdmi`, `keyboard`, `mouse`, `touch`, `audio`, `jpeg`, `bmp`, `png`,
`xmodem`.
Most users will prefer the auto-imported helpers (`play`, `draw_jpg`, `touch`,
`mouse`, `screen`, …) rather than these directly.

---

## Quick reference

```python
# Display
screen(hdmi.RGB320)             # change mode (persisted)
screen(hdmi.RGB1024)             # native 1024x600, 16 colours
palette(6, 0x00FF88)            # redefine an RGB1024 colour (persisted)
d = hdmi.fb(); d.text("hi", 0, 0, d.colour(WHITE))
hdmi.text("BIG", 0, 20, d.colour(RED), -1, 4)   # scaled 8x12 text

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
volume(70); play("/sd/song.mp3"); stop()

# Files
ls("/sd/*.jpg"); run("/sd/app.py"); edit("/sd/app.py")
xrecv("/sd/app.py"); xsend("/sd/app.py")   # XMODEM over the serial console

# Images / clock
draw_jpg("/sd/pic.jpg"); save_image("/sd/screen.bmp")
settime(2026, 7, 4, 14, 30, 0); print(gettime())
```

*Pico Computer 3 firmware v0.3 — based on MicroPython. See
https://docs.micropython.org/ for the Python language and standard library.*
