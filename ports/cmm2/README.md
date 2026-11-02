# MicroPython for the Colour Maximite 2 G2

MicroPython as a **loadable program** on the Colour Maximite 2 G2. It runs on top of the CMM2's MMBasic firmware,
which loads it into the G2's 8 MB for programs:

```
RUN "micropython.app"                 boot.py and main.py if they are there, then the REPL
RUN "micropython.app", script.py      the script, then back to MMBasic
```

It reaches the hardware only through the firmware's *loader services* (`Inc/cmm2img.h` in the CMM2V1.5-loader
repository, version 5): the console, the files, time, the screen, sound, the keyboard and the mouse.

## Building

In WSL or Linux, with `arm-none-eabi-gcc`:

```
cd ports/cmm2
make CMM2_SDK=/path/to/CMM2V1.5-loader/tools/sdk
```

`build/micropython.app` is the program. Copy it to the SD card.

## What there is (phase 1)

- **The REPL** on MMBasic's console: the serial port, the screen and the USB keyboard.
  - Ctrl-C cancels a line, or raises KeyboardInterrupt in running code. The firmware's break key is taken through `break_take()`, so a BASIC program that ran MicroPython carries on afterwards.
  - Ctrl-D is a soft reset, and `machine.reset()` goes back to MMBasic.
- **Files** through `VfsCMM2`, a VFS on the loader's file services, so the firmware's FatFs does the work:
  - the SD card is mounted at `/` and at `/sd` (as the Pico Computer 3 has its card);
  - the USB drive is at `/usb` when one is plugged in;
  - `open`, `os.listdir`, `stat`, `mkdir`, `chdir`, `remove`, `rename` and imports from files all work.
- **`time`** on the CMM2's clock (MMBasic's DATE$ and TIME$) and its microsecond timer.
- **`machine`:** `reset()`, `soft_reset()`, `freq()`, `unique_id()`, `idle()`, `lightsleep()`.
- **The language:** double-precision floats (the M7's FPU), big integers, and the EXTRA_FEATURES set of modules (json, re, struct, random, math, collections, asyncio...).
- **About 6.5 MB of heap.** The program is 221 KB.

## What is to come

The plan is for Pico Computer 3 compatibility:
- **phase 2:** `hdmi` (hdmi.c over a CMM2 back end, on the matching CMM2 modes), the keyboard and the mouse;
- **phase 3:** `audio`, and the image loaders;
- **phase 4:** the PC3's frozen `pc*.py` libraries;
- **phase 5:** `machine.Pin` and the buses, which need new loader services.
