# Appendix K — Running this book on a Colour Maximite 2

Everything in this book also runs on a **Colour Maximite 2** — the G1,
G2 and G3 boards — with its **MicroPython app**. The app is the same
MicroPython as the Pico Computer 3's, with the same `hdmi`, `keyboard`,
`mouse` and sound modules, the same `pc…` libraries, `ulab`, SQLite,
networking and the rest, built for the Colour Maximite 2's processor. Every
example program in the book was run on a Colour Maximite 2 G2, and all of
them run.

This appendix is the short list of what is different: how you start
MicroPython, where your files are, which pins to wire, and the few things
the Colour Maximite 2 does its own way.

## Getting it and starting it

The Colour Maximite 2 is an MMBasic computer: it switches on into BASIC,
and MicroPython is a program you run from its SD card. So chapter 2's
flashing is not needed — instead:

1. The firmware must be **MMBasic 8.00.00b2 or later** (`? MM.VER` at the
   prompt shows the version; the Colour Maximite 2's releases page has the
   newer firmware).
2. Copy **`micropython.app`** from the firmware release's programs zip to
   the root of the SD card.
3. At the MMBasic prompt type:

```
RUN "micropython.app"
```

A couple of seconds later you are at the `>>>` prompt of chapter 3, on the
screen and on the serial console. From there the book works as written.

- **Back to MMBasic:** `machine.reset()` — on the Colour Maximite 2 it ends
  MicroPython and returns to the BASIC prompt, rather than restarting the
  board.
- **One program, then back:** `RUN "micropython.app", myprog.py` runs that
  program and returns to MMBasic when it ends.
- **Ctrl-D** at `>>>` is a soft reset, as on the Pico Computer 3.

## Files

The Pico Computer 3 has two drives — its flash at `/` and the SD card at
`/sd`. The Colour Maximite 2 has one: the SD card is **both** `/` and
`/sd`, so every path in the book works unchanged. A USB flash drive is
`/usb` when one is plugged in. `/lib` (chapter 33) is the card's `lib`
folder, and `boot.py` and `main.py` (chapter 36) go in the card's root.

To move files to and from a PC, take the card out, or use MMBasic's own
`XMODEM` or `ZMODEM` at its prompt before starting MicroPython (`xsend()`
and `xrecv()` of chapter 5 are not in the app).

## Screen, keyboard, mouse and sound

All of Part III runs unchanged. Every `screen()` mode is there: the G1 and
G2 send it to a VGA monitor, the G3 to HDMI. The keyboard layout is
MMBasic's: set it once with `OPTION KEYBOARD` at the BASIC prompt, rather
than with chapter 2's `keymap()`. Sound comes from the Colour Maximite 2's
audio socket (and on a G3 over HDMI too).

A touch panel is not passed to MicroPython: `touch()` reports none, as on a
Pico Computer 3 without one, and every program that uses it works with the
mouse. Gamepads likewise report none.

## Pins: the I/O chapter

The Colour Maximite 2's I/O pins are on its rear 40-pin connector, and
MicroPython names them by the **same GP numbers** the book uses, so the
programs of chapter 32 run as printed. What changes is *where* each GP is:

| The book's pin | Connector pin | Used in |
|---|---|---|
| GP0 | 8 | the LED (ch. 32) |
| GP1 | 10 | the button |
| GP2 | 12 | the servo (PWM) |
| GP20 (I2C SDA) | 21 | I2C devices, but see below |
| GP21 (I2C SCL) | 33 | I2C devices, but see below |
| GP40 (analogue) | 26 | the knob |
| GP41, GP42 (analogue) | 29, 37 | a second knob |
| GP43–GP46 (analogue) | 7, 13, 15, 24 | |

Ground is on pins 6, 9, 14, 20, 25, 30, 34 and 39, and 3.3 V on pins 1
and 17. Pins 2 and 4 are 5 V, for powering things only — never into a
pin.

- **The LED.** `Pin("LED")` is the Colour Maximite 2's **SD card light**,
  on the front: chapter 8's blink program flashes it (reading the card
  lights it too).
- **Analogue pins.** GP40–GP46 are second names for the connector's
  analogue pins: GP40 is pin 26, which MicroPython also calls GP26. Use one
  name or the other for a pin, not both.
- **I2C.** `I2C(0, sda=Pin(20), scl=Pin(21))` works on pins 21 and 33, with
  pull-up resistors switched on. On a Colour Maximite 2 with the **Wi-Fi
  module**, pin 33 belongs to the module, and the same line uses the
  Colour Maximite 2's own I2C instead: wire SDA to **pin 3** and SCL to
  **pin 5**.
- **Not on the connector:** GP34, GP35, GP38 and GP39 (the Pico Computer 3
  header's spares) have no pin here.
- **Pin 40 is the reset link.** Held to ground at power-up, it erases the
  saved BASIC program and every MMBasic option. None of the book's
  projects use it — never wire a button there.

## Wi-Fi: chapter 30

A Colour Maximite 2 with the **Wi-Fi module** runs chapter 30 as written —
`wifi()`, `tz()`, `ntpsync()`, `auto()`, `requests` over HTTPS, MQTT and
`mip.install`. Give it your network once, at the MMBasic prompt:

```
OPTION WIFI "MyNetwork", "MyPassword"
```

It then joins at every power-up, before MicroPython starts, and
`wifi()` finds it connected. `wifi("SSID", "password")` joins another
network for that session, without changing the option. Without the module
there is no network: `board.has_wifi()` is `False`, and libraries such as
chapter 33's are copied into `/lib` from a PC instead of installed with
`mip`.

## Clock and alarm: chapter 29

`gettime()`, `settime()` and `synctime()` work on the Colour Maximite 2's
own battery-backed clock — the same clock as MMBasic's `TIME$` and
`DATE$`. The daily alarm (`ds3231.set_alarm()`, `alarm_fired()`,
`clear_alarm()`) works too: its interrupt line is not on the connector,
so `ds3231.alarm_pin()` gives a pin that is checked four times a second
— chapter 34's wake-up program cannot tell the difference.

## Memory and speed: chapter 35

The Colour Maximite 2 runs Python about **twice as fast** as the Pico
Computer 3, so chapter 35's timings come out smaller. `gc.mem_free()`
shows less: about 3 MB at the prompt on a G2 or G3 (2 MB more is set aside
for SQLite, so chapter 28's databases always find their memory), and less
on a G1. The book's programs fit comfortably.

## A Colour Maximite 2 extra: MMBasic from Python

The `cmm2` module gives Python the machine's BASIC: `cmm2.basic()` runs
one MMBasic statement and `cmm2.evaluate()` returns the value of an
MMBasic expression, as text.

```python
import cmm2
print(cmm2.evaluate("MM.DEVICE$"))       # which Colour Maximite 2
print(cmm2.evaluate("MM.INFO(SDCARD)"))  # the card's state
cmm2.basic('TIME$ = "09:30:00"')         # an MMBasic command
```
