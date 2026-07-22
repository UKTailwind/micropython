# Chapter 33 — Standing on others' shoulders

You have written your own modules (chapter 11), and the machine came
with dozens frozen in (appendix C lists them). There is a third shelf,
and it dwarfs the other two: the libraries that strangers wrote, and
gave away. Chapter 11 named the tool that reaches it — **`mip`**. This
chapter is the *craft* of using it: how to find the right library, how
to tell a good one from a shaky one, and how to wire it to your
machine. And — rare for this book — almost none of it is Pico Computer
3 specific. The skill travels to every MicroPython board you will ever
touch.

## The method

Reaching for a library is the same six steps every time, so learn them
once and every example below is a variation:

1. **Name the interface.** Is the thing a UART device, an I2C device —
   or pure software, no wires at all?
2. **Recognise the data.** What shape does it arrive in? A named
   standard has a name worth knowing.
3. **Search with that word.** Awesome MicroPython, `micropython-lib`,
   or a plain web search — armed with the *right* term.
4. **Judge it.** The two-minute checklist further down.
5. **Install it.** One line of `mip`.
6. **Bind it.** Feed your hardware, or your data, to the library's API.

We will walk the loop three times: once with no hardware at all, once
over a wire, once over a bus.

## A library with no wires: QR codes

The best first library is one you can use *this second*. The machine
cannot make a QR code — there is no `qr` module in the firmware.
Someone wrote one and shared it: **`uQR`**.

Find it, and read the repository before you trust it: at
`github.com/JASchilz/uQR` there is a single file, `uQR.py`, and **no
`package.json`**. That is the single-file case from chapter 11 — the
`github:` shortcut has no manifest to read, so you point `mip` straight
at the raw file. Wi-Fi up (chapter 30), then:

```python
>>> wifi("MySSID", "MyPassword")
>>> import mip
>>> mip.install("https://raw.githubusercontent.com/JASchilz/"
...             "uQR/master/uQR.py")
```

One file lands in `/lib`. From now on `import uQR` works, forever,
offline. Now use it — `edit("qr.py")`:

```python
# qr.py -- draw any text as a scannable QR code on the screen.
from uQR import QRCode

def show_qr(text):
    # a 2-cell quiet zone (the white margin)
    qr = QRCode(border=2)
    qr.add_data(text)
    # a square grid of True/False; True = a black cell
    grid = qr.get_matrix()
    n = len(grid)

    d = hdmi.fb()
    # a QR MUST sit on light -- readers need the contrast
    d.fill(d.colour(WHITE))
    black = d.colour(BLACK)

    # fill most of the 480-tall screen
    scale = 440 // n
    x0 = (640 - n * scale) // 2    # ...centred
    y0 = (480 - n * scale) // 2
    for row in range(n):
        for col in range(n):
            if grid[row][col]:
                d.fill_rect(x0 + col * scale, y0 + row * scale,
                            scale, scale, black)

if __name__ == "__main__":
    show_qr("https://github.com/UKTailwind/micropython")
```

Run it, then **point your phone's camera at the monitor**. A moment,
and it offers to open this machine's own source repository. You wrote a
dozen lines; a stranger's library did the ferocious mathematics of
Reed–Solomon error correction underneath. *That* is the third shelf.

Look at how little of it was yours. The `grid` from `get_matrix()` is
nothing exotic — a list of rows of `True`/`False`, black where `True` —
and everything after it is chapter 15's `fill_rect`, one small square
per black cell. You could have written the drawing. You could not
reasonably have written the encoder.

*In one line: a library adds a power the firmware never had — you
supply a few lines of glue, it supplies the hard part.*

## Is this library any good?

Before you `import` a stranger's code into a project you care about,
spend two minutes on it. The awesome-list has no quality gate — being
listed means a human thought it worth a link, nothing more — so the
judgement is yours:

- **Who wrote it, and is it alive?** A known name (Adafruit, Peter
  Hinch, mcauser) or a repo with recent commits and *closed* issues
  beats an abandoned one-off.
- **Does it show its work?** A README with a runnable example — and,
  even better, a test file — means someone has already walked the path
  you are about to.
- **Is it MicroPython's size?** A driver or a pure-Python utility will
  fit; a giant package from the desktop world will not (see the gotchas
  below).
- **What does it assume about numbers?** The sharp question, and the
  easy one to miss. A library doing astronomy, GPS coordinates, or long
  running sums may quietly assume 64-bit floats — or, if it is careful,
  defend itself against 32-bit ones. The Pico Computer 3 runs **double
  precision**, so it is forgiving; but the habit of asking *"what
  precision does this need?"* is what separates code that works from
  code that works *sometimes*.

That last point is not academic — the very next example is a library
whose author defends you from the trap by construction, and you will
see exactly where.

## A library over a wire: GPS

Now hardware. A GPS receiver is a serial device: give it power and it
talks — endlessly, unprompted — out of a single wire. Connect its **TX
to GP1** (UART0's receive) and its **RX to GP0**, share GND, and look
at what turns up — `edit("gpslook.py")`:

```python
from machine import UART, Pin
import time

# most GPS modules default to 9600 baud
uart = UART(0, 9600, tx=Pin(0), rx=Pin(1))
for _ in range(5):
    time.sleep(1)
    print(uart.read())
```

Lines like `b'$GPGGA,123519.00,4807.038,N,01131.000,E,1,08,...'` scroll
past. That is step two of the method — **recognise the data**. This is
not a private format; it is **NMEA 0183**, the text that every GPS on
Earth speaks. You do *not* parse it by hand (the checksums alone will
spoil your afternoon). You search "micropython nmea gps", and the trail
leads to **`micropyGPS`** — one file, well-worn, listed in Awesome
MicroPython.

Install it (a single file again, no `package.json`):

```python
>>> mip.install("https://raw.githubusercontent.com/inmcm/"
...             "micropyGPS/master/micropyGPS.py")
```

Then bind it: the parser eats the serial stream one character at a
time and keeps the current position up to date — `edit("where.py")`:

```python
# where.py -- read a GPS receiver on GP0/GP1 and report position.
from machine import UART, Pin
from micropyGPS import MicropyGPS
import time

uart = UART(0, 9600, tx=Pin(0), rx=Pin(1))
# 'dd' = plain decimal degrees
gps = MicropyGPS(location_formatting='dd')

print("waiting for a fix -- a clear view of the sky helps "
      "(Ctrl-C stops)")
while True:
    while uart.any():
        # feed each character in
        gps.update(chr(uart.read(1)[0]))
    if gps.satellites_in_use:
        # e.g. [51.5074, 'N']
        lat = gps.latitude
        # e.g. [0.1278, 'W']
        lon = gps.longitude
        print(f"\r{lat[0]}{lat[1]}  {lon[0]}{lon[1]}   "
              f"({gps.satellites_in_use} sats)   ", end="")
    time.sleep_ms(200)
```

Take it near a window; the first fix from cold can take a minute. Then
the numbers settle onto *your* place on the planet.

Here is where the precision question earns its keep. I asked for
`location_formatting='dd'` — decimal degrees, the form you would paste
into a map. On an ordinary MicroPython board (32-bit floats) that is a
mild gamble: a latitude like `51.5074°` has barely seven digits to
spend, and the last of them is *metres*. Notice that micropyGPS's
default is **not** `'dd'` but `'ddm'` — degrees and minutes kept
*apart* — precisely so the fractional part keeps its precision whatever
the float width. That is an author quietly protecting you from the
32-bit trap. On the Pico Computer 3, with double precision, decimal
degrees are safe well past the millimetre, and you can ask for `'dd'`
without a second thought — the same quiet advantage that fixes so much
else on this machine.

*In one line: recognise the data's real name, and the library you need
is one search away.*

## The same shape, a different bus: an I2C sensor

Chapter 32 met the TSL2591 light sensor on the QWIIC socket and read it
the hard way, decoding registers as `ds3231.py` does. You rarely need
to: nearly every I2C module has a published driver, and installing one
is the method again, one bus over. Roll-call the bus (`i2c.scan()` from
chapter 32), note the address, search "micropython" and the sensor's
name, `mip.install` the driver, and then call its friendly method —
`.lux`, `.temperature`, `.pressure` — instead of shuffling bytes
yourself. Raw registers when you must; a driver when you can.

The shape never changes: **interface → recognise → search → judge →
install → bind.** UART, I2C, or no wire at all, it is always those six
moves.

## Three gotchas worth the ink

- **MicroPython is not PyPI.** A library has to be written *for*
  MicroPython — small, and not leaning on desktop-only machinery. A
  giant scientific package from the PC world will simply refuse to
  install. When in doubt, search Awesome MicroPython first: if it is
  there, it fits.
- **A library does not update itself.** `mip.install` copies today's
  version into `/lib` and there it stays. To move up, run `mip.install`
  again; if a project misbehaves after an upgrade, delete the file from
  `/lib` and reinstall a known-good version.
- **Where `import` looks.** Installed files live in `/lib`, which is on
  the import path — but Python checks *next to your program first*. A
  file you are hacking on beside your code therefore shadows the
  installed copy of the same name: occasionally surprising, usually
  convenient. (Chapter 11 has the whole search path — `''`, `.frozen`,
  `/lib` — and how to add your own folders to it with `sys.path`.)

> **Coming from MMBasic:** MMBasic ships almost everything inside the
> firmware — one big binary, with `LIBRARY SAVE` for your own
> additions. MicroPython takes the opposite bet: a small core, and a
> whole internet of add-ons a `mip.install` away. The price is a
> download (and Wi-Fi, once); the prize is an ecosystem no single
> firmware could ever hold.

## Project: the machine's business card

Put a library and the drawing you already know together into something
you would actually pin to the wall: a full-screen card for your Pico
Computer 3 — its name and firmware version, beside a QR code anyone can
scan to build their own. `edit("card.py")`:

```python
# card.py -- an about-screen with a scannable QR of the project
# repo.
from uQR import QRCode
import os

REPO = "https://github.com/UKTailwind/micropython"

def card():
    d = hdmi.fb()
    d.fill(d.colour(WHITE))
    black = d.colour(BLACK)

    # the QR code, lower-right
    qr = QRCode(border=2)
    qr.add_data(REPO)
    grid = qr.get_matrix()
    n = len(grid)
    scale = 300 // n
    x0 = 640 - n * scale - 30
    y0 = 480 - n * scale - 30
    for r in range(n):
        for c in range(n):
            if grid[r][c]:
                d.fill_rect(x0 + c * scale, y0 + r * scale, scale,
                            scale, black)

    # the words, upper-left
    hdmi.text("PICO COMPUTER 3", 30, 50, black, -1, 4)      # big
    # full name + version
    hdmi.text(os.uname().machine, 30, 110, black, -1, 1)
    hdmi.text("Scan to build your own", 30, 430, black, -1, 2)

card()
```

`os.uname().machine` is the same string the banner prints — the name
and firmware version, straight from the horse's mouth — and the QR is
the encoder you installed in six lines of glue. Set `card()` as the
first thing your `/main.py` runs (chapter 36) and the machine greets
every power-on with its own credentials and a way for a friend to make
one too. A whole chapter of skill, standing on one stranger's shoulders.

## Experiments

1. Change the string in `qr.py` to your Wi-Fi network, a message, or a
   phone number, and scan it. Now feed it a very long paragraph and
   watch the QR grow denser — the library silently picks a bigger
   "version" (more cells) to hold more data.
2. **Redundancy, witnessed.** Display the repo QR, then cover a corner
   of it with your thumb and scan anyway. It still reads — QR codes
   carry error-correcting data precisely so a coffee stain (or a thumb)
   cannot kill them. That is the Reed–Solomon maths you did *not* have
   to write, earning its keep.
3. **The two-minute audit.** Open two libraries from Awesome
   MicroPython that do the same job (two GPS parsers, or two drivers for
   one sensor) and run the checklist on each: last commit, README,
   tests, and what they assume about `float`. Write down which you would
   trust, and why.
4. If you have a GPS module: print the raw NMEA line and the parsed
   `gps.latitude` side by side, and watch `gps.satellites_in_use` climb
   from 0 as more satellites lock on.

## Challenges

1. **The card, completed.** Add today's date (the DS3231, chapter 29)
   and the machine's free memory (`gc.mem_free()`, chapter 35) to the
   business card, and make it your `/main.py` boot screen for five
   seconds before the shell appears.
2. **The tracker.** A GPS position logged to a CSV file once a minute
   (chapter 29's cron-junior), then drawn as a path with `plot()`
   (chapter 31). A walk around the block, saved as data.
3. **Find and add.** Pick a device you own or covet — an OLED display,
   a temperature-and-humidity sensor, an RFID reader — hunt down its
   MicroPython driver on Awesome MicroPython, judge it with the
   checklist, install it, and get *one* reading or *one* pixel out of
   it. The whole method, aimed at your own quarry.
4. **The precision audit, for real.** Find a library that does serious
   arithmetic on large numbers — astronomy, GPS, anything with dates as
   big counts — and read its source. Does it assume 64-bit floats, or
   guard against 32-bit ones? Decide whether it is safe to trust on an
   ordinary board, and whether the Pico Computer 3's double precision
   changes your answer. (You are now asking the question most people
   never learn to ask.)
