# Pico Computer 3 Emulator — design and work plan

Goal: a PC application (Windows + Linux) that *is* a Pico Computer 3
running firmware v0.8 — boots to the banner and `>>>` in an SDL window,
runs every program in the book and User Manual, with correct HDMI
modes, DS3231 backed by the PC clock, and audible sound.

## Approach

Not silicon emulation (the HSTX/PIO video path is a research project to
emulate and buys nothing). Instead: **the same MicroPython interpreter
and the same board code, rebuilt for the PC**, with the thin hardware
layer swapped for SDL2. Everything the user can observe comes from the
identical source that runs on hardware, so fidelity is structural
rather than imitated.

Three layers, established by survey of the v0.8 tree:

1. **Frozen Python modules** (`pcshell`, `pcfm`, `pye`, `pcgui`,
   `pcgame`, `pcsprite`, `pctilemap`, `pcturtle`, `pcgfx`, `pcimage`,
   `pcconsole`, `pccursor`, `pcaudio`, `pcmath`, `pcplot`, `pcnet`,
   `ds3231`, …) — run unchanged. Proven: the book's 34 chapters were
   verified on the unix port.
2. **Portable board C** — compiles unchanged:
   - `hdmi.c` drawing core: all primitives, text, blit, tilemap, flood,
     scroll, palette operate on plain byte buffers via
     `hdmi_target_ptr()`/`hdmi_wbuf()`; no hardware touched.
   - `audio.c` (+ `hxcmod`, `dr_wav`, `dr_mp3`, `dr_flac`,
     `sound_tables.h`): a pure **pull** renderer — Python calls
     `tone_read`/`sound_read`/`mod_read`/`wav_read`… to fill PCM
     buffers. No hardware at all in this file.
   - Image loaders `jpeg.c`/`bmp.c`/`png.c` (+ `picojpeg`, `upng`,
     `bmp_decoder`, `dither.c`) — pure CPU.
   - Fonts (`fonts.h`, MMBasic set) — data.
3. **Hardware C to replace** — the only real work:
   - `hdmi.c` scanout: core1 fill-loop + per-mode DMA IRQs + HSTX
     registers + `hdmi_set_clock`. → SDL backend (below).
   - TinyUSB host input (`mp_usbh.c`, `usb_keyboard.c`,
     `usb_mouse*.c`, `usb_touch*.c`) → SDL events.
   - `machine.I2S` output (used by `pcaudio.py`) → SDL audio queue.
   - `machine.Pin/ADC/PWM/I2C/Timer`, `_sercon`, `xmodem`,
     `machine_sdcard` → emulated or stubbed (below).

## Host skeleton

A new **unix-port variant** (`ports/unix/variants/pc3`) plus an
emulator backend directory (this one). The unix port supplies the
interpreter, POSIX VFS, sockets/TLS, `select`, `ffi`. Build with:

- `MICROPY_FLOAT_IMPL_DOUBLE` (matches the board),
- heap fixed at **8 MB** (the board's heap lives in the 8 MB PSRAM —
  internal SRAM is consumed by framebuffers and is not the heap),
- the board manifest's frozen modules,
- SDL2 for video/input/audio.

Filesystem: `/flash` and `/sd` map to two host directories
(`~/.pc3emu/flash`, `~/.pc3emu/sd` by default, CLI-overridable). Bonus
over hardware: users can drag files in with the PC's file manager.

Windows: build the same tree under MSYS2/MinGW (SDL2 is packaged);
ship `pc3emu.exe` + `SDL2.dll` + a pre-seeded `flash/` folder. Linux:
native build, or an AppImage later.

## HDMI — the fidelity-critical part

All four modes must behave exactly as on hardware:

| Mode | Framebuffer | Format | Output |
|---|---|---|---|
| `RGB640` | 640×480 | RGB332, 8 bpp | native 640×480@60 scan |
| `RGB320` | 320×240 | RGB565 | pixel+line doubled → 640×480 |
| `RGB512` | 512×300 | RGB565 | pixel+line doubled → 1024×600 |
| `RGB1024` | 1024×600 | RGB121 4 bpp | 16-colour palette → RGB332 native |

Plan: introduce a **backend seam** in `hdmi.c`. Everything above the
seam (mode table, buffers, N/L/F targets, drawing, palette,
`hdmi_map256`, the module API) is shared source. Below the seam, two
implementations:

- `hdmi_backend_rp2` — the existing core1/HSTX/DMA code, moved but not
  rewritten (byte-identical behaviour on hardware).
- `hdmi_backend_sdl` — a host thread ticking at 60 Hz: composite the
  visible image exactly as the core1 fill-loop does (N, then the L
  layer merged over it honouring `hdmi_layer_transp`, per mode's pixel
  format — reusing the same conversion tables), upload to an SDL
  texture, present, then advance a `v_scanline`-equivalent so
  `hdmi.vsync()` blocks with hardware timing semantics.

Critical behaviours the backend must preserve (all live in shared code
already, listed as the acceptance checklist):

- `fb()` binds to the write target **at call time**.
- `create()`/`layer()` raise `ValueError` if the target exists;
  `close()` safe when absent.
- Layer merge is per-refresh with the transparent colour, not a copy.
- `vsync()` waits for the next frame boundary (60 Hz).
- Palette changes (`palette()`) take effect live (RGB1024).
- The mode's exact resolution/colour behaviour, including RGB320's
  big-pixel look — the SDL window scales integer-perfectly
  (640×480 content in a 640×480 or 1280×960 window, no smoothing).

## Input

- **Keyboard**: SDL scancodes are numerically the USB HID usage codes,
  so SDL key events feed the *firmware's own* keymap tables
  (`keyboard_maps.h`) and 6-slot rollover state. `keydown()`,
  `keyboard.on_key()`, layouts via `keymap("UK")`, modifier/lock
  bitmaps, and the drain-console-input behaviour all come from shared
  code.
- **Mouse** (`mouse` module) / **touch** (`touch`): SDL mouse events;
  touch maps to click-drag (or absolute events on a touchscreen
  laptop). `pccursor`/`pcgui` then work unchanged.

## Audio

`pcaudio.py` drives `machine.I2S(0)` in non-blocking IRQ mode. The
emulator provides an I2S lookalike with the same contract: `write()`
returns immediately, the drained-callback fires via the MicroPython
scheduler, format 16-bit stereo 44.1 kHz. Backing: SDL audio queue.
Everything audible — 4-voice synth, `tone`, `beep`, MOD tracker, WAV/
MP3/FLAC — is rendered by the portable `audio.c` and flows through
this one pipe. USB plug/unplug jingles included.

## Peripherals

- **DS3231** (`ds3231.py` speaks `machine.I2C`): emulate the chip at
  register level behind a fake I2C bus — time/date registers read from
  the **PC clock** (BCD, century bit, day-of-week as the chip computes
  it), alarm 1/2 registers writable, `A1F` latches when the alarm time
  matches, INT pin state readable via the fake `Pin(32)`. `set_alarm`/
  `alarm_fired`/`clear_alarm` then work identically. Setting the time
  adjusts an offset from the PC clock rather than the PC clock itself.
- **machine.Pin/ADC/PWM**: virtual pin table with sensible idle states
  (pull-ups read high, ADC reads a settable value). Phase 5 adds a
  small "pin panel" window: LED states shown, buttons clickable, a
  slider for the chapter-31 knob. `Pin("LED")` lights a dot in the
  window title bar area of the panel.
- **Wi-Fi / network**: unix port has real sockets+TLS; `pcnet`/`wifi()`
  facade reports connected with the host's addresses. NTP, `requests`,
  MQTT, the weather station all use the real network.
- **RTC epoch**: MicroPython's unix port uses the 1970 epoch where the
  board uses 2000. Where the frozen modules or book rely on epoch
  arithmetic, the emulated RTC layer compensates; noted as a
  compatibility item to verify with the chapter 28/32 programs.
- **xmodem / serial console**: not emulated (meaningless on a PC);
  `console("serial")` routes to the launching terminal's stdio, which
  is the natural analogue. `_sercon.mute` gets a stub honouring the
  same semantics.

## Differences that remain (documented, accepted)

- Raw CPU speed (~50× faster; `pcgame.Clock(vsync=True)` paces games
  correctly, but chapter 33's *numbers* differ).
- `@micropython.native`/`viper` compile to x86-64 (work, different
  ratios).
- Flash wear/size semantics; `gc.mem_free()` starts from 8 MB as on
  hardware but fragmentation behaviour differs.
- Boot time is instant; the USB enumeration messages are synthesised.

## Phases

1. **Terminal emulator** (no SDL): unix variant + frozen board modules
   + `machine`/`_sercon` shims + FS mapping + 8 MB heap + banner.
   REPL, shell, `fm`, `pye`, files, network work in a terminal.
   *Deliverable: `pc3emu --console`.*
2. **SDL video**: the `hdmi` backend seam + SDL window + screen
   console + keyboard. The machine boots to the banner in a window;
   turtle, sprites, tilemaps, games all render. The book's chapters
   3–27 pass.
3. **SDL audio**: the I2S lookalike; chapters 20/23–26 audible.
4. **Peripherals**: DS3231 (PC clock), mouse/touch, `pccursor`/`pcgui`
   verification, virtual pins; chapters 28–32 pass.
5. **Packaging**: MSYS2 Windows build + Linux build, seeded `flash/`
   with `lib/` + examples, release assets. Book appendix / README
   section ("no hardware yet? start here").

Acceptance test throughout: **the `book/examples/` tree** (157
programs) — the same corpus used to verify the book — plus the
on-device test suite (`tests/test_all.py`) where hardware-independent.

## Risks / open questions

- The unix port's `machine` module doesn't exist; the shim's surface
  must cover exactly what the frozen modules + book use (survey says:
  `Pin`, `ADC`, `PWM`, `I2C`, `I2S`, `Timer`, `RTC`, `freq`,
  `unique_id`, `reset`) — small, but `Timer`/`Pin.irq` need a POSIX
  timer/thread implementation with scheduler-safe callbacks.
- `hdmi.c` seam refactor must land without behaviour change on
  hardware — do it as its own commit, verified by Peter's next
  hardware pass (drawing code untouched; only the scanout moves).
- MSYS2 build of the unix port needs a trial early (phase 1) to avoid
  a Windows surprise at the end.
- Performance of the 60 Hz composite in C is trivial (< 1 ms/frame);
  no concerns.
