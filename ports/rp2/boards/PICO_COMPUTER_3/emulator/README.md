# The Pico Computer 3 Emulator

A PC build of the Pico Computer 3: the same MicroPython interpreter and the
same board code as the firmware, with the hardware layer (HSTX video, USB
input, I2S audio, the DS3231, the I/O header) provided by SDL2 and the host.
It boots to the machine's banner and `>>>` in a window, and runs the course
book's programs unmodified -- display modes, keyboard, mouse, sound, clock
and all.

Because the drawing, keyboard decoding, audio rendering and the frozen
Python modules are compiled from the identical sources the firmware uses,
what you see is the machine's behaviour, not an imitation of it.

## Building (Linux, or Windows via WSL2)

Requirements: git, the usual C build tools, and SDL2. From nothing to a
running emulator (a few minutes on any distro, however old):

    sudo apt install -y git build-essential libsdl2-dev python3
    git clone --branch pico-computer-3 https://github.com/UKTailwind/micropython
    cd micropython
    make -C mpy-cross -j$(nproc)
    make -C ports/unix VARIANT=pc3 submodules
    make -C ports/unix VARIANT=pc3 -j$(nproc)

Run it:

    ports/rp2/boards/PICO_COMPUTER_3/emulator/pc3emu

**Clone with git as shown -- GitHub's "Download ZIP"/source-tarball
links won't build**: those archives lack the git submodules
(`lib/ulab`, `lib/usqlite` and friends), and the `make ... submodules`
step can only fetch them inside a real clone.

Without `libsdl2-dev` the build still works but is terminal-only (no
display window) -- install SDL2 and rebuild for the full machine.

On Windows, install Ubuntu under WSL2 (`wsl --install -d Ubuntu-26.04`
in an administrator PowerShell, then the commands above inside Ubuntu).
WSL2's WSLg shows the emulator's windows and plays its audio natively.
**`INSTALL-WINDOWS.md` (alongside this file, and in the release
download) is a complete walk-through for a machine that has never seen
WSL** — including the prebuilt-download route that skips compiling
entirely. A native Windows build (no WSL) is future work: the
emulator's console plumbing is POSIX.

## Using it

- The **display window** is the machine: the boot banner, the REPL, all
  graphics. Type in it as you would at the real keyboard; layouts follow
  `keymap("UK")` etc., exactly as on the machine.
- The window is **resizable** — drag any corner and the image scales to fit,
  keeping its aspect ratio (black bars fill the remainder). On a hi-res /
  4K monitor it opens scaled up automatically so it's readable; override the
  starting size with `PC3EMU_SCALE=N` (an integer 1–4), e.g.
  `PC3EMU_SCALE=1 pc3emu` for exact native pixels or `PC3EMU_SCALE=3` for a
  big window. The mouse tracks correctly at any size.
- The **launching terminal** doubles as the serial console: everything is
  mirrored there, you can type there too, and it is the natural place to
  paste from (like a serial terminal on the real machine).
- **Ctrl-V in the window** pastes the system clipboard as keystrokes --
  `autosave("prog.py")`, Ctrl-V, Ctrl-Z is the fastest way to get a book
  listing in.
- **Ctrl-C** behaves as on the machine: cancels the line at the prompt,
  interrupts a running program. Closing the display window is Ctrl-C too.
- **Exit** with Ctrl-D at the prompt (or `machine.reset()`).

### The drives

`/` (the flash) and `/sd` are two host directories, printed in the boot
banner:

    ~/.pc3emu/flash        the flash filesystem (settings.json lives here)
    ~/.pc3emu/sd           the SD card

Drop files in from the host and they appear on the machine instantly.
Set `PC3EMU_HOME` to run separate machines with independent drives:

    PC3EMU_HOME=/tmp/demo pc3emu

### The virtual I/O header

    >>> import Pins

opens a second window with the machine's I/O header: a latched switch and
an LED for every exposed pin (GP0-7, GP20, GP21, GP26, GP34-46) and a
potentiometer on each analog pin (GP40-46).

- Click a switch to set the level a `machine.Pin` input reads; a Pin's
  pull chooses the idle level until you click.
- LEDs show a Pin's output when the pin is an output.
- Drag a pot and `machine.ADC(...).read_u16()` follows it.
- `Pin.irq` handlers fire on switch edges -- the book's doorbell rings.

`Pins.close()` closes the panel. From code, `_emupins.set_switch(gpio, v)`
presses a switch programmatically (handy for scripted tests).

### What's emulated, what's real, what's absent

| Subsystem | Status |
|---|---|
| HDMI (all four modes, layer, vsync) | the firmware's drawing core, SDL scanout |
| Keyboard (layouts, rollover, repeat) | the firmware's decoder, SDL events |
| Mouse / pccursor / pcgui | the firmware's module, SDL events |
| Sound (synth, tones, MOD, WAV/MP3/FLAC) | the firmware's renderer, SDL audio |
| DS3231 clock + daily alarm (INT on GP32) | register-level, backed by the host clock |
| GPIO / ADC | the Pins panel (or sensible idle values) |
| Wi-Fi / requests / MQTT | the host's real network |
| ulab / pcmath / plot | the same ulab the firmware builds |
| SQLite (`import usqlite`) | the same usqlite the firmware builds, over the host filesystem |
| Touch panel | reports not-present |
| XMODEM, Bluetooth, deep sleep | not emulated |

Honest differences: the PC is far faster (pace game loops with
`pcgame.Clock(vsync=True)` or `hdmi.vsync()`, as the book teaches);
`gc.mem_free()` starts from the same 8 MB but fragments differently;
`console("screen")` cannot silence the launching terminal.

## Binary releases

`make-dist.sh` in this directory builds a self-contained
`pc3emu-linux-x64.tar.gz`: the executable, a launcher, this README, and an
`sd-seed/` carrying the course book's example programs and the reader's
toolkit -- copied onto the SD card on first run, so `cd("/sd/examples")`
works out of the box. The target machine needs only the SDL2 runtime
(`sudo apt install libsdl2-2.0-0`).

The prebuilt binary is made on current Ubuntu and needs **glibc 2.43 or
newer** (Ubuntu 26.04+). On an older distro it stops with
`` version `GLIBC_2.43' not found `` -- that machine can't run this
binary, but building from source (above) works everywhere and takes
only a few minutes.
