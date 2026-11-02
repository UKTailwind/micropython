# MicroPython for the Colour Maximite 2 G2

MicroPython as a **loadable program** on the Colour Maximite 2 G2. It runs on top of the CMM2's MMBasic firmware,
which loads it into the G2's 8 MB for programs:

```
RUN "micropython.app"                 boot.py and main.py if they are there, then the REPL
RUN "micropython.app", script.py      the script, then back to MMBasic
```

It reaches the hardware only through the firmware's *loader services* (`Inc/cmm2img.h` in the CMM2V1.5-loader
repository, version 6): the console, the files, time, the screen, sound, the keyboard and the mouse.

## Building

In WSL or Linux, with `arm-none-eabi-gcc`:

```
cd ports/cmm2
make CMM2_SDK=/path/to/CMM2V1.5-loader/tools/sdk
```

`build/micropython.app` is the program. Copy it to the SD card.

## What there is

- **The REPL** on MMBasic's console: the serial port, the screen and the USB keyboard.
  - Ctrl-C cancels a line, or raises KeyboardInterrupt in running code. The firmware's break key is taken through `break_take()`, so a BASIC program that ran MicroPython carries on afterwards.
  - Ctrl-D is a soft reset, and `machine.reset()` goes back to MMBasic.
- **Files** through `VfsCMM2`, a VFS on the loader's file services, so the firmware's FatFs does the work:
  - the SD card is mounted at `/` and at `/sd` (as the Pico Computer 3 has its card), and `/lib` is on `sys.path`;
  - the USB drive is at `/usb` when one is plugged in;
  - `open`, `os.listdir`, `stat`, `mkdir`, `chdir`, `remove`, `rename` and imports from files all work.
- **`time`** on the CMM2's clock (MMBasic's DATE$ and TIME$) and its microsecond timer.
- **The Pico Computer 3's modules**, their C built unchanged from `ports/rp2` (as its emulator builds them) over CMM2 back ends:
  - `hdmi` (`hdmi_cmm2.c`): each PC3 mode on the CMM2 mode of its size, its framebuffer put on the screen each frame
    by the loader's frame hook (the DMA2D's copies, or the CPU for a layer and the 16-colour modes):

    | PC3 mode | size | CMM2 mode | refresh's CPU |
    |---|---|---|---|
    | RGB640 | 640x480, 256 colours | MODE 8,8 (75 Hz) | 5% |
    | RGB320_8 | 320x240, 256 colours | MODE 7,8 (75 Hz) | 4% (16% with a layer) |
    | RGB320 | 320x240, RGB565 | MODE 7,16 (75 Hz) | 6% (20% with a layer) |
    | RGB512 | 512x300, RGB565 | MODE 18,16 at 2x2 (60 Hz) | 32% |
    | RGB640_4 | 640x480, 16 colours | MODE 8,8 (75 Hz) | 11% |
    | RGB1024 | 1024x600, 16 colours | MODE 18,8 (60 Hz) | 41% |

    While the display is on, the REPL is shown on the serial console only.
  - `draw3d`, and the image loaders `jpeg`, `png` and `bmp`.
  - `keyboard` (`kbd_cmm2.c`): `keydown()` is MMBasic's KEYDOWN, and `on_key()` sees each key.
  - `mouse` (`mouse_cmm2.c`): `query()` on the firmware's mouse (USB, PS/2 or I2C).
  - `audio` (its decoders and synths) and **`machine.I2S`** (`machine_i2s_cmm2.c`) on the loader's sound stream, with
    the PC3's non-blocking contract, so the PC3's `pcaudio.py` plays tones, the synth, WAV, MP3, FLAC and MOD.
    `machine.Pin` is only a pin's number for now.
- **`usqlite`** (SQLite 3.47, `lib/usqlite`), its databases on the card or the USB drive; its pool of 2 MB is taken
  from the heap on the first `connect()`. Its regression tests (`lib/usqlite/tests`) pass on the G2.
- **`ulab`** (numpy-like arrays, `lib/ulab`).
- **`machine`:** `reset()`, `soft_reset()`, `freq()`, `unique_id()`, `idle()`, `lightsleep()`, `I2S`.
- **The language:** double-precision floats (the M7's FPU), big integers, and the EXTRA_FEATURES set of modules (json, re, struct, random, math, collections, asyncio, framebuf...).
- **About 5.5 MB of heap** (3.4 MB while a database is open). The program is 1.1 MB.

`hdmitest.py` shows each display mode and the mouse and keys; `imgtest.py` the image loaders.

## What is to come

- the PC3's frozen `pc*.py` libraries (its console on the screen, graphics, sprites, GUI, sound...);
- `machine.Pin` and the buses, which need new loader services.
