# Appendix C — The modules, one paragraph each

Everything the firmware offers, with its book chapter and its User
Manual section for the full API. Names marked † are injected at boot —
usable without an `import`.

Everything under **"The board's own"** below is **Pico Computer 3
specific** — these modules are this machine's, not part of standard
MicroPython, so code that leans on them won't run unchanged on another
board. Everything under **"The standard library"** travels with you.
When it matters in the chapters, the book flags the board-specific
pieces as they appear.

## The board's own

**pcshell** † — the shell: `ls cd pwd cat cp mv rm mkdir rmdir run
edit autosave cls`. Ch. 4–5; manual §4.

**pcconsole / `console()`** † — routes console output
(`"both"/"serial"/"screen"/"none"`) and owns the on-screen terminal
(ANSI colours included). Ch. 2, 17; manual §1.

**hdmi** † — the display engine: modes, `fb()` → `Display`, the
`N/L/F` buffer system, `blit`, `vsync`, `text`, fonts. Ch. 15–17;
manual §5.

**pcgfx** † — the `Display` drawing class (`line rbox arc bezier
flood`...) and the named colour palette. Ch. 15; manual §5.

**pcimage** — `draw_jpg/png/bmp` †, `save_image` †, and
`load_image()` → `Image` (sprite sheets, `.cell`, `.sprites`).
Ch. 16; manual §10.

**pcsprite** — the sprite engine: `grab sheet Sprite`, layers,
`update()` collision events, walls, scrolling. Ch. 18; manual §5.

**pctilemap / `TileMap`** † — tile worlds: maps, camera, attributes,
`collide`, `blit_tile`, CSV loading. Ch. 19; manual §5.

**pcturtle / `Turtle`** † — turtle graphics, MMBasic conventions.
Ch. 9; manual §5.

**pcgui** — the widget toolkit: `GUI`, twenty-ish controls, pop-up
keyboards, callbacks. Ch. 27; manual §5.

**pccursor** — the visible mouse pointer (save-under; ARROW/CROSS).
Ch. 27; manual §7.

**pcaudio** — `play pause resume stop volume beep` †, `tone` †,
`sound` † (4-voice synth), `mod_sample` †. Ch. 20; manual §9.

**pcnet** — `wifi tz ntpsync auto` †: connection, timezone, NTP.
Ch. 30; manual §11, §16.

**pcplot / `plot()`** † — autoscaled plotting: data, functions,
series, styles. Ch. 8, 30; manual §17.

**pcmath** — `correl chi_square crossings power_spectrum window`,
3-D vectors, quaternions, `PID`. Ch. 31; manual §17.

**pcgame / `Clock`** — drift-free frame timing, `dt`, `vsync=True`,
`.fps`. Ch. 22; manual §17.

**pcsd** — SD mount + hot-swap watcher (runs itself; you just use
`/sd`). Ch. 4; manual §12.

**ds3231** — the battery clock: `settime gettime synctime` † and the
daily alarm (`set_alarm alarm_fired clear_alarm alarm_pin`).
Ch. 29, 33; manual §11.

**keyboard / `keydown()`** † — live key state, named key constants,
`on_key`, `on_usb_event`; `keymap()` † for layouts. Ch. 21; manual §6.

**gamepad / `gamepad()`** † — read a USB gamepad (Xbox, DualShock 3/4,
and a table of common HID pads — recognised controllers only, so a
keyboard's stray HID interface is never mistaken for one):
`"LX" "LY" "RX" "RY"` sticks, `"L" "R"` triggers, `"B"` button bitmap
(`gamepad.A`…), `"H"` hat, `"T"` type, PS4 gyro/accel;
`gamepad.monitor()` + `gamepad.configure()` teach it an unrecognised
pad. Manual §7.

**USBSerial** † — a USB-serial adapter (FTDI/CP210x/CH34x/CDC-ACM) on
the host port, as a UART-like object: the same `read write readline
readinto flush any` stream methods, plus `connected()` and
`on_change()`. Manual §7.

**xmodem** — `xrecv xsend` †: file transfer over the USB-C serial.
Ch. 5; manual §13.

## The standard library (the ones this book used)

**math** trig, `sqrt`, `pi`, `radians` (ch. 3, 24) · **random**
`randint choice seed` (ch. 7) · **time** `sleep sleep_ms ticks_ms
ticks_us ticks_diff localtime mktime` (ch. 5, 21, 28) · **os**
`listdir mkdir remove statvfs uname` (ch. 16, 35) · **json**
`load(s)/dump(s)` (ch. 30) · **gc** `mem_free collect` (ch. 35) ·
**sys** `path implementation` (ch. 11) · **machine** `Pin ADC PWM I2C
SPI UART Timer WDT RTC freq` (ch. 32, 33) · **micropython** `const
native viper mem_info` (ch. 35) · **framebuf** (under `Display`;
manual §5) · **network** WLAN (ch. 30) · **requests** HTTP(S)
(ch. 30) · **umqtt.simple/robust** MQTT (ch. 30) · **ntptime**,
**mip** (installer), **aioble/bluetooth** (BLE — manual §16) ·
**asyncio** tasks (ch. 34) · **ulab.numpy / ulab.scipy** arrays, FFT,
linalg (ch. 31) · **usqlite** on-device **SQLite** database —
`connect()`, SQL, cursors, `.db` files on `/` or `/sd` (manual §17).

## Your own shelf (built through the book)

`shapes.py` (11) · `scorelib.py` (12) · `handy.py` (13, 21) ·
`sfx.py` (20) · `initials.py` (26) · `gpad.py` (31) · `bench.py`
(34) · `backup.py` (35). No one can take these away; improving them
is the hobby.

Where these live and how `import` finds them — next to your program,
the `/lib` shelf on flash, or a folder you add to `sys.path` (from
`/boot.py`, to make it stick) — is chapter 11.
