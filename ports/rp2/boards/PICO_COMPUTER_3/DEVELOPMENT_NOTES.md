# Pico Computer 3 — MicroPython port development notes

Progress log and design record for turning the MicroPython **rp2** port into a
stand-alone Python computer on the **Pico Computer 3** board. The end goal is an
editor, SD-card storage, and basic OS functionality, bringing HDMI and (later)
USB-host support across from the existing MMBasic (PicoMite) firmware.

## Board summary

- MCU: **RP2350B** (Cortex-M33, ARM; `PICO_RISCV=0`), QFN-80, 48 GPIOs.
  This board is **ARM-only**. The RP2350's alternate RISC-V (Hazard3) cores are
  not a supported target: the stock `mpconfigvariant_RISCV.cmake` that rode in
  when the board was cloned from `RPI_PICO2` has been removed, so there is a
  single build variant (`rp2350`, ARM) to reason about and validate.
- Base board definition: `pimoroni_pico_plus2_w_rp2350`.
- 16 MB external flash, 8 MB PSRAM (CS on GP47), CYW43 Wi-Fi/BT.
- SD card on **SPI1**: SCK=GP30, MOSI=GP31, MISO=GP28, CS=GP33.
- The same firmware also runs on the **Pico Computer 2** (no CYW43, LED on GP25,
  SD on GP29/30/31/32 bit-banged; the DS3231 is fitted but its 32 kHz output is
  not connected). Which board it is on is detected at start-up from that 32 kHz
  clock on GP27 — see §69 and the `board` module.
- Console UART on **UART1**: TX=GP8, RX=GP9.
- Flash filesystem (LittleFS2) sized to **12 MB** (`MICROPY_HW_FLASH_STORAGE_BYTES`
  set in `mpconfigboard.cmake` so the linker partition and the C value agree).

## Build & flash

Build runs in **WSL Ubuntu** (Windows 11 host). The editor/terminal tools run on
Windows, so build commands are wrapped with `wsl`:

```bash
wsl -d Ubuntu bash -lc "cd ~/src/micropython/ports/rp2 && make BOARD=PICO_COMPUTER_3"
```

Output firmware: `ports/rp2/build-PICO_COMPUTER_3/firmware.uf2`.

Flashing: hold **BOOTSEL** at power-up, the `RPI-RP2` drive appears, drop the
`.uf2`. (USB device mode is disabled in the app — see below — but the bootrom's
BOOTSEL USB is independent and still works.)

Approx. footprint after all changes: FLASH ~7.4 %, RAM ~44 % of 512 KB SRAM. The
RAM figure is dominated by the **153 KB HDMI framebuffer** (320×240 RGB565, must
be SRAM for scanout); before HDMI it was ~14 %. The MicroPython GC heap lives in
PSRAM (split heap), so this SRAM use doesn't reduce the Python heap. (pye adds
~150 KB of frozen *flash*; imported lazily, no RAM cost until `edit()`.)

---

## Driving the board over serial (automated testing)

The console is the **CH340 USB-serial adapter at 115200 baud** (UART1 on GP8/GP9;
it enumerates as a COM port on the host — `COM11` on this machine). USB device
mode is disabled in the app, so all scripted interaction goes over that UART.

`mpremote` is the natural tool (`mpremote connect COM11 run script.py`) but was
not installed here, so tests were driven with a small **pyserial** helper that
speaks MicroPython's **raw REPL** protocol directly:

1. `\r\x03\x03` — Ctrl-C twice to interrupt whatever is running.
2. `\x01` — Ctrl-A to enter raw REPL; wait for the `raw REPL; CTRL-B to exit`
   banner, then drain to the `>` prompt.
3. `reset_input_buffer()`, then send the whole script followed by `\x04`
   (Ctrl-D) to execute.
4. The reply is framed `OK` + *stdout* + `\x04` + *stderr* + `\x04` + `>`.
   Accumulate into **one** buffer until two `\x04` bytes have arrived, then split
   on them — do *not* wait for `OK` in a separate read first, or a fast program's
   `\x04` markers are consumed before the main read loop starts (a very fast
   program then looks like a timeout).
5. `\x02` — Ctrl-B back to the friendly REPL.

A soft reboot — Ctrl-C then `\x04` at the *friendly* prompt — restarts the runtime
and frees lazily-reserved pools (e.g. the `usqlite` dedicated SQLite heap), handy
for a clean baseline between memory tests.

Gotchas learned the hard way:

- **Ctrl-C cannot interrupt a long C call.** MicroPython only polls for
  KeyboardInterrupt between bytecodes, so a single `execute()` that runs a big
  SQLite sort / index build blocks the console until it returns — a slow query is
  indistinguishable from a hang. Use generous read timeouts, and reach for a
  physical reset only after ruling out "still working".
- When the raw-REPL framing gets out of sync (e.g. after a heavy run that
  overran its timeout), a friendly-REPL byte-dump (`print('ALIVE')`, read back)
  is a reliable liveness probe, and a soft reboot re-syncs without a power cycle.

---

## Changes

### 1. 64-bit integers and double-precision floats

- Integers: already arbitrary precision via `MICROPY_LONGINT_IMPL_MPZ` (default
  in the rp2 port) — 64-bit and beyond work transparently, no change needed.
  Small ints (≤31-bit on this 32-bit core) live inline in the object word; only
  larger values allocate an `mpz` bignum, so the common case is not slow.
- Floats: switched from single to **double precision**.
  - `mpconfigport.h`: made `MICROPY_FLOAT_IMPL` `#ifndef`-guarded so a board can
    override it (keeps the upstream default for other boards).
  - `boards/PICO_COMPUTER_3/mpconfigboard.h`: `MICROPY_FLOAT_IMPL_DOUBLE`.
  - RP2350 has hardware-accelerated `double` routines via the bootrom (pico-sdk
    wraps `__aeabi_dadd`/`__aeabi_dmul`); floats are now heap-boxed (a 64-bit
    double can't pack into the 32-bit object word).

### 2. Console on UART1 (GP8/GP9), USB fully disabled

- `mpconfigboard.h`: `MICROPY_HW_ENABLE_UART_REPL (1)`.
- `mpconfigboard.cmake`: the pico-sdk default UART must point at UART1/GP8/GP9.
  Because `setup_default_uart()` lives in pico-sdk source that never sees
  `mpconfigboard.h`, and the Pimoroni board header's `PICO_DEFAULT_UART*` are
  `#ifndef`-guarded, these are set as compile definitions (seen first):
  ```cmake
  add_compile_definitions(
      PICO_DEFAULT_UART=1
      PICO_DEFAULT_UART_TX_PIN=8
      PICO_DEFAULT_UART_RX_PIN=9
  )
  ```
- Console params: **115200 8N1**. stdout goes to the UART; the UART RX IRQ feeds
  the same stdin ring buffer the REPL reads.
- `mpconfigboard.h`: `MICROPY_HW_ENABLE_USBDEV (0)` — disables USB-CDC, MSC and
  `machine.USBDevice` (they cascade off this in `mpconfigport.h`). This frees the
  single USB controller for future USB-host use. (RP2350 has one USB controller,
  so host and device/CDC are mutually exclusive — the UART console is what makes
  USB-host bring-up possible later.)

USB-serial wiring for the console: adapter RX ← GP8, adapter TX → GP9, GND↔GND.

### 3. Pin reservation (protecting system pins)

The rp2 port already had a board-overridable `MICROPY_HW_PIN_RESERVED(i)` hook,
but it was only consulted in `machine_pin_deinit()` — nothing stopped user code
from grabbing a system pin.

- `machine_pin.c` (`mp_pin_make_new`): the constructor now raises
  `ValueError: Pin(n) is reserved` for non-ext reserved pins. Guarded by the same
  macro, so it's a no-op (`0`) on all other boards. Internal drivers configure
  their GPIOs directly and don't go through this constructor, so they're
  unaffected.
- `mpconfigboard.h`: reserved list currently covers the console UART and SD pins:
  ```c
  #define MICROPY_HW_PIN_RESERVED(i) \
      (mp_hal_is_pin_reserved(i) || (i) == 8 || (i) == 9 \
       || (i) == 28 || (i) == 30 || (i) == 31 || (i) == 33)
  ```
  Extend this line as PSRAM/HDMI pins are brought up.

Limitation: this protects the `Pin` constructor. A pin passed *by number* into
another peripheral (e.g. `UART(0, tx=8)`) resolves via `machine_pin_find()`, not
the constructor, so it isn't blocked. Airtight protection would move the check
into `machine_pin_find()` with a user-vs-internal flag — deferred for now.

### 4. Native SD card driver — `machine.SDCard`

Architecture chosen: **MicroPython-native**. The SD card is exposed as a block
device on MicroPython's existing `oofatfs` + `VfsFat` stack (not MMBasic's
FatFs). Only the SD-over-SPI protocol was taken from MMBasic; the filesystem is
MicroPython's, so `open()`, `import`, and `os` all work on the card.

New file `machine_sdcard.c`:
- SPI/SD protocol adapted from MMBasic `SDCard.c` (itself from ChaN's FatFs SPI
  sample): CRC7, `send_cmd`, `rcvr`/`xmit_datablock`, card identification,
  single- and multi-block read/write.
- Simplified for fixed hardware: pico-sdk `spi1` on the determinate pins, no
  function-pointer dispatch, no MMBasic `Option`/`Timer1` globals (uses pico-sdk
  `absolute_time` deadlines instead).
- Exposes the block-device protocol (`readblocks`/`writeblocks`/`ioctl`) plus
  `present()` and `info()`.
- Speeds: 400 kHz during identification, then 12 MHz
  (`MICROPY_HW_SD_SPI_BAUD_FAST`, board-overridable). 512-byte blocks, SPI mode 0.

Integration:
- `mpconfigport.h`: `MICROPY_PY_MACHINE_SDCARD` default `(0)`.
- `mpconfigboard.h`: enables it and defines `MICROPY_HW_SD_SPI_ID/SCK/MOSI/MISO/CS`.
- `modmachine.c`: registers `machine.SDCard` (conditional entry in the module dict).
- `CMakeLists.txt`: `machine_sdcard.c` added to both the source and QSTR lists.
- `boards/.../manifest.py`: dropped `require("sdcard")` — the pure-Python driver
  is replaced by the native one (and it also resolved a `MP_QSTR_SDCard` clash).

Note: an rp2 board-cmake `set(MICROPY_PY_...)` is **inert** unless turned into a
`-D` via `target_compile_definitions`; the real switch is the C `#define` in the
board header.

**Critical hardware detail:** MISO needs a pull-up. Without it the line floats
low between transfers / during identification, `wait_ready()` never sees `0xFF`,
and init times out (~500 ms) reporting no card. The driver now sets
`gpio_pull_up(MISO)` plus 8 mA drive strength and input hysteresis, matching
MMBasic's setup.

Manual use:
```python
import machine, os
sd = machine.SDCard()
sd.present()          # True if initialised
sd.info()             # (capacity_bytes, 512)
os.mount(os.VfsFat(sd), "/sd")
# blank card, once: os.VfsFat.mkfs(sd)
```

### 5. SD auto-mount at boot

`modules/_boot.py`, after the flash root is mounted:
```python
if hasattr(machine, "SDCard"):
    try:
        _sd = machine.SDCard()
        if _sd.present():
            vfs.mount(vfs.VfsFat(_sd), "/sd")
        del _sd
    except Exception:
        pass
```
- `hasattr` guard makes it inert on boards without the native driver.
- Only mounts if a card actually initialised; `try/except` means a missing,
  unformatted, or faulty card never blocks boot.
- Does **not** auto-`mkfs` (that could wipe a card that merely failed to mount).
- No hot-plug detect line is wired, so a card inserted after boot needs a manual
  mount.

### 6b. System clock at 252 MHz

Startup clock raised from the pico-sdk default (150 MHz) to **252 MHz** for HDMI
headroom (HSTX is clocked from `clk_sys`; 378 MHz is the other planned option).

- `main.c`: new board-overridable `MICROPY_HW_CLK_SYS_KHZ` (default `SYS_CLK_KHZ`).
  Startup now pre-sets the flash (QMI) divider for the *target* frequency
  **before** the PLL switch, then raises the clock, then reasserts flash timing
  (`set_sys_clock` can rewrite the QSPI pads). Ordering matches MMBasic.
- `mpconfigboard.h`: `MICROPY_HW_CLK_SYS_KHZ = 252000`.
- **No vreg change**: DVDD is supplied by an external 1.3 V regulator on this
  board, so `vreg_set_voltage()` is unnecessary.
- `clk_peri` and `clk_hstx` are sourced from `clk_sys`, so they track it to
  252 MHz automatically. The UART console and SD SPI are initialised after the
  clock change, so their dividers are computed against the final `clk_peri`.
- **Flash**: `MICROPY_HW_FLASH_MAX_FREQ` set to **63 MHz** in the board header.
  The QMI divisor is `ceil(clk_sys / MAX_FREQ)` and RXDELAY (a 3-bit field, max 7)
  is set equal to the divisor. The default cap (`SYS_CLK_HZ/4` = 37.5 MHz) would
  give divisor/RXDELAY = 7 at 252 (just fits) but 11 at 378 (overflows the field
  and corrupts timing). 63 MHz gives divisor 4 @252 and 6 @378 — both valid, and
  matches the flash speed MMBasic uses on this board.
- **PSRAM**: `rp2_psram.c` already derives its timing from live `clk_sys` and has
  explicit handling for `clk_sys > 200 MHz` (divisor/rxdelay bumps), so it needed
  no change — divisor 2 @252, 3 @378.

Verify at the REPL: `machine.freq()` → `252000000`. `machine.freq(378000000)` also
works at runtime (flash cap and PSRAM init both handle it).

### 6. REPL convenience imports

`_boot.py` runs as a frozen *module*, so its imports don't reach the REPL, which
lives in `__main__`. To pre-populate the prompt we assign onto `__main__`
directly (its `.globals` is `MP_STATE_VM(dict_main)`, the same dict the REPL
uses):
```python
if "PICO COMPUTER 3" in os.uname().machine:
    import __main__
    import pcshell
    __main__.os = os
    __main__.machine = machine
    __main__.Pin = machine.Pin
    for _name in pcshell.COMMANDS:          # ls, run, edit, pwd, cd, mkdir, ...
        setattr(__main__, _name, getattr(pcshell, _name))
```
Gated by board name so other rp2 boards keep standard behavior. `os`, `machine`,
`Pin` and the whole shell command set are available at the prompt with no import.
They persist until soft-reset (Ctrl-D), after which `_boot.py` re-runs and
re-injects them.

### 7. REPL banner tidy-up

Banner changed from
`MicroPython v1.29.0-preview.450.g562d6be365.dirty on 2026-07-02; PICO COMPUTER 3 with RP2350`
to `MicroPython v1.29.0 on PICO COMPUTER 3 with RP2350B`.

- `mpconfigboard.h`: `MICROPY_HW_MCU_NAME "RP2350B"`;
  `MICROPY_BANNER_NAME_AND_VERSION "MicroPython v" MICROPY_VERSION_STRING`
  (drops the git hash / date; the version itself comes from upstream's defines,
  so it read `1.29.0-preview` until this branch merged the released v1.29.0 tag
  and `1.29.0` after); `MICROPY_BANNER_MACHINE_SEP " on "`.
- `mpconfigport.h`: `#ifndef`-guard the MCU name so the board can override it.
- `py/mpconfig.h`: added a `MICROPY_BANNER_MACHINE_SEP` default (`"; "`); used in
  `shared/runtime/pyexec.c`. So only this board opts into `" on "`; every other
  board keeps the standard banner.
- Side effect (intended): `os.uname().machine` / `sys.implementation._machine`
  now report `PICO COMPUTER 3 with RP2350B` (the `_boot` auto-import gate matches
  on the `"PICO COMPUTER 3"` substring, so still fires).

### 8. Shell layer, editor and program launcher

A small frozen module `pcshell.py` provides unix-style helpers, injected into the
REPL (see §6). Files handled through MicroPython's VFS (flash + `/sd`).

- **Directory / files:** `pwd()`, `cd(path="/")`, `ls(path=None)` (dirs first,
  then files A–Z, with size and mtime), `mkdir`, `rmdir`, `rm` (file), `cat`
  (print a text file), `cp(src,dst)`, `mv(src,dst)`.
- **`cls()`** — clear the console screen (ANSI home + clear, `\x1b[H\x1b[2J`,
  same sequence `cat`'s pager uses; MMBasic's `CLS`). Works on whichever
  console(s) output is routed to.
  - `cp`/`mv`: if dst is an existing directory the file lands inside it.
  - `mv` across filesystems (SD ↔ flash) falls back to copy+remove, since
    `os.rename` only works within one filesystem.
  - Errors surface as normal Python `OSError` (kept Pythonic, not swallowed).
  - Extend by writing the function and adding its name to `pcshell.COMMANDS`.
- **`run(path)`** — launch a `.py` from anywhere in a *fresh* namespace
  (`__name__ == "__main__"`, cwd temporarily set to the program's folder and
  restored after). Program can't clobber REPL globals. Source is compiled
  with `compile(src, path, "exec")` so tracebacks name the actual file
  (`File "prog.py", line N`) rather than `<string>` — essential for the
  edit/run/fix loop.
- **`edit(*args)`** — full-screen editor. Lazily imports **pye**
  (robert-hh/Micropython-Editor, **MIT**, `V2.79`) which is vendored verbatim as
  `boards/PICO_COMPUTER_3/pye.py` and frozen via `manifest.py`. Works over the
  UART VT100 console now (and will keep working when HDMI+USB-keyboard become the
  console, since it only uses `sys.stdin`/`sys.stdout`). `edit("/sd/x.py")` opens
  or creates a file; **Ctrl-S** save, **Ctrl-Q**/Esc quit, **Ctrl-F** find.
  - **Local patches** to `pye.py` (each marked with a `# local:` comment;
    provenance sha256 `17867248…` of the upstream `master/pye.py`): (1) remapped
    `"\x08"` (Ctrl-H) from Replace to Backspace, so terminals that send Ctrl-H
    for Backspace work (DEL `0x7F` was already mapped; Replace stays on Ctrl-R);
    (2) a lightweight Python **syntax highlighter** for `.py` files (§38).

### 9. core1 dedicated to HDMI (threads disabled)

`mpconfigboard.h`: `MICROPY_PY_THREAD (0)`. The rp2 port only ever launches core1
for `_thread`; disabling it (and never registering the HDMI core1 as a multicore
lockout victim) means the HDMI scanout has sole, uninterrupted use of core1. The
flash erase/write lockout path (`use_multicore_lockout()`) is gated on threads, so
with threads off and core1 not a registered victim, **core0 flash writes never
pause the scanout** — provided the scanout code is RAM-resident and the
framebuffer/line-buffers are in SRAM (both true here). GC's stop-the-other-core
path is also gone. See the core1 discussion for the full mechanics.

### 10. HDMI — WORKING (dual-mode, 640×480@60 DVI)

`hdmi.c` + `hdmi` module: 640×480@60 DVI via the RP2350 **HSTX hardware TMDS
encoder** + ping-pong command DMA on **core1**. Adapted from MMBasic
`graphics/HDMI.c`; distilled to two modes, single framebuffer, no layers/tiles.
The framebuffer is a static SRAM array sized for the largest mode (640×480×8 =
307,200 bytes) — hence RAM ~74 %.

Two modes selected at `init()`:
- **`hdmi.RGB332`** — 640×480 RGB332 (8bpp), **native** resolution and native HSTX
  format, so DMA scans the framebuffer directly (no doubling, no fill-loop). 256
  colours, crisp 80×60 text with an 8×8 font. **Boot default.**
- **`hdmi.RGB565`** — 320×240 RGB565, H-doubled by the core1 fill-loop, V-doubled
  by DMA (source line = active/2). 65536 colours, 40×30 text.

Details:
- **Ownership / core1:** `multicore_launch_core1_with_stack`, own 4 KB stack
  (`0xf00dbeef` sentinel, `hdmi.stack_ok()`), not a lockout victim (§9).
- **DMA IRQ line 1:** `DMA_IRQ_0` has MicroPython's shared `rp2.DMA` handler, so
  an exclusive handler there hard-asserts. HDMI uses `DMA_IRQ_1` exclusively
  (clashes with `machine.I2S(id=1)` if used later).
- **Clock:** `clk_hstx = clk_sys/2 = 126 MHz` → pixel 25.2 MHz → ~60 Hz (both modes).
- **Encoders** (NBITS field = bits−1; both confirmed by colour-bar test):
  - RGB565: L0 blue rot29/4, L1 green rot3/5, L2 red rot8/4; `expand_shift` 2×16bit.
  - RGB332: L2 red rot0/3, L1 green rot29/3, L0 blue rot26/2 (verbatim from
    MMBasic non-FullColour); `expand_shift` 4×8bit.
- **Pins:** `MICROPY_HW_HDMI_CLK/D0/D1/D2 = 1/3/5/7` (HSTX bit; bit N → GP12+N;
  positive on the odd bit, negative on bit−1). GP12–19 reserved from `machine.Pin`.
- **`deinit()`** stops the scanout (halt core1 via `multicore_reset_core1`; break
  both DMA chains via the non-triggering `al1_ctrl` alias, then abort both
  together with bounded waits; stop HSTX) so `init(other_mode)` can relaunch.
  Claimed DMA channels are kept. Fresh core1 vector on relaunch, so the exclusive
  handler re-registers cleanly.
- **Python API:** `hdmi.init(mode=RGB565)`, `hdmi.deinit()`, `hdmi.fill(colour)`,
  `hdmi.framebuffer()` (writable memoryview alias, mode-sized — see §32), `hdmi.width()`,
  `hdmi.height()`, `hdmi.stack_ok()`, `hdmi.RGB332/RGB565`, and **`hdmi.fb()`**
  which returns a ready-made **`pcgfx.Display`** at the current geometry/format
  (built from C via `mp_import_name`/`mp_call_function`).
- **Framebuffer memory:** static SRAM (DMA can't scan PSRAM; the GC heap is in
  PSRAM so a heap buffer wouldn't be SRAM). 307 KB committed for the firmware's
  life regardless of mode. Nothing to free; `deinit()` releases no memory.

### 11. Graphics helper — `pcgfx.Display` + colour palette

Frozen board module `boards/PICO_COMPUTER_3/pcgfx.py`:
- **`Display(framebuf.FrameBuffer)`** — subclass adding `colour(r,g,b)` /
  `colour(0xRRGGBB)` (+ `color` alias) that converts RGB888 → the framebuffer's
  real format (RGB332 or RGB565, from the stored `fmt`). So drawing code is
  resolution-agnostic: `fb.text(s, x, y, fb.colour(RED))` works in either mode.
- **Palette** — the MMBasic 16-colour set (`WHITE`, `YELLOW`, `LILAC`, `BROWN`,
  `FUCHSIA`, `RUST`, `MAGENTA`, `RED`, `CYAN`, `GREEN`, `CERULEAN`, `MIDGREEN`,
  `COBALT`, `MYRTLE`, `BLUE`, `BLACK`) + `GRAY`/`LITEGRAY`/`ORANGE`/`PINK`/`GOLD`/
  `SALMON`, as 24-bit RGB888.
- `_boot.py` (this board) auto-`hdmi.init(hdmi.RGB332)` at boot, injects `hdmi`,
  `framebuf`, `Display`, and the palette (all-uppercase names) into `__main__`.
  Note: palette globals live in `__main__` (REPL); `run()`-launched scripts should
  `from pcgfx import *`.

### 12. On-screen text console — WORKING

Frozen board module `pcconsole.py` + C helpers in `hdmi.c`. `console()` mirrors
the REPL to the HDMI screen via `os.dupterm`; **auto-started at boot** so the
banner/prompt appear on the monitor. Input still comes from the UART (USB-host
keyboard is the next step) — the console is output-only (`readinto` → `None`).

- **`Console(io.IOBase)`** — must subclass `io.IOBase` for `os.dupterm` to accept
  it as a stream (routes the C stream protocol to Python `write`/`readinto`).
- **8×12 font, 80×40.** MMBasic `font1` vendored as `console_font.h`; blitted by
  **`hdmi.putc(px,py,ch,fg,bg)`** (C, opaque — fg where set, bg elsewhere).
  framebuf's `text` is 8×8-only, so we don't use it.
- **ANSI/CSI interpreter** — honours the sequences the REPL line editor emits:
  `\x1b[nD`/`\x1b[nC` (cursor back/forward, needed for **backspace** & arrows),
  `\x1b[K` (erase to EOL), plus `A/B` (up/down), `H`/`f` (position), `J` (erase
  display), `r` (DECSTBM scroll region) and `ESC M` (reverse index) — the last
  two added in §37 so pye's full-screen scrolling renders correctly; `m` (SGR
  colour) added in §38. Device queries (`n`), private (`?…`) are ignored.
- **Blinking underline cursor** (like MMBasic) — `machine.Timer` toggles a
  bottom-row underline ~1 Hz. `write()` erases it before rendering and redraws
  after (and an `_in_write` guard), so it's never left behind or scrolled.
- **Fast scroll** — framebuf's `scroll` is a per-pixel double loop (catastrophic
  for 307 KB). Replaced with **`hdmi.scroll(rows, colour)`** (C): a single bulk
  `memmove` of `(height−rows)` rows + `memset`/clear of the freed rows.
- **Terminal sync** — `sync_terminal()` sends xterm `CSI 8;rows;cols t`
  (`\x1b[8;40;80t`) at boot and on `console()`, resizing the serial terminal to
  the screen's 80×40 grid (as MMBasic does entering its editor).
- **API:** `console(on=True, fg=0xFFFFFF, bg=0)` (RGB888), injected into `__main__`.
  `console(False)` detaches and stops the blink timer. After a resolution swap,
  re-run `console()` to rebind. Targets: `"both"`/`"screen"`/`"serial"`/
  `"none"` — `"none"` (added for full-screen graphics: kills the blinking
  cursor over animations) tears down the screen console *and* mutes
  `_sercon`, output nowhere, input unaffected; not persisted, so RESET
  recovers. Full-screen programs pair it with `finally: console()`.

### 13. USB host (TinyUSB) — enumeration + HID reports WORKING

The RP2350's USB controller runs in **host mode** (freed by disabling USB-device).
Built up in stages; enumeration and raw HID reports confirmed on mouse,
touchscreen, and a composite hub-keyboard.

- **Config flip (not a vendored file):** `shared/tinyusb/tusb_config.h` gains a
  `#if MICROPY_HW_USB_HOST` block — `CFG_TUSB_RHPORT0_MODE = OPT_MODE_HOST`,
  `CFG_TUH_ENABLED`, native controller (`CFG_TUH_RPI_PIO_USB 0`). The device glue
  (`mp_usbd.c`/`usbd.c`/`msc_disk.c`) is all `#if MICROPY_HW_ENABLE_USBDEV`, so
  it's inert. `CFG_TUSB_OS` defaults to `OPT_OS_NONE` (polling) since the pico-sdk
  only forces `OPT_OS_PICO` on `tinyusb_device`, and we link `tinyusb_host`.
- **CMake:** `CMakeLists.txt` links `tinyusb_host` vs `tinyusb_device` on the
  board's `MICROPY_HW_USB_HOST` cmake flag; adds `mp_usbh.c`.
- **`mp_usbh.c`:** `tuh_init(0)` at startup (`main.c`), callbacks for mount/unmount
  and HID mount/report/unmount (currently diagnostic hex dumps).
- **Pumping `tuh_task()`:** from `MICROPY_INTERNAL_EVENT_HOOK` (→ `mp_usbh_task`),
  which runs in **thread context** during any `mp_event_wait_*` (never in an IRQ —
  TinyUSB polling must not be pumped from an ISR). A 1 ms repeating timer
  (`add_repeating_timer_us`, empty callback) just **wakes WFE** so the hook keeps
  pumping through enumeration's timed steps (which generate no IRQ of their own).
- **Two config gotchas that mattered** (both matched to MMBasic):
  `CFG_TUH_ENUMERATION_BUFSIZE = 1024` — a composite/hub device's config
  descriptor exceeds 256 and enumeration aborts silently (simple devices have
  tiny descriptors, so they worked); `CFG_TUH_HUB = 2` (with
  `CFG_TUH_DEVICE_MAX = 3*HUB+1`) for devices with a built-in hub.
- **HID:** `CFG_TUH_HID = 4*CFG_TUH_DEVICE_MAX`. `tuh_hid_mount_cb` must call
  `tuh_hid_receive_report()` to start the flow, and `tuh_hid_report_received_cb`
  must **re-arm** it after every report (else reports stop).

### 14. USB keyboard → REPL — WORKING (standalone input)

`mp_usbh.c` keyboard driver: HID boot-keyboard reports → keystrokes into
`stdin_ringbuf` (the same buffer the UART IRQ feeds), so a USB keyboard drives
the REPL/console — the board is now self-contained.

- US-layout HID-usage→ASCII table (unshifted/shifted); **shift/ctrl/caps** handled
  (Ctrl-A..Z → 0x01..0x1a; caps affects letters only).
- **Special keys → VT100 escape sequences** (arrows `\x1b[A/B/C/D`, Home/End,
  Ins/Del `\x1b[2~`/`3~`, PgUp/Dn) so readline and pye navigation work.
- **Ctrl-C** → `mp_sched_keyboard_interrupt()` (matches the UART path).
- **Auto-repeat** (400 ms delay / 40 ms rate) synthesised from the held key —
  HID only reports on change — checked in `mp_usbh_task` (thread context).
- Forces **boot protocol** on keyboard mount for a reliable 8-byte report.
- Only keyboard-protocol interfaces feed stdin; mouse/gamepad/touch reports are
  ignored for now (to be handled later).

Selectable **layout** via a `keyboard` C module: `keymap("UK")` / `keymap()` /
`keymaps()`, injected into `__main__`. The six MMBasic tables (US/UK/DE/FR/ES/BE)
are vendored verbatim as `keyboard_maps.h` (`table[usage*2 + shift]`); the decoder
indexes the active layout. Module lives in `usb_keyboard.c` (not `mp_usbh.c`)
because it needs QSTR scanning, which can't see `tusb.h`; they share the active
`kbd_layout` pointer via `extern`. Switch is live; **resets to US on reboot** (no
persistence yet).

Next: mouse/gamepad/touch handling; caps/num-lock LEDs; USB-MSC (drives);
persist the keymap choice.

### 15. Editing-key fixes (Delete / Backspace)

- **Delete deleted backwards.** The vendored layout tables hold Delete-forward
  (usage 0x4c) as `0x7f`, which both readline and pye read as **backspace**. The
  table lookup ran before the special-key switch, so Delete never reached its
  VT100 case. Fix: the caps/nav cluster (`0x39`, `0x49..0x52`) now **bypasses the
  table** and emits its escape sequence — Delete → `\x1b[3~` (forward delete).
- **Delete over a serial terminal.** TeraTerm (Backspace = `0x08`) sends a lone
  `0x7f` for its Delete key, which readline reads as backspace. The UART RX IRQ
  (`uart.c`) now translates `0x7f` → `\x1b[3~`, gated by board macro
  `MICROPY_HW_UART_REPL_DEL_FORWARD` (stock rp2 boards, where Backspace may be
  `0x7f`, are unaffected). The USB keyboard never emits `0x7f`, so this is
  unambiguous here.

### 16. Audio — WAV / MP3 / FLAC over the PCM5102 I2S DAC — WORKING

Pins **BCLK=GP10, LRCK=GP11 (=BCLK+1), DIN=GP22** (SCK grounded → internal PLL).
Driven through **`machine.I2S(0)`**, which runs on **DMA_IRQ_0** (shared with
`rp2.DMA`) and so never clashes with HDMI's exclusive DMA_IRQ_1. Left
**unreserved** — I2S claims the pins via `Pin()`.

- **Background / non-blocking playback.** `i2s.irq()` puts I2S in non-blocking
  mode; `write()` returns immediately and a **scheduler callback** queues each
  next chunk, so the REPL stays live (MMBasic-style). Single-buffer reuse is safe
  because the callback fires only once the previous buffer is fully consumed.
- **Python front-end** `pcaudio.py`: `play(path)` dispatches by extension
  (`.wav`/`.mp3`/`.flac`), `stop()` (hard — deinits I2S so no buffered tail),
  `volume(0..100)` (≈50 dB **log taper**, applied in C by `audio.scale`),
  `beep()`, `is_playing()`. `play_wav` kept as an alias.
- **Decoders** (single-header, streamed via `mp_stream` read/seek/tell callbacks
  over a Python file — no loading whole files into RAM): **dr_wav** vendored from
  MMBasic `third_party_mod`; **dr_mp3 / dr_flac** are the **stock** headers from
  github.com/mackron/dr_libs (MMBasic's copies are patched with
  `GetMemory`/`Memory.h` and won't build). C glue in `audio.c`; each impl in its
  own TU (`dr_wav.c`/`dr_mp3.c`/`dr_flac.c`).
- **Two critical gotchas:**
  1. **Allocator must be the GC heap (PSRAM), not the C heap.** dr_* default
     `malloc` hits the tiny pico C heap, whose wrapper **panics** ("Out of
     memory"). Routed to `m_malloc_maybe`. But dr_mp3/dr_flac keep **persistent**
     buffers across reads, and a static C pointer isn't a GC root → the collector
     would reclaim them mid-song. So live allocations are held in a rooted table
     (`MP_REGISTER_ROOT_POINTER(audio_allocs[16])`). dr_wav's buffers are
     transient (stack-referenced), so it was fine before — the table covers all.
  2. **Seek must map all three origins SET/CUR/END.** dr_mp3/dr_flac seek to
     **END** during init to size the stream; collapsing END→SET makes init fail
     on VBR / MPEG-2 files (e.g. a 22050 Hz MP3) with "not a valid …". dr_wav has
     only two origins (start/current) so was unaffected.
- Verified: WAV (incl. 8-bit stereo), MP3 (incl. MPEG-2 VBR), FLAC all play; no
  memory leak across repeats.

Next (deferred): USB connect/disconnect sounds (`Connect.h`/`Remove.h` = 8-bit
8 kHz WAV).

### 17. DS3231 hardware RTC

`ds3231.py` (frozen) on **I2C0 GP20=SDA / GP21=SCL**, addr `0x68`, BCD registers,
24-hour (12-hour-aware read), weekday derived from the date.
`settime(y,mo,d,h,mi,s)` sets chip **and** system clock; `settime()` copies the
current system clock to the chip; `gettime()` reads it; `synctime()` reads → sets
the system clock. `_boot` calls `synctime()` in a `try/except` so the clock is
right at boot and a missing chip/dead battery never blocks boot. Pins left
unreserved (I2C claims them via `Pin()`).

**Clock-change robustness:** `machine.I2C`'s baud divider is derived from the
peripheral clock (which tracks `clk_sys`) at bus creation, so a **live** CPU
clock change — `screen(mode, 315/378)` — would leave a cached bus clocking SCL
too fast for the DS3231's 400 kHz limit (400 kHz × 378/252 ≈ 600 kHz),
corrupting reads. `_bus()` records `machine.freq()` at creation and re-creates
the bus whenever the clock differs, so `gettime`/`settime` stay correct across
a clock switch. (The C-side clock switch already re-times UART/PSRAM/cyw43 in
`hdmi_set_clock`; this covers the one board-managed peripheral built in Python.
A user's own `machine.I2C`/`SPI`/`PWM` created before a live clock change has
the same limitation and would need re-creating — inherent to live clock
switching.)

- **Daily alarm (Alarm 1) on INT/GP32** (added for the book, v0.8):
  `set_alarm(h, m, s=0)` writes regs 0x07–0x0A BCD with `A1M4=1`
  (day-masked → daily), then read-modify-writes control 0x0E to set
  `INTCN|A1IE` (RS bits preserved); `alarm_fired()`/`clear_alarm()` read/
  clear `A1F` in status 0x0F (A2F preserved); `alarm_off()` clears `A1IE`;
  `alarm_pin()` returns `Pin(32, IN, PULL_UP)` (INT is open-drain active
  low). Because the alarm lives in the battery-backed chip it survives
  resets — poll the flag or `.irq()` the pin. Host-tested against a fake
  I2C at register level (BCD encoding, mask bits, flag preservation).

### 18. cyw43 Wi-Fi — gSPI PIO clock divider must scale with clk_sys

Wi-Fi broke after the boot clock rose to 252 MHz ("hdr mismatch" / ioctl
timeout). The SDK's cyw43 gSPI PIO divider is a **fixed** default of 2, tuned for
~125 MHz; at 252 MHz it doubles SCK out of the cyw43439's range. Fix (MMBasic's
algorithm, proven 48–396 MHz): enable `CYW43_PIO_CLOCK_DIV_DYNAMIC=1` (board
cmake) and, in `main.c` **before `cyw43_init`**, set
`div = ceil(clk_sys_khz / 100000)` (min 2) via `cyw43_set_pio_clkdiv_int_frac8`
— SCK = clk_sys/(2·div); 252 MHz → div 3 → 42 MHz. (This was a PIO *clock* issue,
not a PIO-instance clash with I2S.) When a **live** clk_sys switch is added, this
must re-run *and* re-tune the running cyw43 PIO SMs (`pio_sm_set_clkdiv…`+restart),
not just the stored value.

### 19. HDMI live clock switch (252/315/378 MHz)

`hdmi.init(mode, clock)` — clock ∈ {252, 315, 378} MHz. `hdmi_set_clock()` (core0,
before core1 launches) reuses **`machine.freq()`'s proven sequence** — flash
pre/post-timing, `set_sys_clock_khz`, `setup_default_uart`/`mp_uart_init` (baud
re-derive, since clk_peri follows clk_sys) and **`psram_init()`** (re-times the
PSRAM QMI window — our GC heap lives there) — plus the cyw43 stored-divider
update. Then core1 sets clk_hstx to keep a valid pixel clock:
`378 → clk_sys×332/1000` (25.1 MHz, 60 Hz), `252/315 → clk_sys/2`
(25.2/31.5 MHz = 60/75 Hz). Verified 252↔378 with a 2 MB PSRAM heap array
intact both ways and the UART console surviving. (Wi-Fi *across* a switch still
needs the live cyw43 PIO-SM retune — for now bring it up after a switch.)

### 20. HDMI modes renamed + RGB512 / 1024x600 — WORKING

Modes renamed by resolution: **`hdmi.RGB640`** (640×480×8, native, was RGB332),
**`hdmi.RGB320`** (320×240×16 doubled, was RGB565), and new **`hdmi.RGB512`**
(512×300×16 → doubled to **1024×600**).

- RGB512 uses the 1024×600 timing (MMBasic Screens.h "X"), the **RGB565
  "fullcolour" expand config** (16-bit — *not* MMBasic's 8-bit 1024×600),
  `clk_hstx = clk_sys` (÷1 → 50.4 MHz pixel), forced to 252 MHz.
- **Key:** MMBasic emits **fixed negative-sync command lists for every mode**
  (the `MODE_*_SYNC_POLARITY` metadata never reaches the HSTX output), so
  1024×600's positive H-sync is *not* special-cased — the command-list structure
  is identical to 640×480, only the H/V constants differ.
- **Per-resolution hot path:** the per-scanline DMA IRQ is hardcoded per
  resolution — a **separate `hdmi_dma_irq_x`** with 1024×600 constants
  compile-time (parameterising the IRQ is too slow). The fill-loop branches once
  on mode then runs a hardcoded 512→1024 (or 320→640) inner loop. core1 installs
  the correct handler.
- **Bandwidth result:** clean, stable 1024×600 **even while playing an MP3** —
  HDMI DMA (IRQ_1) + I2S DMA (IRQ_0) + core1 fill (512→1024/line) + MP3 decode
  all coexist. No fallback to 8-bit needed.
- **Gotcha fixed:** `hdmi.deinit()` now `irq_remove_handler(DMA_IRQ_1)` —
  `irq_set_exclusive_handler` hard-asserts if a *different* handler is already
  installed (switching between the 640×480 IRQ and `hdmi_dma_irq_x`).

### 21. Images — JPEG / BMP / PNG load + BMP save

All decode straight into the HDMI framebuffer (RGB565 for RGB320/RGB512, RGB332
for RGB640, packed 4bpp nearest-palette for RGB1024; the loaders switch on
`hdmi.bpp()` — see §29). Vendored decoders, MMBasic-derived:

- **`draw_jpg(path, x, y, scale)`** — vendored **picojpeg** (unmodified) + `jpeg.c`
  adapting MMBasic's `cmd_LoadJPGImage`: MCU-row decode, **binning downscale**
  (scale 1/2/4/8, averaged), no dithering. picojpeg's 9 work buffers are static
  (~2 KB); only the per-image MCU-row buffer is PSRAM (freed after). Baseline
  JPEG only (progressive unsupported).
- **`draw_bmp(path, x, y)`** — vendored **BmpDecoder.c** *decode engine* (display
  half dropped, truncated at `decodeBMP`) as `bmp_decoder.c`. Handles every
  variant: 1/4/8/16/24-bit, RLE4/RLE8, BI_BITFIELDS, V4/V5 headers. Its per-line
  `linecallback` is pointed at a framebuffer blit in `bmp.c`; `decodeBMP(false)`
  (sequential read; `screenRow` is already the display row).
- **`draw_png(path, x, y, cutoff)`** — vendored **upng** (`upng.c`) + `png.c`. PNG
  read in Python, handed to `upng_new_from_bytes`; RGB8/RGBA8 → framebuffer with
  **alpha-cutoff transparency** (alpha ≤ cutoff leaves the pixel). Non-interlaced
  RGB8/RGBA8 only (indexed/grey/16-bit rejected with a clear error).
- **`save_image(path)`** — `bmp.c` writes the framebuffer to a **24-bit** BMP
  (full colour, not MMBasic's 16-colour SAVE IMAGE).

Two reused patterns from the audio decoders: MMBasic's `GetMemory`/`FreeMemorySafe`
become **PSRAM `m_malloc`/`m_free` shims**, and `error()`→a raised `ValueError`.
GC safety differs per decoder: picojpeg/BMP hold their PSRAM buffer in a C stack
local (scanned); upng holds everything inside the GC-scanned `upng_t` (reachable
from the stack) — so none get reclaimed mid-decode. File I/O is the mp_stream
protocol on a Python file (or, for PNG, the whole file as `bytes`).

### 22. Persistent settings (keymap + HDMI mode/clock)

`pcconfig.py` stores settings as JSON in **`/settings.json`** on the flash FS.
`_boot` reads it and applies the saved **keyboard layout** and **HDMI mode +
clock** at start-up (defaults: US, RGB640 @ 252 MHz), all in try/except so a
missing/corrupt file or a failing mode falls back to 640×480 and boots normally.

- **`keymap("UK")`** — now a `pcconfig` wrapper that applies (via the C
  `keyboard` module) *and* persists; `keymaps()` still just lists.
- **`screen(mode, clock)`** — new command: `hdmi.deinit()`+`hdmi.init(mode,
  clock)`, persists both, and re-runs `console()` at the new geometry.
  `screen()` returns the saved `(mode, clock)`.

Raw `hdmi.init()`/`keyboard.keymap()` still work for one-off (non-persisted)
changes. Recovery from a monitor-incompatible saved mode: the UART console
survives, so `screen(hdmi.RGB640)` (or `rm("/settings.json")`) resets it.

---

### 23. Hot-swappable SD card (replicates MMBasic CheckSDCard)

Cards can now be inserted/removed while running. This mirrors MMBasic's
`CheckSDCard` (FileIO.c): a background poll, ~2×/second, that verifies the card
is still present and clears down the mount if it has been pulled.

- **Liveness probe (C, `machine_sdcard.c`):** `SDCard.check()` issues a
  **READ_OCR (CMD58)** and discards the OCR — the exact lightweight,
  non-destructive test MMBasic uses (`disk_ioctl(MMC_GET_OCR)`). The MISO
  pull-up makes it return fast when the card has been removed.
- **Deferred by activity:** every `readblocks`/`writeblocks` stamps
  `sd_last_activity = mp_hal_ticks_ms()`. `check()` skips the probe (reports
  "present") while the card was used within **500 ms** — the direct analog of
  MMBasic resetting `diskchecktimer` to `DISKCHECKRATE` on every access, so the
  poll never interrupts an active transfer (e.g. FLAC streaming off SD).
- **On removal:** `check()` marks the card `STA_NOINIT` and returns False;
  `pcsd` unmounts `/sd` and prints `Warning: SDcard removed`.
- **On insertion:** while unmounted, the poll calls `SDCard.reinit()`
  (re-runs card identification) and, on success, mounts `/sd` and prints
  `SDcard inserted`. (MMBasic re-mounts lazily on next access via `InitSDCard`;
  we fold insertion into the same poll, which is cleaner in MicroPython's VFS.)

**Driver:** `pcsd.py` owns the `machine.SDCard`, the `/sd` mount and a soft
`machine.Timer` (period 500 ms). The Timer callback is *scheduled* (not a hard
IRQ), so mount/umount/print from it are safe. `_boot` calls `pcsd.start()` for
this board and the generic `_boot` SD auto-mount is skipped on the Pico
Computer 3. `pcsd.verbose = False` silences the notices; `pcsd.stop()` halts the
poll.

---

### 24. USB plug-in / unplug sounds

A short sound plays whenever a USB device is connected or removed (vendored from
MMBasic: `ezyZip_wav` plug-in, `remove_wav` unplug — both 8-bit unsigned mono
8 kHz WAVs, copied verbatim as `usb_connect_sound.h` / `usb_remove_sound.h`).

- **Trigger (C):** `mp_usbh.c`'s `tuh_mount_cb`/`tuh_umount_cb` call
  `usbh_notify_event(connect)`, which `mp_sched_schedule`s a registered Python
  callback as `cb(True)`/`cb(False)`. These run in `tuh_task()` (thread) context,
  so scheduling is safe.
- **Callback registration:** `keyboard.on_usb_event(cb)` stores `cb` in a rooted
  pointer (`MP_REGISTER_ROOT_POINTER(usbh_event_cb)` in `usb_keyboard.c` — it
  must live in a QSTR/root-scanned file, which `mp_usbh.c` is not). `_boot` sets
  it to `pcaudio.system_sound`.
- **Decode (C):** `audio.usb_sound(connect)` skips the 44-byte header and expands
  the 8-bit unsigned mono PCM to a fresh 16-bit signed **stereo** bytearray
  (sample copied to both channels), returning `(samples, rate)`.
- **Playback (Python):** `pcaudio.system_sound()` applies the current volume
  (`audio.scale`) and plays through the normal background I2S path in 4096-byte
  chunks. Like MMBasic's `PlayMemWav`, it **returns immediately if audio is
  already playing** (`_pb is not None`), so it never cuts off music. Set
  `pcaudio.usb_sounds = False` to silence.

A plug-in sound therefore also plays shortly after boot when the keyboard
enumerates — expected. MMBasic ties these to USB-MSC mount/unmount; we have no
MSC yet, so they fire for *any* device (keyboard/mouse/…).

---

### 25. USB multi-touch + gestures — `touch()`

USB touchscreens (multi-touch HID digitizers) are supported, with the same
gesture vocabulary as MMBasic's `TOUCH()` (`fun_touch` in Pointer.c). Ported
faithfully from MMBasic and adapted to our event-driven USB host.

**User function** (`touch(subcommand [, n])`, injected into the REPL):

| Subcommand | Returns |
|---|---|
| `"X"` / `"Y"` | contact-0 coords, or **-1** when nothing is touching |
| `"DOWN"` / `"UP"` | 1/0 — is the screen being touched |
| `"X2"` / `"Y2"` | second contact, or -1 |
| `"XN", n` / `"YN", n` | nth contact (n=1..); **n=0 → live contact count** |
| `"SWIPE"` | 0 none / 1 L / 2 R / 3 U / 4 D (clears on read) |
| `"SWL"/"SWR"/"SWU"/"SWD"` | 1 if that swipe just happened (clears) |
| `"TAP"/"HOLD"/"DTAP"` | tap / long-press / double-tap (clears) |
| `"PINCH"` | 0 none / 1 expand / 2 contract (clears) |
| `"EXPAND"/"CONTRACT"` | 1 if that pinch just happened (clears) |
| `"ROTATE"` | 0 none / 1 CW / 2 CCW (clears) |
| `"CW"/"CCW"/"TTAP"` | rotate / two-finger tap (clears) |
| `"PRESENT"` | 1 if a USB touch panel is connected |

Latched gestures are **clear-on-read** one-shots (reading the matching code
consumes it), exactly like MMBasic — so a polling loop can `SELECT CASE` on
`touch("SWIPE")` or test `touch("SWL")` then `touch("SWR")` without losing the
event.

**Architecture:**
- **`usb_touch.c`** (includes tusb.h; not QSTR-scanned) holds it all: the HID
  report-descriptor parser (`analyze_touch_descriptor` — Windows Precision
  Touchscreen layout: Digitizer page 0x0D, Finger collections, Tip/ContactID/
  X/Y/ContactCount), the report decoder + coordinate scaler, report
  **reassembly** (RP2350 host splits long reports across 64-byte packets and
  coalesces short ones), the **digitizer-init handshake** (Input-Mode feature
  write / cert-blob GET_FEATURE reads to switch dual-mode panels out of mouse
  mode), a **100 ms no-report watchdog** (some panels omit the release report),
  and the single- + two-finger **gesture state machine** (from Pointer.c).
- Contacts are scaled to the **current framebuffer geometry** (`hdmi_get_width/
  height()`), so coordinates match the pixel space you draw in — and follow a
  `screen()` mode change automatically.
- **`usb_touch_mod.c`** is the QSTR-scanned `touch` module (`touch.query`),
  reading state via `usb_touch_query()` — no USB access, safe from Python.
  `touch(sub[, slot])`, or `touch("XN"/"YN", n[, slot])` — the slot is optional
  and last (default = the panel's own slot).

**HID handling — ported VERBATIM from MMBasic (do not reinvent this).** Getting
two devices (keyboard + touch) to coexist required replicating MMBasic's exact
model in `mp_usbh.c`; my first attempts (continuous re-arm in the callback,
`set_protocol` from the mount cb) caused order-dependent enumeration and cost a
long debug cycle. The proven model:
- **4-slot device table** `hid_slots[4]` (1=keyboard, 2=mouse, 3=gamepad,
  4=touch) allocated by MMBasic's `FindFreeSlot` (touch prefers slot 4).
- **Request-based polling, NOT re-arm.** Each slot's `report_timer` counts up in
  the 1 ms `usbh_wake_cb`; `hid_poll()` (called from `mp_usbh_task`, = MMBasic
  `hid_app_task`) issues **one** `tuh_hid_receive_report` per slot when its timer
  reaches `report_rate` (keyboard 20 ms, touch 5 ms), guarded by
  `report_requested`. `tuh_hid_report_received_cb` only stores + dispatches, then
  clears `report_requested` and zeroes `report_timer` — **it never re-arms.**
- **Staggered startup** `report_timer = -(10 + (slot+2)*500)`: the touch panel
  (slot 4) begins reporting ~2.5 s after it's plugged in (gives its bring-up
  handshake time). This is MMBasic's exact value — expected, not a bug.
- **The keyboard mount MUST NOT call `tuh_hid_set_protocol(BOOT)`.** TinyUSB
  already activates boot protocol on boot-capable interfaces during enumeration.
  Issuing that EP0 control transfer from the mount callback wedges enumeration of
  any device *behind* the keyboard — this was the "keyboard first → nothing else
  enumerates" bug. (Touch *does* set REPORT protocol via `probe_mount`, matching
  MMBasic, and that's fine.)
- `mp_usbh_task` has a **reentrancy guard**: an enumeration `mp_printf` → dupterm
  → Python console → VM → `MICROPY_VM_HOOK_LOOP` → `mp_usbh_task` could otherwise
  re-enter `tuh_task()` mid-enumeration and corrupt it.
- Touch reports route to `usb_touch_on_report`; `mp_usbh_task` also pumps
  `usb_touch_task` (handshake + watchdog); GET/SET_FEATURE completions sequence
  the handshake.

**Keyboard LEDs (Caps/Num/Scroll)** — ported from MMBasic. Each keyboard slot
carries a `sendlights` bitmap (**0x01 num, 0x02 caps, 0x04 scroll**). It's pushed
to the physical LEDs via `tuh_hid_set_report(..., HID_REPORT_TYPE_OUTPUT,
&sendlights, 1)` (`kbd_set_leds()`): once on the slot's **first poll**
(`notfirsttime`), and again whenever Caps (0x39) / Num (0x53) / Scroll (0x47) is
pressed — `kbd_key()` toggles the matching global lock state, updates the bit,
and re-sends. Lock keys are excluded from auto-repeat. Lock states default off
(MMBasic seeds from `Option.capslock/numlock`, which we don't have yet — could be
persisted via `pcconfig` later). NOTE: num-lock currently drives the LED only; it
does not yet remap the numeric keypad (digits vs navigation) — a separate MMBasic
behaviour to add if wanted.

`touch()` diagnostics used during bring-up were removed; only the real API
remains, including `touch("PRESENT")` and `touch("SLOT")`.

---

### 26. USB mouse — `mouse()`

USB mice work on slot 2, exposed like MMBasic's `DEVICE(MOUSE n, "...")` reader.
Ported from MMBasic: `analyze_mouse_descriptor`/`process_mouse_report`
(USBKeyboard.c) + `process_mouse_input` (KeyboardMap.c).

| `mouse(code)` | Returns |
|---|---|
| `"X"` / `"Y"` | virtual cursor position — deltas accumulated and clamped to the screen (starts centred) |
| `"L"` / `"R"` / `"M"` | left / right / middle button (1 = pressed) |
| `"W"` | scroll-wheel accumulator |
| `"B"` | raw button bitmap (1 L, 2 R, 4 M) |
| `"D"` | left-button double-click within 500 ms (clears on read) |
| `"T"` | 3 if the mouse has a wheel, else 0 |
| `"PRESENT"` / `"SLOT"` | mouse connected? / its HID slot (2) |

`mouse(code, slot)` pins an explicit slot. `mouse_speed()` gets / `mouse_speed(v)`
sets sensitivity (raw delta ÷ v, then MMBasic's fixed ÷2), = MMBasic
`Option.mousespeed`.

**Architecture** mirrors touch: **`usb_mouse.c`** (tusb.h; not QSTR-scanned) holds
the descriptor parser (detects 8/12/16-bit X/Y mice), the per-type report decoder,
delta accumulation into a screen-clamped cursor, buttons + wheel + double-click,
and `usb_mouse_query()`. **`usb_mouse_mod.c`** is the QSTR-scanned `mouse` module.
`mp_usbh.c` calls `usb_mouse_mount` on a protocol-MOUSE device, routes reports to
`usb_mouse_on_report` via the slot table, and coordinates scale to the framebuffer
(`hdmi_get_width/height()`). Uses the same MMBasic polling model as the keyboard.

**Tight-loop pumping:** `mp_usbh_task()` normally runs from
`MICROPY_INTERNAL_EVENT_HOOK`, which only fires when the runtime *waits* (REPL,
`sleep`, I/O). A `while True:` polling loop never yields to it, so USB reports
(touch **and** keyboard) would freeze mid-program. Fixed with a divided
`MICROPY_VM_HOOK_LOOP` (in `mpconfigboard.h`) that calls `mp_usbh_task()` every
512 VM branch back-edges — negligible cost, keeps input live in tight loops. The
VM checks pending exceptions right after the hook, so Ctrl-C still works.

**REPL helpers in `run()`:** `run()` seeds a program's namespace with a *copy* of
`__main__.__dict__` (non-dunder names), so injected helpers — `touch`, `ls`,
`play`, `hdmi`, the colour palette — are available in a program exactly as at the
prompt, without letting it clobber the real REPL globals. (`vars()` isn't a
MicroPython builtin; use `__dict__`.)

**Limitations (v1):** one active touch panel at a time (the touch slot);
single-finger absolute-pointer "touch monitors" that enumerate as a Mouse
interface (MMBasic's TOUCHMOUSE) aren't yet routed in — only true multi-touch
digitizers (protocol NONE with Finger collections). Mouse/gamepad get slots and
are polled but not yet decoded. **Confirmed working on hardware alongside a USB
keyboard, order-independent, once the MMBasic HID model was replicated exactly.**

---

### 27. Board-scope refactor — keep the shared rp2 files clean

A review of everything this port changed in **shared** MicroPython files (as opposed
to new files) found several PC3-only changes sitting in port-wide files, where they
would affect (or in one case silently break) every other rp2 board. All were moved
into board scope. Verified afterwards with a clean `RPI_PICO` build (680 KB, **zero**
PC3 objects, generic help text) and a clean `PICO_COMPUTER_3` build (firmware
byte-identical to v0.1).

- **Flash filesystem size — was a real bug.** `mpconfigport.h` did
  `#undef MICROPY_HW_FLASH_STORAGE_BYTES` / `#define … (12*1024*1024)`, which
  changed only the **C** value. The linker still reserved **2.5 MB** (the stale
  value in `mpconfigboard.cmake`), so LittleFS believed it owned ~9.5 MB of flash
  it did not — a filesystem grown past 2.5 MB would overrun its partition. Fix: the
  shared header is back to its `#ifndef` 1408 KB default, and the real **12 MB** is
  set once in `mpconfigboard.cmake`. The rp2 build passes that CMake var to **both**
  the linker (`__micropy_flash_storage_bytes__` defsym) **and** C (a `-D` compile
  def), so setting it in one place keeps them in agreement. **Rule: never override
  `MICROPY_HW_FLASH_STORAGE_BYTES` in a C header — set it in the board cmake.**
- **ulab wiring.** ulab is now pulled in with
  `list(APPEND USER_C_MODULES …/lib/ulab/code/micropython.cmake)` in
  `mpconfigboard.cmake`. ulab enables itself via `MODULE_ULAB_ENABLED` from its own
  cmake; the C `MICROPY_PY_ULAB` define was never used by it, and the
  `if(MICROPY_PY_ULAB)` block in the port CMakeLists was **dead** (never set as a
  CMake variable — ulab only built before because a `USER_C_MODULES=…` from an old
  command line was cached in the build dir). A fresh `make BOARD=PICO_COMPUTER_3`
  now includes ulab reproducibly.
- **Redundant feature block removed.** The `MICROPY_PY_JSON/RE/COLLECTIONS/MATH/
  CMATH/TIME/STRUCT/ERRNO/GC/…` block in `mpconfigport.h` was deleted — all of those
  are already enabled by the port's default `EXTRA_FEATURES` ROM level, so forcing
  them port-wide changed nothing except removing other boards' ability to opt out.
- **Board-specific C sources gated.** The port CMakeLists no longer adds the PC3
  media/USB/SD sources to every rp2 build. They are gated (in both the source list
  and the QSTR list) on `MICROPY_HW_ENABLE_HDMI` (HSTX DVI + `audio.c`/`dr_*` +
  image loaders), `MICROPY_PY_MACHINE_SDCARD` (`machine_sdcard.c`), and
  `MICROPY_HW_USB_HOST` (`mp_usbh.c` + `usb_*`), each set in `mpconfigboard.cmake`.
- **help() pin text.** The shared `help.c` line is back to the generic
  `Pins are numbered 0-29 …` and is now an overridable `MICROPY_HW_HELP_PIN_TEXT`
  macro; PC3 defines the `0-47` version in `mpconfigboard.h`. (This also fixed a
  `"47volumeADC"` corruption that had crept into the hardcoded line.)
- **`_boot.py` de-PC3'd.** The shared `ports/rp2/modules/_boot.py` no longer holds
  any PC3 logic. It ends with a generic hook — `try: import _boot_board except
  ImportError: pass` — so any board may freeze a `_boot_board` module to run its own
  start-up. All PC3 boot code (REPL injection, SD mount, RTC sync, HDMI + console
  bring-up) moved verbatim into the new frozen `_boot_board.py`, freezing it in the
  board manifest.

Net effect: the shared-file diff shrank from ~180 changed lines to ~90, and what
remains in shared files is either board-agnostic (an overridable default or a
generic hook) or fully `if()`-gated. The other shared edits kept as-is are the
deliberately generic, opt-in ones (banner separator, pin reservation in the `Pin`
constructor, the cyw43 PIO-divider fix, the USB-host `tusb_config.h` block).

---

### 28. `hdmi.text()` scaled font + XMODEM file transfer

Two additions, both driven by making the standalone machine easier to use.

- **`hdmi.text(s, x, y, fg, bg=-1, scale=1)`** (in `hdmi.c`): draws a string in the
  MMBasic **8×12** console font (`font1`) at an arbitrary pixel position, scaled by
  `scale` (each font pixel → a `scale`×`scale` block), format-aware (RGB332/RGB565)
  like `hdmi.putc`. `bg=-1` draws with a transparent background. Returns the x past
  the string. `framebuf.text()` is only the fixed 8×8 font, and the 8×12 console
  font lives in C (not reachable from `framebuf`), so this exposes it for big/legible
  on-screen text (used by the graphical Sudoku demo).

- **XMODEM file transfer** — new `xmodem` module (`xmodem.c`), a faithful port of
  MMBasic's `misc/XModem.c` (see [[replicate-mmbasic-exactly]]): 128-byte XMODEM,
  additive checksum on receive, checksum-or-CRC on transmit, same retry/timeout
  constants and `crc16_ccitt` table, and the same 1 KB software FIFO in `_inbyte`.
  `xmodem.recv(path)` / `xmodem.send(path)` (aliased to `xrecv`/`xsend` in the REPL
  by `_boot_board`) transfer files to/from `/sd` or flash over the **console UART**.
  During a transfer the console RX **IRQ is disabled** so raw bytes reach the
  protocol instead of the REPL, and output goes straight to the UART (not through
  the dupterm/HDMI console). The transfer workers return an error *string* rather
  than raising, so the wrapper always re-enables the IRQ before raising — a dead
  console on error was the trap. **Deviation from MMBasic (deliberate):** the file
  receive path trims the sender's trailing padding (`0x1A`/`0x00`) from the *last*
  block only (by holding one block back and trimming at EOT), so transferred `.py`
  files aren't corrupted by pad bytes and run as-is; MMBasic writes raw blocks and
  relies on its own tokeniser to tolerate the padding. Gated on
  `MICROPY_HW_ENABLE_XMODEM` (board cmake) + `#if MICROPY_HW_ENABLE_UART_REPL`.

---

### 29. RGB1024 — native 1024×600×4 (16 colours) — Phase 1 (scanout)

**Naming:** the mode is **`hdmi.RGB1024`** (named for its framebuffer width, like
RGB640/RGB320/RGB512); its pixel format is **RGB121** (1-bit R, 2-bit G, 1-bit B =
4bpp), the name kept for the format/palette/dither identifiers below (as RGB332 /
RGB565 are for the other modes).

New **`hdmi.RGB1024`** mode: a **true native 1024×600** at 16 colours, alongside the
existing `RGB512` (512×300×16 pixel-doubled to 1024×600). Both share the 1024×600
timing and the 307,200-byte framebuffer; RGB1024 packs **4bpp** (2 px/byte), which
is an *exact fit* (1024·600/2 = 307,200) — no RAM increase, and full native
sharpness that the doubled RGB512 can't give. See [[replicate-mmbasic-exactly]].

**Key insight (from MMBasic):** RGB121 is **not** a new HSTX pixel format. HSTX
only ever runs the RGB332 or RGB565 expander. RGB121 reuses the **RGB332** native
expander; core1 expands each packed 4bpp source line, one nibble at a time,
through a **16-colour palette** into an RGB332 line buffer that HSTX scans. It is
the same "core1 fill-loop into `HDMIlines`" path as RGB320/RGB512 — only the inner
loop differs (a `map16[nibble]` lookup instead of a 16-bit copy).

- **Packing = `framebuf.GS4_HMSB`.** MMBasic's RGB121 (low nibble = even/left px)
  and MicroPython `GS4_HMSB` (even x → low nibble) agree exactly, so `hdmi.fb()`
  returns a `pcgfx.Display` in `GS4_HMSB` and all framebuf primitives draw straight
  into the packed buffer. `Display.colour()` now returns the nearest **4-bit
  palette index** (bit3=R, bits2:1=G, bit0=B) in GS4 mode.
- **Palette** — default is MMBasic's MAP16DEF (pure RGB121 bit-expansion, stored
  as RGB888 in `hdmi_pal_default`). **Settable** at runtime via `hdmi.palette(i,
  0xRRGGBB)` (get: `hdmi.palette()` / `hdmi.palette(i)`); a set rebuilds the SRAM
  expansion table so it shows immediately, even mid-scan. `pcconfig.palette()`
  wraps it to persist across reboots (applied at boot by `apply_palette()`); the
  live palette survives a mode switch (lazy-init flag, not re-defaulted per init).
- **Palette MUST be in SRAM, not flash** — the core1 hot loop can't touch flash.
  A first cut kept the 16-entry palette `const` (flash) and read it per pixel; the
  picture came up then died the instant `screen()` saved `settings.json`, because
  this board disables the multicore flash lockout (§9), so a core0 flash write
  drops XIP out from under core1's palette reads. Fix: at init build an SRAM
  **256-entry `uint16` table `hdmi_map256`** (source byte → two RGB332 pixels,
  low byte = even/left) from the flash palette; the hot loop is then one SRAM
  lookup + one 16-bit store per byte-pair — no flash reads, and lighter than the
  RGB512 loop, so line-time is comfortable. (General rule for this port: anything
  core1 reads during scanout — code, framebuffer, line buffers, and now the
  palette — must be SRAM-resident.)
- **Scanout hot path:** a third per-resolution DMA IRQ **`hdmi_dma_irq_1024`** (like
  `hdmi_dma_irq_x` but 256 words/line = 1024 RGB332 px at 4 px/word, not 512).
  core1 fill-loop gets an RGB121 branch (512 packed bytes → 1024 RGB332 px, no
  vertical doubling). `clk_hstx = clk_sys = 252 MHz` → 50.4 MHz pixel (fixed 252,
  like RGB512).
- **Format-aware drawing:** `hdmi.fill/scroll/putc/text/blit_glyph` gained a 4bpp
  branch (read-modify-write the correct nibble; scroll/fill are byte ops), so the
  on-screen console works in RGB1024 without corrupting the packed buffer.
- **New queries:** `hdmi.bpp()` → 4/8/16; `hdmi.rgb565()` now means *16-bit
  RGB565* only (False for RGB1024). `screen(hdmi.RGB1024)` persists it (fixed 252).
- **Images in RGB1024 (phase 3):** `draw_jpg/bmp/png` and `save_image` work in
  RGB1024. The loaders take `hdmi.bpp()` (4/8/16) instead of the old `hdmi.rgb565()`
  bool and gained a 4bpp path: each RGB888 pixel maps to the **nearest** of the 16
  live palette entries (`hdmi_nearest_index`, squared-distance search) packed into
  the correct nibble; `save_image` reverses it (`hdmi_index_rgb888`) to a 24-bit BMP.
- **Dithering (phase 3b) — `draw_jpg`/`draw_bmp` `dither=` arg:** `True` = Atkinson
  (the recommended default; `_dither_mode()` in pcimage maps `True`->2, `False`/
  `None`->0, ints pass through), `1` Floyd-Steinberg, `2` Atkinson, `0` none. Works
  for **RGB1024** (4bpp, quantise to the palette grid) and **RGB640** (8bpp, quantise
  to RGB332); ignored for RGB565 and PNG. New shared TU `dither.c`/`.h` ports
  MMBasic's exact quantisers (`rgb888_to_rgb121/rgb332_dither`) + FS/Atkinson error
  distribution (`FileIO.c`) as a stateful per-row API; `dither_t.format` selects the
  grid and `dither_row` emits either a 4-bit index or an 8-bit RGB332 byte. Applied at
  **output** resolution (after JPEG binning), one row at a time top-to-bottom with
  two `int16` error rows swapped per row; off-screen rows are still dithered to keep
  the error state continuous, only in-bounds pixels are written. Error buffers are
  transient `m_malloc` (freed after the load) held on the decode's C stack (JPEG) or
  a `bmp_load` stack struct reached via `g_bd` (BMP) so the **GC can't reclaim them
  mid-decode** (cf. §16/§21). Quantises to the fixed RGB121 grid = the default
  palette, so a heavily customised palette + dithering can mismatch. PNG is left
  nearest-colour only (transparency complicates row diffusion; MMBasic dithers only
  BMP/JPG too). See [[replicate-mmbasic-exactly]].
- **Atkinson (2) beats Floyd-Steinberg (1) on RGB121 — expected, not a bug.** Both
  kernels are verbatim MMBasic. RGB121 has only 2 red / 2 blue / 4 green levels and
  channels dither independently, so a grey pixel resolves to magenta/green speckle.
  FS propagates 100% of the (±127) error, maximising that speckle in flat regions —
  often looking worse than no dithering; Atkinson propagates 6/8, staying calmer
  (it was designed for 1-bit displays). Docs steer RGB1024 users to `dither=2`.

Verify: `screen(hdmi.RGB1024)` → crisp native 1024×600; `hdmi.test()` shows 16
vertical colour bars; the REPL console renders on-screen.

---

### 30. Key-state input — `keydown()` + `keyboard.on_key()` (+ num-lock keypad)

Games can't read held keys from a stdin stream, so this ports MMBasic's
`KEYDOWN()` (`fun_keydown`, MM_Misc.c + the `KeyDown[]` state filled in
KeyboardMap.c `process_kbd_report`). See [[replicate-mmbasic-exactly]].

- **State (`mp_usbh.c`):** `kbd_keydown[7]` — [0..5] mapped codes of the held
  keys in *reverse report order* (so `keydown(1)` = most recent), [6] = the
  modifier bitmap in MMBasic's bit order (1 L-Alt, 2 L-Ctrl, 4 L-GUI, 8 L-Shift,
  16/32/64/128 the right-hand versions — NOT the raw HID order). Filled at the
  end of `kbd_process_report`; a report containing HID error codes 1–3
  (roll-over) leaves the previous state untouched, exactly as MMBasic. Cleared
  on keyboard unmount (with `kbd_prev`, so a re-plug starts clean).
- **Mapping — `kbd_map_code()`,** a faithful port of `APP_MapKeyToUsage`:
  lock keys → 0; **AltGr specials** for the DE/FR/ES/BE layouts (verbatim
  tables, keyed off `kbd_layout_name` — these do *not* yet apply to the stdin
  typing path, which is untouched); Ctrl+letter → 1..26 (from the unshifted
  column, so layout-aware); keypad → column by **num-lock**; letters → column by
  caps XOR shift; rest → column by shift. Non-printing keys therefore report
  MMBasic's codes straight from the vendored tables (UP 0x80 … F1 0x91 …,
  with shift variants like Shift-Down 0xA1).
- **`keyboard.keydown(n=0)`** (usb_keyboard.c → `usb_kbd_keydown()`): n=0 count
  of held keys, 1..6 nth key code, 7 modifier bitmap, 8 lock bitmap (1 caps,
  2 num, 4 scroll). **Drains `stdin_ringbuf` on every call** — MMBasic's
  `while(getConsole()!=-1);` — so a polling game doesn't leave WASD spam as
  typed-ahead REPL input. Injected into the REPL as `keydown` by `_boot_board`.
  Codes exposed as module constants (`keyboard.UP/DOWN/…/F1..F12`).
- **`keyboard.on_key(cb)`** — `cb(code)` scheduled (mp_sched_schedule, thread
  context) for every new keypress *and* every synthesised auto-repeat (matching
  what a console reader would see). Same rooted-pointer pattern as
  `on_usb_event` (`MP_REGISTER_ROOT_POINTER(usbh_key_cb)` in usb_keyboard.c,
  read from mp_usbh.c). Unmapped keys (code 0) are not reported; the key still
  goes to stdin as normal. A full scheduler queue silently drops (acceptable —
  state polling via keydown() is the lossless path).
- **Num-lock keypad remap (stdin path)** — the deferred §25 item: `kbd_num` now
  defaults **true** (MMBasic `Option.numlock` default), and with num-lock *off*
  `kbd_key()` redirects keypad digit/period usages (0x59–0x63) to the equivalent
  nav-cluster usages so the existing VT100 switch emits the right sequences
  (kp4 → Left `\x1b[D`, kp0 → Ins, kp. → Del, …). Keypad-5 keeps typing '5'
  (MMBasic's table has no shifted meaning for it). With num-lock on (default)
  behaviour is unchanged, so the proven typing path is untouched.

Verify: hold A → `keydown()`=1, `keydown(1)`=97; Shift-A → 65; two keys →
`keydown(0)`=2 with `keydown(1)` the newer; arrows → 0x80..0x83; `keydown(7)`
tracks Shift/Ctrl/Alt; `keydown(8)` bit 2 set at boot (num-lock on);
`keyboard.on_key(print)` echoes codes incl. auto-repeat; num-lock off → keypad
arrows move the cursor at the REPL.

---

### 31. MOD tracker, tones and 4-voice synth — audio parity with MMBasic

Ports MMBasic's `PLAY MODFILE`/`MODSAMPLE`, `PLAY TONE`, `PLAY SOUND` and
`PLAY PAUSE`/`RESUME` (io/Audio.c) onto the existing background pump: each new
source is a C "produce a chunk" function that pcaudio's scheduled I2S callback
pulls, with the master volume applied per chunk by `audio.scale` (playing the
role of MMBasic's `i2sconvert`). See [[replicate-mmbasic-exactly]].

- **MOD (`play("x.mod", loop=)` + `mod_sample()`):** vendored **hxcmod.c/h from
  MMBasic third_party_mod** (upstream is Jean-François Del Nero's HxCMOD,
  license "do what you want"; MMBasic's copy adds the `seffect` sound-effect
  engine that `hxcmod_playsoundeffect` = `PLAY MODSAMPLE` needs, so theirs is
  vendored, not upstream's). The whole .mod loads into a Python `bytes` (PSRAM
  heap) — hxcmod plays pattern/sample data **in place** (verified: only the
  1084-byte header is copied into `modcontext`; the load never writes to the
  file buffer, so an immutable `bytes` is safe). pcaudio keeps the bytes
  referenced in `_pb`; the `modcontext` (~tens of KB) comes from the **rooted
  audio allocator** (`audio_allocs`, §16), so the GC can't reclaim either
  mid-song. Output is 16-bit stereo at **22050 Hz** (MMBasic's
  `modfilesamplerate`) — MMBasic doubles each sample to a 44100 output; we just
  run I2S at 22050. End-of-song (noloop): `hxcmod_fillbuffer` returns 1
  mid-buffer; the buffer is memset-0 before each fill so the tail is silence
  (MMBasic replays stale samples there). `mod_sample(1..32, effect 1..4,
  vol 1..64, rate)` → Amiga period `3579545/rate` (MMBasic hardcodes 16000; we
  expose it). Effect-trigger latency ≈ chunk (46 ms) + I2S queue (93 ms @
  ibuf 8192) — the same ballpark as MMBasic's two 8 KB swing buffers.
- **Tone (`tone(fl, fr, ms)`):** faithful `fillToneBuffer` — 4096-entry
  `SineTable` (vendored, `sound_tables.h`), float phase accumulators,
  `(table-2000)*16` full-scale, `mono` fast path when fl==fr, duration rounded
  to **whole left-channel cycles** (f ≥ 10 Hz) so it ends at a zero crossing
  (the rounding multiplies the integer cycle count by the *float* period,
  exactly as MMBasic — integer-truncating the period ends mid-cycle = click).
  Re-calling `tone()` while playing **retunes without restarting** (MMBasic's
  repeat-call path: new PhaseM/SoundPlay, phases kept) — melodies don't click.
- **Synth (`sound(voice, side, wave, freq, vol)`):** faithful
  `fillSoundBuffer`/`getsound` (I2S branch): 4 voices × independent L/R; sine +
  triangle from vendored tables, square/saw computed (MMBasic's 99/98 marker
  values become a clean enum — same waveform math), **P**eriodic noise = 4096
  random 100..3900 table scanned by phase (MMBasic `setnoise`, lazily built in
  rooted PSRAM), **N** white noise = random level held for a freq-length dwell.
  Per-voice volume 0..25 → `mapping[vol*41/25]` (vendored `mapping[101]`) —
  the limit that lets 4 full voices just fill int16 (`sum*16`). Volume changes
  **ramp 1 step/ms** (`SOUND_RAMP_INTERVAL` 44) = MMBasic's click-free ramp.
  Deviations (deliberate): fresh playback zeroes voice volumes (MMBasic
  inherits a boot default of 25); white-noise dwell uses the *new* waveform's
  interpretation on the first call (MMBasic checks the old mode — artifact).
- **Pause/resume (`pause()`/`resume()`):** the pump stops feeding
  (`_paused`); the I2S queue drains (≤ ~93 ms tail) and the callback chain
  ends. `resume()` re-primes with `_feed()`. A `_pending` flag (set on write,
  cleared on callback entry) prevents a pause→resume race from issuing a
  second non-blocking write while one is in flight (the I2S driver doesn't
  allow overlapping writes).
- **Chunk/queue sizing:** files keep 4096-byte chunks + 16 KB ibuf; tone/synth
  use 2048 + 4 KB (live changes audible in ~35 ms, MMBasic's 704-byte swing
  buffers are snappier but our scheduler-driven pump needs more margin); MOD
  4096 + 8 KB. All synth fills are cheap C, so underrun margin stays large.
- **New C surface (`audio` module):** `tone_start/tone_read`,
  `sound_reset/sound_set/sound_read` + waveform-id constants,
  `mod_open/mod_read/mod_close/mod_sample`. pcaudio maps MMBasic's letters
  (S/Q/T/W/P/N/O, sides L/R/B) and injects `tone`, `sound`, `mod_sample`,
  `pause`, `resume` into the REPL.

Verify: `play("/sd/x.mod", loop=True)` prints the title and plays;
`mod_sample(n)` fires an effect over the music; `tone(440,880,1000)` clean
start/end; a `tone()` loop plays a melody without clicks; `sound()` all seven
waveforms; four voices at vol 25 don't clip; `pause()`/`resume()` on every
source; MP3 + HDMI 1024×600 still coexist.

---

### 32. Overlay layer + off-screen buffer (MMBasic FRAMEBUFFER N/L/F)

Ports MMBasic's `FRAMEBUFFER LAYER/CREATE/WRITE/COPY/CLOSE` (FrameBuffer.c +
the HDMI.c scanout merge). Three drawing targets in `hdmi.c`:

- **N** — the display (`hdmi_fb`), always exists.
- **L** — the overlay **layer, RGB320 only**: the *second half* of the static
  video memory (2 × 320×240×2 = exactly the 307,200-byte buffer — the same
  arithmetic MMBasic uses: `LayerBuf = DisplayBuf + ScreenSize`). core1's
  RGB320 fill loop merges it per pixel before the H-double: layer pixel wins
  unless it equals the single **transparent colour** (RGB565; converted from
  RGB888 with pcgfx's exact `colour()` formula so Python-side colours match
  the merge compare). Merge cost ~4 extra cycles/px — well inside the line
  budget. The layer must be SRAM (core1 cannot scan PSRAM), which MMBasic
  enforces too ("Layer Buffer must be in tightly coupled RAM").
- **F** — an off-screen, display-sized buffer in **PSRAM** (GC heap), never
  scanned — a draw/decode target and copy endpoint only. Held in a **rooted
  pointer** (`MP_REGISTER_ROOT_POINTER(hdmi_framebuf_f)`) so it survives with
  no Python reference; a soft reset clears the root and `hdmi_wbuf()` snaps
  the target back to N.

**Write-target switch:** one static `hdmi_target`; `hdmi_wbuf()` resolves it
and every drawing entry point uses it — `framebuffer()` (and therefore
`fb()`, the **image loaders** via pcimage, and `save_image`), `fill`,
`scroll`, `putc`, `blit_glyph`/`text`, and the console (putc/scroll). This is
MMBasic's `WriteBuf` model: `hdmi.write("L")` redirects *everything*,
including REPL output — documented, and exactly how MMBasic behaves.

**API:** `hdmi.layer(transparent=0)` (errors if it already exists, as
MMBasic; layer pre-filled with the transparent colour *before* the volatile
enable so it appears atomically), `hdmi.create()`, `hdmi.write("N"/"L"/"F")`
(getter with no args), `hdmi.copy(src, dst)` (whole-buffer memcpy),
`hdmi.close(["L"/"F"])` (no arg = both; resets the target to N if it was
closed). **Any mode change (`hdmi.init`) closes both** L and F (sizes are
mode-dependent) — as MMBasic's mode switch. Deviation from MMBasic: our
transparent colour is a full RGB888→RGB565 value (MMBasic mode-2 uses 4-bit
palette indices; its 16-bit HDMI mode uses `RGBtransparent` the same way we
do).

Verify: `screen(hdmi.RGB320)`; scenery via `draw_jpg`; `hdmi.layer()`;
`hdmi.write("L")`; draw text/rects on `hdmi.fb()` → they overlay; `fill(0)`
on the layer → scenery intact; `hdmi.copy("N","F")` + `hdmi.copy("F","N")`
round-trip; mode change silently drops both; console follows the target.

**`hdmi.framebuffer()` returns a MEMORYVIEW, not a bytearray** (hardware
incident): typing `hdmi.framebuffer()` bare at the REPL makes the REPL print
the buffer's repr — for a bytearray that is ~600 KB of `\x..` hex spam pushed
through the 115200 UART *and* the on-screen console renderer (minutes of
"scrolling garbage"), and a Ctrl-C landing inside the console's dupterm write
raises there, which makes `os.dupterm` **deactivate the on-screen console** —
net effect looked like a firmware lock-up. A memoryview's repr is a few
characters, and it is interchangeable with a bytearray everywhere the buffer
is actually consumed (`framebuf.FrameBuffer`, the image loaders, `bmp.save`
all use the buffer protocol). General rule for this port: **never return a
large bytearray from a REPL-facing function — return a memoryview.**

---

### 33. Blitter — `hdmi.blit()` (MMBasic BLIT, generalised across targets)

`hdmi.blit(x, y, w, h, x1, y1[, src[, dst[, skip]]])` — rectangle copy within
one buffer or between any two of N/L/F (src/dst default to the current write
target, matching MMBasic's plain `BLIT` operating on `WriteBuf`). MMBasic's
plain BLIT is same-surface and opaque; the cross-buffer form and the `skip`
colour are our generalisation (MMBasic gets transparency from BLIT MEMORY /
sprites instead).

- **Clip** is MMBasic `cmd_blit`'s exact shape: a negative source origin
  shifts the destination and vice versa, then both rects are clamped to the
  mode geometry, then the combined guard bails if anything is still out of
  range.
- **Overlap safety** (same buffer): opaque 8/16bpp blits are one `memmove`
  per row (horizontal overlap safe), iterated bottom-up when `y1 > y`
  (vertical overlap safe) — cheaper than MMBasic's column-strip approach,
  same result. The per-pixel path chooses row order by `y1 > y` and, for
  same-row copies, column order by `x1 > x`.
- **`skip`** is a native-format colour compared per source pixel (like
  `fill`/`putc`/`text` colours; -1 = opaque). 4bpp packed (RGB1024) always
  uses the per-pixel path (nibble alignment); 8/16bpp only when `skip` is
  given. Full-screen per-pixel worst case ~10 ms; sprite-sized blits are
  microseconds.
- `hdmi_px_get/px_set` helpers added (mode-branched single-pixel access) —
  also the natural base for a future sprite engine.

Verify: same-buffer shift left/right/up/down over itself (no smearing);
`blit(..., "F", "N", skip)` sprite stamping with a cut-out colour in RGB320,
RGB640 and RGB1024; save/restore-under-sprite round-trip via F; clipped blits
at all four edges.

---

### 34. Sprite engine — `pcsprite` (MMBasic semantics, compositor rendering)

A deep review of MMBasic Sprite.c (2,654 lines) concluded its *semantics* are
battle-tested but its *rendering architecture* — save-under buffers ordered
by two LIFO stacks, SHOW/HIDE SAFE tearing down and rebuilding everything
above the target, MOVE/SCROLL hiding and re-showing every sprite on the live
screen — is the organically-grown part (its own `"sprite internal error"`
consistency checks say as much). The port keeps the semantics and replaces
the architecture:

- **Rendering = dirty-rectangle compositor** (`pcsprite.py`, frozen):
  sprites are Python objects in one z-ordered list. `update()` collects
  old+new rects of moved/hidden sprites, erases just those patches, redraws
  intersecting sprites in z-order, then runs one collision pass — the
  MMBasic `next_x/MOVE` deferred-commit idea promoted to the core primitive.
  No stack discipline, no SAFE variants, no save-under buffers at all.
- **Erase source per mode**: RGB320 composites sprites on the **overlay
  layer** (erase = `fill_rect` of the layer-transparent colour; scenery on N
  untouched — the §32 layer was built for this). Other modes: classic
  dirty-rect engine with the **F buffer** as scenery snapshot (erase =
  `blit(F→N)`); `snapshot()` refreshes it. Mode picked automatically on
  first `update()`; `hdmi.gen()` watched so a `screen()` change resets.
- **Collisions = MMBasic ProcessCollisions, faithfully**: AABB with
  touching-counts for sprite-sprite (`<`/`>`), strict overlap for walls
  (`<=`/`>=`, as MMBasic's static objects — their inconsistency, kept),
  layer partition with layer 0 colliding with all, screen-edge flags,
  **edge-triggered** reporting via a per-sprite contact set (MMBasic's
  `lastcollisions` bitmask, pythonified). Events are `(sprite, other)`
  tuples; optional `on_collision(cb)`. Dropped: the 0xF1/0x80 in-band codes,
  numbered buffers, master/copy bookkeeping (shared image bytearrays),
  SWAP (assign `.img`), per-frame rotation (pre-baked `flip()` copies).
- **`scroll(dx,dy,blank)`** replicates SPRITE SCROLL: scenery shifts with
  wraparound (a cached strip bytearray carries the wrapped band via the
  tuple-surface blit), layer-0 sprites and walls travel with centre-point
  wrap exactly as MMBasic; in F mode the sprites are lifted from N first
  (they're painted into it), and both N and F scroll.
- **C additions (hdmi.c)**: `hdmi.blit` src/dst generalised to
  **`(buffer, w, h)` tuple surfaces** with per-surface geometry/stride
  (sprite images live in PSRAM bytearrays; even width required in 4bpp);
  **`hdmi.vsync()`** — waits for the next vertical-blanking start, pumping
  `mp_event_handle_nowait()` so USB HID polling, the audio feeder and Ctrl-C
  stay alive during the wait (a bare busy-wait would stall input ~16 ms per
  frame — exactly where games hurt); **`hdmi.transparent()`** exposes the
  layer's RGB565 transparent colour (pcsprite's erase colour).
- **Perf shape**: all pixel work is C; Python moves rectangles. ~20 sprites
  of 16×16 ≈ 40 small C blits + trivial arithmetic per frame — comfortably
  inside a 60 Hz budget. `update()`'s state lives in flat lists so the loop
  can be promoted to C later without changing the API.
- **Single-buffered — a note on tearing.** The engine composites *live* (dirty
  rects straight onto the layer in RGB320, or N in F-mode), which core1 is
  scanning top-to-bottom. With many/large sprites the per-pixel skip-blits
  (~1 ms for a few 55×58) overrun the ~1.4 ms vblank, so a sprite redrawn after
  the beam passed its row shows the erased hole — it vanishes near the top, tear
  line drifting with jitter. This is inherent to a single-buffer live
  compositor; it's fine for modest sprite counts. For tear-free animation of
  big/many sprites, **double-buffer in the application** with the existing
  buffer primitives: compose the whole frame into the off-screen F buffer
  (`hdmi.create()`, draw to it via a `Display`/`Image.blit(dst="F")`), then
  `hdmi.vsync(); hdmi.copy("F", "N")` to flip — the only screen write is one
  fast opaque copy. `tests/demo_asteroids.py` does exactly this. (Deliberately
  kept out of the engine to avoid forcing a 150 KB shadow buffer on every game.)

Verify: RGB320 — scenery jpg, 4-sprite sheet, move with keydown() at
`update(vsync=True)` (no flicker, scenery intact); collisions fire once per
contact (sprite/edge/wall); `scroll()` wraps scenery+layer-0 sprites; RGB640
and RGB1024 — same test over the F snapshot incl. `snapshot()` after
redrawing scenery; `screen()` change mid-game resets cleanly.

---

### 35. Console output routing — `console("both"/"serial"/"screen")`

MMBasic `OPTION CONSOLE`: route console *output* so prints don't corrupt the
HDMI screen while testing graphics (or don't clutter the serial log). Input
(USB keyboard + UART) is never affected. **Not persisted — power-up is
always "both".**

- **"serial"** = detach the dupterm screen console (existing `console(False)`
  mechanics; `True`/`False` remain as shorthands).
- **"screen"** = mute the UART side: new `mp_uart_repl_mute` flag in the
  shared `uart.c` (default off, one branch in `mp_uart_write_strn` — inert
  for other boards), exposed by the tiny board-gated **`_sercon`** C module
  (`_sercon.mute()`); `pcconsole.console()` wraps the routing. XMODEM is
  unaffected by the mute (its `_outbyte` writes the UART directly), and a
  muted serial terminal can still type — including Ctrl-C (RX path untouched).
- `sync_terminal()`'s resize escape is muted too in "screen" mode — harmless
  (it only matters to a serial terminal, which isn't listening).

Verify: `console("serial")` → REPL echo continues on the terminal, screen
static while a sprite demo runs; `console("screen")` → terminal silent but
typing there still executes (echo on the monitor); `console("both")`
restores; XMODEM works in all three.

---

### 36. On-device test suite (`boards/PICO_COMPUTER_3/tests/`)

Regression tests for the custom development, shipped in the repo (not
frozen); copy `tests/` to the SD card and `run("/sd/tests/test_all.py")`.
Two tiers:

- **Automatic (self-verifying)**: blit, buffer targets, sprites, images,
  misc. Graphics tests verify by *reading pixels back* (`fb.pixel()`), run
  in every video mode where relevant, and route console output to serial
  (`console("serial")` — §35 built for exactly this) so on-screen prints
  can't corrupt the pixels under test. Collision tests assert MMBasic's
  exact conventions (touching counts for sprites, strict overlap for walls,
  edge-triggered once-per-contact). Image tests use the BMP save→load
  round-trip, which is exact for top-bit colours in every format (the
  888↔native conversions mask the same top bits both ways).
- **Interactive**: keydown/on_key (prompts to hold keys, then asserts the
  codes), audio (human confirms tone/synth/pause; click-free-retune check),
  console routing (markers on each output). All prompts print to both
  consoles.

`testutil.py` is the tiny check/report framework; the runner restores the
saved screen mode at the end. Optional assets (`test.jpg/png/mod/mp3`) are
skipped when absent. Every file also runs standalone.

---

### 37. On-screen editor scrolling — scroll region + reverse index

**Bug (hardware):** the pye editor scrolled correctly over the serial
terminal but on the HDMI console only the single newly-exposed line changed,
not the whole screen. **Cause:** pye scrolls by setting a **DECSTBM scroll
region** (`\x1b[1;{rows-1}r`, reserving the bottom status line) and then
emitting **reverse index `\x1bM`** at the region top (scroll down) or `\n` at
the region bottom (scroll up); it redraws only the exposed line, trusting the
terminal to move the rest. Our `pcconsole` ANSI interpreter ignored `\x1bM`
(non-CSI ESC → dropped) and DECSTBM (`r` fell through `_csi`), and `_newline`
only scrolled at the very last *screen* row — so `\n` at the region bottom
(row 38, above the status line) never scrolled. Real terminals implement all
three, hence serial worked.

Fix, in two parts:
- **`hdmi.scroll` generalised to a directional pixel band**:
  `hdmi.scroll(dy, colour=0, y0=0, height=None)` — scrolls only `[y0,
  y0+height)`, `dy>0` content up / `dy<0` content down, via one banded
  `memmove` + edge fill (helper `hdmi_fill_rows`). Whole-screen up-scroll (the
  old 1–2 arg form) is unchanged, so the REPL path is byte-identical.
- **`pcconsole` scroll-region support**: track `rtop/rbot`; parse DECSTBM
  (`\x1b[t;b r`, `\x1b[r` resets, homes cursor per VT100); implement `ESC M`
  reverse index and (for completeness) `ESC D` index; `_newline` now scrolls
  the *region* when the cursor is at `rbot` (via `_scroll_region(±1)` →
  banded `hdmi.scroll`), leaving pye's status line untouched. Region resets to
  full screen on a mode change (`_resync`). For the REPL (region = full
  screen) behaviour is identical to before.

Verify: `edit("/sd/somefile.py")` on the HDMI screen — arrow past the top/
bottom scrolls the whole text area smoothly, status line stays put; exit
returns cleanly to the REPL; serial editing still fine; REPL scrolling
unchanged.

---

### 38. On-screen console colour (ANSI SGR)

The `pcconsole` terminal emulator now honours **SGR** escapes (`\x1b[…m`), so
on-screen text can be coloured — most visibly, **pye's status bar** (bold white
on blue) and its **marked/selected text** (yellow background) now render in
colour instead of appearing as plain text (their SGR codes were previously
dropped).

- **16-colour ANSI/VGA palette** (`_ANSI`, RGB888): 0–7 normal, 8–15 bright.
  Foreground 30–37/90–97, background 40–47/100–107, plus `0` reset, `1` bold
  (brightens a 0–7 foreground), `7` reverse, and the `22`/`27`/`39`/`49` off
  codes. Colours convert to the framebuffer's native format per mode via
  `Display.colour()` (nearest-of-16 in RGB1024).
- **State** (`_fgi`/`_bgi`/`_bold`/`_rev`) resolves to cached native
  `cur_fg`/`cur_bg`, which `hdmi.putc` uses per glyph; each cell is drawn
  opaquely so a background colour fills the cell. State persists across writes
  (terminal semantics) and resets on `\x1b[0m` and on a mode change (native
  colours differ per format).
- 256-colour / truecolour (`38;5;n` / `38;2;r;g;b`) are not parsed — only the
  16-colour set (which is what pye and typical TUIs use).
- **pye syntax highlighting (second `local:` patch to the vendored editor):**
  base pye only colours its status bar and selection, so a further local patch
  adds a lightweight Python highlighter — `_hl()` inserts zero-width SGR codes
  around comments/strings/keywords/numbers in each visible line slice
  (**MMBasic's editor colour scheme**: keyword cyan, string magenta, comment
  yellow, number green — all bright, from `VT100_C_*` in MMBasic's Editor.c),
  applied
  in the `flag == 0` (no-selection) render branch for `.py` files, gated by
  `Editor.syntax`. It leaves the visible characters (and cursor columns)
  unchanged and caches the plain text in `scrbuf` (so line diffing is
  unaffected). It tokenises each slice standalone, so a horizontally-scrolled
  margin or a triple-quoted string spanning lines can mis-colour — cosmetic
  only. pye resets colour after its status bar, so per-line highlighting never
  bleeds.

---

### 39. Wi-Fi credentials + NTP time (`pcnet`)

MMBasic's `OPTION RTC AUTO` equivalent: set the clock from the internet. New
frozen module `pcnet.py`, injected into the REPL as `wifi`/`ntpsync`/`tz`.

- **`wifi(ssid, pw)`** connects (`network.WLAN(STA_IF)`, ~15 s timeout) and
  persists the credentials via `pcconfig` (`wifi_ssid`/`wifi_pw`); `wifi()`
  reconnects from the saved pair. An explicit SSID forces a fresh connect
  (disconnects first) so changed credentials take effect.
- **`ntpsync()`** — the clock chain **Wi-Fi → NTP (UTC) → local → DS3231**:
  `ntptime.settime()` sets the system RTC to UTC, then `time.localtime(time()
  + tz*3600)` gives local time, written to the DS3231 via `ds3231.settime()`
  (which also re-sets the system clock). So after a sync both the system clock
  and the battery-backed chip hold **local** time and `gettime()` is right
  offline. `tz()` is the persisted UTC offset in hours (fractional allowed).
- **`auto(True)`** persists `ntp_auto`; `_boot_board` calls `pcnet.boot_sync()`
  **last** (after the display + console are up, so connect messages are
  visible), wrapped so any Wi-Fi/NTP failure is swallowed — the DS3231 sync
  earlier in boot always leaves a valid time first.
- **Security (documented):** SSID + password are stored **plaintext** in
  `/settings.json` (no secure element on this board — MMBasic stores Wi-Fi
  creds the same way). The manual's RTC section states this and offers the
  per-session alternative (don't persist; call `wifi()`/`ntpsync()` manually).
- `ntptime` comes from `bundle-networking` (already required); the cyw43 gSPI
  divider is correct at the default 252 MHz (§18), so NTP works out of the box.

Verify: `wifi("ssid","pw")` connects; `tz(1); ntpsync()` sets local time;
`gettime()` correct after a power cycle (DS3231 held it); `auto(True)` syncs at
boot; no network / bad creds never blocks boot.

---

### 40. Richer 2D primitives — arc / rbox / thick line / bezier / flood fill

`framebuf` covers line/rect/ellipse/poly; MMBasic has more. Added the rest —
the vector shapes as clean Python on `pcgfx.Display` (framebuf primitives), the
flood fill in C (per-pixel scanline work, too slow in Python). See
[[replicate-mmbasic-exactly]] — MMBasic's *semantics* are matched; the vector
shapes use MicroPython's proven primitives rather than porting MMBasic's exact
pixel loops.

- **`d.line(..., w)`** — thick line as a **filled quadrilateral** via
  `framebuf.poly` (the 4 corners offset ±w/2 along the perpendicular): gap-free,
  butt caps. `w<=1` falls through to `super().line`.
- **`d.rbox(x, y, w, h, r, colour, fill)`** — rounded rectangle from four
  `framebuf.ellipse` **quadrant-mask** corners (masks 1/2/4/8 = TR/TL/BL/BR) +
  straight `hline`/`vline` edges (+ band `fill_rect`s and quadrant fills when
  filled). `r` clamped to half the shorter side (as MMBasic RBOX).
- **`d.arc(x, y, r1, r2, a1, a2, colour)`** — filled annular sector, MMBasic's
  angle convention (0°=up, clockwise: `x+r·sinθ, y−r·cosθ`). Two paths, both
  **gap-free** (an early radial-spoke version left 1px gaps at the outer edge):
  a **full ring** (`a2−a1 ≥ 360`, incl. `a2==a1`) draws two annulus x-spans per
  row (`±[√(r1²−dy²) .. √(r2²−dy²)]`), no angle test; a **partial sector** fills
  the annular-sector **polygon** (outer arc forward + inner arc back, or the
  apex for r1=0) with one `framebuf.poly` C fill. The polygon replaced a
  correct-but-slow per-pixel-`atan2` scanline (MMBasic's literal `cmd_arc`,
  ~O(annulus·atan2)) — the poly fill is a C scanline with no per-pixel trig, so
  a big partial arc went from ~100 ms to a few ms. Curved edges are
  chord-approximated (~2px chords); a very thin band (`r2−r1 ≤ 3`) additionally
  strokes its outer/inner polylines, since a polygon fill can drop rows there.
- **`d.bezier(points, colour)`** — N-point **Bernstein** curve (binomials
  `C(n-1,i)`), `steps = clamp(bbox_diag·3, 10, 2000)`, drawn as line segments —
  MMBasic's `PlotBezier` scheme.
- **`d.flood(x, y, colour, border)` → `hdmi.flood` (C):** scanline seed fill
  with a growable span stack (`hdmi_flood_push` via `m_renew`), reusing the
  §33 `hdmi_px_get/px_set` helpers so it works in every pixel format. Two modes,
  faithful to MMBasic `floodfill`: **flood** (`border<0`) replaces the seed
  colour; **boundary** (`border>=0`) fills over any colour up to the border. The
  match predicate makes filled pixels stop recursion (flood: now ≠ seed;
  boundary: now == fill). Operates on the current write target (`hdmi_wbuf()`).

Verify: thick lines at various angles (no gaps); `rbox` outline+fill; `arc`
quarter/half/full ring and a thin outline (r1=r-1); a bezier squiggle; draw a
`rect` then `flood` inside it (flood mode) and `flood` a shape to a border
colour (boundary mode); all in RGB320/RGB640/RGB1024.

---

### 41. `load_image()` / `Image` — images into memory (sprite sheets)

The `draw_*` loaders already decode into a generic `(buffer, w, h, bpp)` target
and clip to it (§21) — `pcimage` just fed them the HDMI framebuffer. The blitter
already takes `(buffer, w, h)` surfaces (§34). So loading a sheet **into memory**
needed almost no new machinery — just sizing the buffer before decoding.

- **`load_image(path, transparent, dither, cutoff, scale) -> Image`**
  (pcimage): peeks the image **dimensions** in pure Python — PNG IHDR (offsets
  16/20, BE), BMP (offsets 18/22, LE, signed height), JPEG (`_jpeg_size` scans
  segments for an SOFn marker) — allocates a `bytearray` in the current format
  (`hdmi.bpp()`; even width in 4bpp), optionally pre-fills it with a
  `transparent` native colour via a throwaway `framebuf` (so PNG alpha areas
  become that colour, i.e. the future skip colour), then calls the **existing**
  `jpeg.render`/`png.render`/`bmp.load` with `x=y=0`. No C changes.
- **`Image`**: thin wrapper (`buf`, `w`, `h`); `.surface` = `(buf,w,h)`;
  `.blit(x,y,sx,sy,w,h,dst,skip)` and `.cell(col,row,cw,ch,x,y,…)` are one
  `hdmi.blit` each — blit the whole image or a sheet cell straight from RAM.
- **`Image.sprites(cw, ch, …)`**: cuts the sheet into `pcsprite.Sprite`s that
  **share** the one buffer (no per-cell copy, and it doesn't clobber the F
  buffer like the older `sheet()`). Enabled by extending `Sprite` to reference a
  sub-rectangle of a shared surface — new `sw/sh/sx/sy` ctor args (default =
  the whole own buffer, so standalone sprites are unchanged); `update()`'s blit
  and `flip()` now use the surface geometry + cell offset. `flip()` extracts the
  cell into a standalone flipped copy.
- Buffer lives in the PSRAM GC heap; format is per-mode (reload after
  `screen()`), like the framebuffer.

Verify: `load_image` a PNG/JPEG/BMP; `.cell()`/`.blit()` sub-rectangles;
`transparent=` + skip gives per-pixel transparency from a PNG; `.sprites()`
feed the engine and animate; `flip()` a sheet cell; all in RGB320/640/1024.

---

### 42. Turtle graphics — `pcturtle` (MMBasic TURTLE)

Frozen `pcturtle.py`, a `Turtle` class on top of the §40 `pcgfx` primitives —
faithful to MMBasic's `Turtle.c` semantics (see [[replicate-mmbasic-exactly]]):

- **Conventions**: screen-pixel coordinates, **home = screen centre**, heading
  **0 = up, 90 = right (clockwise)** — `forward` is `x += d·sin(h);
  y −= d·cos(h)`, `right()` increments the heading (verbatim MMBasic). Colours
  are 24-bit RGB, converted to the mode format via `Display.colour()` and cached
  native.
- **Drawn with the primitives**: lines via `pcgfx.line(w=)` (thick pen), arcs by
  MMBasic's step-and-turn (`segs = |angle|/5+1`, chord
  `2r·sin(step/2)`, `forward` + `heading += step`), `wedge` = a filled sector
  (`arc(...,r1=0)`), `circle`/`dot`/`fcircle` via `ellipse`, `rectangle` via
  `rect`/`fill_rect`, `bezier` (control/end points as distance+angle offsets
  from the turtle) via `pcgfx.bezier`, polygon fill (`begin_fill`/`end_fill`)
  via `framebuf.poly`. CPython-style aliases (`fd`/`bk`/`lt`/`rt`/`pu`/`pd`/
  `setpos`/`seth`/…) plus MMBasic names.
- **Target**: draws on `hdmi.fb()` (the current write target), so `write("F")`
  + a `Turtle()` draws off-screen; make a fresh `Turtle` after a `screen()`
  mode change (format differs). Injected into the REPL as `Turtle`.
- **Deviations (deliberate):** the animated on-screen cursor (pixel save/restore
  under a moving turtle sprite) and the 32 fill *patterns* are not ported — the
  cursor is off by default in MMBasic anyway, and fills are solid; `stamp()`
  draws a static heading triangle. Can add later if wanted.

Verify: a square (`forward`/`right` ×4); a filled star (`begin_fill`/`end_fill`);
`arc`/`circle`/`wedge`; `pencolor`/`pensize`; `push`/`pop`; all in
RGB320/640/1024.

### 43. MMBasic bitmap fonts — the full font table (`fonts.h`)

The 8×12 console font (`font1`) was the only glyph set; this adds MMBasic's
other eight, so `hdmi.text()`/`Display.text()` can render in nine fonts.

- **Data**: the PicoMite font headers are vendored under `ports/rp2/fonts/`
  (Misc_12x20, Hom_16x24, Fnt_10x16, Inconsola 24×32, ArialNumFontPlus 32×50
  digits-only, F_6x8, TinyFont 4×6, font8x10), aggregated by `ports/rp2/fonts.h`
  into `hdmi_fonts[]` (1-based numbers match MMBasic). `#included` only by
  `hdmi.c` (gated on `MICROPY_HW_ENABLE_HDMI`), so ~130 KB of glyphs never reach
  other rp2 boards. The `int[]`/`uint32_t[]` fonts are cast to `uint8_t*` — the
  little-endian RP2350 yields the byte order MMBasic uses.
- **Glyph format** (matches PicoMite `Draw.c`): 4-byte header
  `{w, h, first, count}`, then each glyph is `w*h` bits as **one continuous
  MSB-first bitstream** (NOT byte-aligned rows), so pixel `(x,y)` is bit
  `n = y*w + x`: `on = (glyph[n>>3] >> (7 - (n&7))) & 1`. `w*h` is a multiple of
  8 for every font, which makes that exact.
- **API**: `hdmi_blit_glyph()` is now font-parameterised;
  `hdmi.text(s,x,y,fg,bg=-1,scale=1,font=1)` resolves the font once and blits.
  `hdmi.fonts()` lists `(number,w,h,first,count)`. `pcgfx.Display.text()` gained
  `font=`/`scale=`/`bg=` kwargs — called the framebuf way (`text(s,x,y,c)`) it
  is still the built-in 8×8; with `font=`/`scale`/`bg` it delegates to
  `hdmi.text()` (drawing to the current write target). `hdmi.putc()` and the
  console are unchanged (still font 1).

Verify (`tests/test_fonts.py`, automatic): `hdmi.fonts()` metrics for all nine;
a glyph renders (set-pixel count > 0) and advances by its width in each font;
`scale=2` advance; space/transparent draws nothing; out-of-range char / bad font
number / off-screen don't crash. `WATCH=True` shows every font at scale 1 and 2.

### 44. On-screen GUI toolkit — `pcgui` (MMBasic GUI, core set)

Frozen `pcgui.py`: MMBasic's GUI controls (Micromite Plus) reimagined as a small
object-oriented widget toolkit rather than a byte-exact port of the 4900-line
`GUI.c` (which is welded to the BASIC interpreter). The **control set and
behaviour** match MMBasic; the rendering is cleaner (rounded shapes via §40
`pcgfx.rbox`/`arc`, text via the §43 bitmap fonts).

- **Model**: a `GUI` manager owns a control list, resolves RGB→native via
  `Display.colour()`, caches font metrics from `hdmi.fonts()`, and dispatches
  events in `poll()`. Each control is a `Control` subclass with `.value`
  (property; setter redraws), `.enabled`/`.hidden`, a `callback(control)`, and
  `draw()`/`hit()`/`press()`/`drag()`/`release()`/`key()` hooks. Factories on
  `GUI` create+register+draw and return the object.
- **Input**: pointer from `mouse("X"/"Y"/"L")` or `touch("X"/"Y"/"DOWN")`
  (touch preferred while a contact is down, else the mouse cursor); `poll()`
  edge-detects press/drag/release and routes to the control under the pointer.
  Keyboard for text boxes via `keyboard.on_key(cb)` — the scheduled callback
  buffers key codes, drained to the focused box in `poll()` (ENTER=10 commits +
  blurs, BKSP=8, ESC=27, printable filtered for number boxes). Global touch
  hooks `g.on_touch(down/up/move)` (MMBasic's `GUI INTERRUPT TouchDown/TouchUp`)
  fire from `poll()` at the press/drag/release edges with the screen `(x,y)`, in
  addition to per-control callbacks (`up` carries the last live position).
- **Controls (core set)**: caption, frame, button, switch, checkbox, radio
  (mutually exclusive by `group`), LED, gauge (circular, 270° open-bottom arc),
  bar gauge (h/v), slider (draggable), text box, number box (`.number` float).
- **Flicker-free gauge**: the gauge updates *incrementally* — a value change
  repaints only the wedge between the old and new value (fg when growing, track
  when shrinking) and the centred number only when its integer changes, never a
  full clear. Both the value and track arcs are built from one **fixed grid of
  boundary vertices** (≈2px chords), so any sub-range's edges land on exactly the
  same pixels as the arc it overpaints — no fill-colour outline remnant (the bug
  from redrawing arcs whose chord counts depended on their own sweep) and, since
  each contiguous colour run is a single poly fill, no radial seams. The **bar
  gauge** updates the same way — it repaints only the strip between the old and
  new fill level (fg when growing, track when shrinking), never the whole bar, so
  it doesn't flash. The slider thumb is likewise sized to sit inside the control
  box so erasing the box on redraw leaves no trail.
- **Touch text entry**: tapping a text/number box opens `_OnScreenKeyboard`, a
  modal docked at the bottom — alpha (QWERTY + Shift) for text, a numeric keypad
  for numbers, with Del/OK/Esc. It runs its own touch+key loop (also accepting a
  real USB keyboard), waits for the release before returning, then repaints the
  region it covered (`_redraw_region`). So the GUI is fully usable on a bare
  touch panel. Release events use the last live pointer position (a lifted touch
  reports −1), which is what made non-slider controls appear dead on touch.
- **Injection**: `import pcgui` pre-imported into `__main__` by `_boot_board`.
  Build the GUI after the screen mode is set; a mode change needs a fresh `GUI`
  (pixel format differs).

Verify (CPython, stubbed `hdmi`/`mouse`/`touch`/`keyboard`/`pcgfx`): button
click fires / release-outside doesn't; switch & checkbox toggle; radio-group
exclusivity; slider drag sets value + fires; text-box focus→type→backspace→
Enter-commit→blur; number-box filters non-numeric; gauge/bar clamp. On-device:
`tests/test_gui.py` is an interactive control panel (mouse/touch + keyboard).

- **Phase-2 controls**: display box (read-only, interior-only repaint), spinner
  (number + touch up/down arrows, `step`/`lo`/`hi`), list box (scrolling; tap a
  row to select, drag to scroll; `.value` index / `.text` string), format box
  (a number box whose display runs through a `%`-format, `.number` float), and
  area (an invisible touch region firing on touch/drag with `.value` = the
  relative `(x, y)` — a hit region / scribble canvas). `tests/test_gui2.py`.

Full MMBasic GUI control set is now present.

### 45. Tile maps — `pctilemap` + `hdmi.tilemap` (MMBasic TILEMAP)

Scrolling tile-map backgrounds. The render loop (the one performance-critical
part) is C; everything else is Python.

- **C (`hdmi.tilemap`)**: the existing `hdmi_blit` clip/copy core was factored
  into `hdmi_do_blit(sf, df, x, y, w, h, x1, y1, skip)`; `hdmi.tilemap(map, cols,
  rows, tileset, tpr, tw, th, vx, vy, sx, sy, vw, vh, skip, dst)` is then just
  the MMBasic `TILEMAP DRAW` loop over it — walk the visible cell range, skip
  tile 0, blit each tile's sheet sub-rect to the screen with a sub-tile scroll
  offset, clipping edge tiles. `map` is a `uint16` buffer (an `array('H')`),
  `tileset` any blit surface (a `(buf,w,h)` tuple such as `load_image().surface`).
  ~10× a per-tile Python loop, so full-screen scroll stays smooth.
- **Python (`pctilemap.TileMap`)**: holds the tileset, the map array, the
  viewport and a tile→attribute dict. `set/get/fill`, `view/scroll/clamp`,
  `draw()` (drives `hdmi.tilemap`), `blit_tile()` (one tile at a pixel),
  `tile_at()`, `set_attr()/attr()`, `collide(wx,wy,w,h,mask)` (wall/water hit
  test), and `TileMap.load()` for CSV/space map files. Frozen, injected as
  `TileMap`.
- **Game objects**: reuse `pcsprite` / `load_image` (the tileset doubles as a
  sprite sheet) rather than MMBasic's separate tile-sprite subsystem — the
  sprite engine here is already richer.
- **Design note**: the tileset must be in the current screen's pixel format
  (blit copies raw pixels); `load_image()` loads in the active format. For
  flicker-free scroll, compose in the F buffer and `copy("F","N")`.

Verify: `tests/test_tilemap.py` builds a 4×4 tileset in RAM (grass/water/wall/
tree), fills a 48×36 map with a bordered world + pond + trees, and bounces the
viewport around via the F buffer. C DRAW math and the Python layer (map access,
`tile_at`, attributes, `collide` with/without mask, viewport clamp, `draw`/
`blit_tile` argument marshalling) checked on host.

### 46. Maths helpers — `pcmath` (the MMBasic MATH gaps over ulab)

MMBasic's `MATHS.c` is a grab-bag; most of it (statistics, linear algebra, FFT,
element-wise math, **complex** scalar+array) is already covered — and exceeded —
by the **ulab** module compiled into this board (`ULAB_SUPPORTS_COMPLEX=1`,
scipy on, FFT numpy-compatible, linalg inv/det/eig/cholesky/qr/norm) plus core
`complex`/`cmath`. So `pcmath.py` (frozen, injected as `pcmath`) only adds the
verbs ulab lacks, in clean Python:

- **Quaternions** (`Q_*`): a `Quat` class (pure-Python floats) — `from_axis`,
  `from_euler` (aerospace ZYX), `*`, `conjugate/inverse/normalise`, `rotate(v)`
  via the sandwich product, `to_euler`, `to_matrix` (ndarray).
- **3-D vectors** (`V_*`): `vcross/vdot/vmag/vunit`, `vrotate` (Rodrigues).
- **DSP** (`WINDOW/SINC/CROSSING`, FFT power): `window` (hann/hamming/blackman/
  bartlett/rect), `sinc`, `crossings` (level sign-changes), `power_spectrum`
  (|FFT|², one-sided; power-of-two length per ulab).
- **Statistics** (`CORREL/CHI`): `correl` (Pearson r), `chi_square` → `(chi2, p)`
  with the p-value from a self-contained regularised upper-incomplete-gamma
  (Lanczos `lgamma` + NR series/continued-fraction) — no scipy dependency.
- **Control** (`PID`): a `PID` class numerically identical to MMBasic's `MATH PID`
  (PicoMite `MATHS.c` `PIDController_Update`) -- trapezoidal (Tustin) integral with
  a dedicated anti-windup clamp (`int_min/int_max`, MMBasic's `limMinInt/limMaxInt`),
  derivative-on-measurement band-limited by a first-order LPF of time constant `tau`,
  and a fixed sample time `T` seconds (floor `T >= 0.001`, MMBasic's 1 ms). `.start(cb)`
  / `.stop()` mirror `MATH PID START`/`STOP`: they run `cb(pid)` every `T` off a
  `machine.Timer` (MMBasic fires a BASIC interrupt sub each tick instead).

Deliberately **not** wrapped: stats/linalg/FFT/complex (use ulab directly) and
the heavier/niche `SENSORFUSION` (AHRS) — can add later. No firmware change (ulab
was already built in); pure frozen Python.

Verify (`tests/test_math.py`, automatic; also host-tested vs NumPy): quaternion
rotate/compose/inverse, euler round-trip, `to_matrix` vs `rotate`; vector ops;
window ends/symmetry; `sinc`; 4 zero-crossings of two sine periods; power-spectrum
peak bin; `correl` ±1; `chi_square` = 5.8 with p matching the 4-dof closed form
`e^-2.9·3.9`; PID P-term, anti-windup clamp, band-limited derivative (`tau`) and 1 ms floor.

### 47. `autosave()` — paste a program onto the board (MMBasic AUTOSAVE)

`pcshell.autosave(path)` (injected into the REPL) captures everything sent at the
console straight into a file — the frictionless way to get a small program on
without XMODEM or an SD card: run it, paste, press Ctrl-Z. It disables
`micropython.kbd_intr` so Ctrl-Z/Ctrl-D (finish) and Ctrl-C (abort) arrive as raw
bytes, reads `sys.stdin.buffer` byte-by-byte, echoes with exactly one CR/LF per
line break (so CR / LF / CRLF pastes all look right), then normalises endings to
`\n` and writes. Works over serial or the USB keyboard. Host-tested: mixed
endings normalise, Ctrl-D terminates, Ctrl-C writes nothing, no doubled newlines.

### 48. Game-loop timing + plotting — `pcgame` / `pcplot`

Two small frozen helpers aimed at the games/education objective.

- **`pcgame.Clock(fps, vsync=False)`** — a drift-free fixed-cadence frame clock
  (MMBasic `SYNC`). `tick()` holds an absolute `_end` deadline, waits until it
  (sleeps the bulk via `sleep_us`, spins the last ~1 ms for µs accuracy), then
  advances `_end += period` — so a slow frame isn't paid back with drift; the
  cadence stays anchored. Overrun by more than a period re-anchors to now (no
  runaway catch-up). Returns `dt` (seconds since the last tick) for frame-rate-
  independent motion, and tracks a rolling `.fps`. `vsync=True` waits on
  `hdmi.vsync()` instead (locks to the display refresh). Injected as `pcgame`.
- **`pcplot.plot(...)`** — one-call plotting on the HDMI screen for maths/science
  education. Accepts a sequence, a list of sequences (multi-series), or a
  callable sampled over `x=(a,b[,n])`; `style` line/scatter/bar; autoscales,
  draws an axis box, zero lines and range labels (font 7). Draws on `hdmi.fb()`
  with the `pcgfx` primitives. Injected as `plot`.

No firmware change (frozen Python). Verify: `Clock` host-tested for cadence
(10 frames @50 fps = 0.200 s) and re-anchoring after a slow frame; `plot`
host-tested for series parsing (list / function / multi-series), bounds/flat-
data padding, and per-style draw counts. `tests/test_plot.py` is a visual demo
(static plots + a `Clock`-driven scrolling sine showing fps).

### 49. File manager — `pcfm` (MMBasic FM, dual-panel)

`fm(path)` (frozen, injected as `fm`): a **dual-panel** file manager that opens
files by type — the "this is a computer" front end. Pure Python over the console
and the existing helpers.

- **Two panes** (`_Panel`: path/sel/top/entries + geometry), split at the console
  midpoint; **Tab** switches the active pane; **C/M** copy/move the selected file
  to the *other* pane's directory (via `pcshell._copy_file` / `os.rename`),
  reloading the affected pane(s). Single-panel had no copy destination — this
  fixes that.
- **Rendering**: an ANSI UI (`ESC[2J`, cursor-position, reverse-video selection)
  to `sys.stdout` — identical on serial and the on-screen console (CUP/SGR/erase,
  §37/§38). **Incremental redraw**: a cursor move within the window repaints only
  the two changed rows (old + new selection); only a scroll or directory change
  redraws a whole pane — so navigation is snappy (the original full-screen redraw
  per keystroke was slow). Listing via `os.ilistdir` (parent, dirs, files, case-
  insensitive). **Enter is type-sensitive** (run/play/show/view — no separate
  view/play key). The **key legend** greedy-wraps to the screen width across as
  many lines as needed (1–2 normally, 3 at 40 cols) so every command stays
  visible; the **status line shows the selected file's full name** (readable when
  truncated in its pane). `←/→` select the left/right pane, Tab toggles.
- **Keys**: a **blocking** stdin reader decodes ANSI escapes to logical keys
  (arrows/PgUp/PgDn/Home/End/Del) — both the serial terminal and the USB keyboard
  deliver these as `ESC[…` sequences to stdin (mp_usbh translates HID→VT100), and
  an arrow's bytes arrive together so the follow-up reads return at once.
  (`select.poll()` was tried first but **doesn't see the USB-keyboard ring
  buffer** on this board, so every arrow timed out and looked like a lone Esc,
  dropping out of FM — hence blocking reads.) A lone Esc can't be distinguished
  from the start of a sequence, so **Q** is the exit key; `fm()` disables
  `kbd_intr` so Ctrl-C also quits cleanly.
- **Open by type**: `.py` → `pcshell.run`; audio → `pcaudio.play` (background,
  with S=stop, ±=volume); image → `pcimage.draw_*`; text → `pcshell.cat` (paged);
  E → `pcshell.edit` (pye). Plus delete (confirm), rename, mkdir. All reuse
  existing modules — no new C, no new capability, just a front end.
- **Display restore** (MMBasic FM does the same): a launched program may switch
  video mode (a game → RGB320) and leave it, which would garble FM's console.
  `run()` snapshots `(width, height, bpp)` before and, if it changed, restores
  FM's mode via `hdmi.deinit`/`init(pcconfig hdmi_mode/clock)` + `pcconsole.
  console()` (and recomputes the console size); otherwise it just re-attaches
  the console (programs often re-route it to serial).

- **Multi-select** (MMBasic FM parity): **Space** toggles selection of the
  highlighted file and steps down a row (repeated presses sweep a range);
  selected rows render **yellow** (`SGR 33`, combined with the cursor's
  reverse/blue) and the status line appends `[N selected]`. `_Panel.marked` is
  a per-pane set of names — cleared on `set_path`, pruned against the listing
  on every `load()` so vanished files can't be acted on. **C/M/D** operate on
  the whole selection when one exists (sorted case-insensitively), confirm
  once with the count for delete, report `copied/moved/deleted N files`, and
  clear the selection; errors are reported but don't abort the batch.
  Directories (and `..`) are not selectable. Also added while here: C/M now
  refuse when both panes show the **same directory** (a copy onto itself via
  `_copy_file` would truncate the source file).

Host-tested: path normalisation (`..`/`.`/relative), listing sort order, type/
extension detection, directory navigation, the full ANSI key decoder
(every arrow/nav key, Enter/Back/letter, lone-Esc, Ctrl-C), and multi-select
(sweep/toggle, status count, group copy/move/delete, delete refusal keeping
the selection, same-dir guard, mark pruning after external deletion).

---

### 50. Mouse pointer overlay — `pccursor` (MMBasic GUI CURSOR)

Frozen `pccursor.py`: a visible mouse pointer, so mouse-driven GUIs aren't
clicking at coordinates the user can't see. MMBasic's `GUI CURSOR` (the CMM2
command set) as a module: a **save-under sprite** — the pixels beneath the
pointer are read into a buffer before the sprite is drawn and blitted back
when it moves, so it floats over any screen content without disturbing it.

- **Sprites**: MMBasic's two built-in cursors, verbatim from PicoMite
  `Pointer.c` — `ARROW` (13×19, hot point at the tip) and `CROSS` (15×15, hot
  point centred). One int per row, bit *n* = opaque pixel in column *n*,
  drawn in a configurable colour (default white); clear bits transparent.
- **Rendering**: three `hdmi.blit` calls per move (save-under → draw with
  skip-colour → restore), so all pixel work is C; Python bookkeeps one
  rectangle. Buffers are built in the mode's native format (16/8/4 bpp;
  width padded even for RGB1024's packed nibbles) with a transparent value
  chosen to differ from the pointer colour. `hdmi.blit`'s clip-in-step
  semantics make partial off-screen positions save/draw/erase symmetrically,
  so no Python-side clipping is needed. A mode change (`hdmi.gen()` bump)
  voids the saved patch and rebuilds the buffers.
- **API**: `on(shape, colour, x, y)` / `off()` / `hide()` / `show()` /
  `refresh()` (track the mouse; call from the loop) / `erase()` (lift before
  drawing underneath; next refresh repaints) / `move(x, y)` (steer without a
  mouse — tests, keyboard/joystick) / `pos()` / `active()`.
- **pcgui integration** (mirrors MMBasic GUI.c's `CursorHide()` discipline):
  `GUI.start()` turns the pointer on when `mouse("PRESENT")` (opt out with
  `start(cursor=False)`), `stop()` turns it off, `poll()` refreshes it each
  call and lifts it before dispatching a press/drag/release. Every control
  draw path (`Control.redraw`, `GUI._erase`/`_redraw_region`/`redraw`/`cls`)
  erases first so the save-under patch never captures a control mid-change;
  the modal on-screen keyboard erases before its draws and refreshes inside
  its own loop (it replaces `poll()` while open).
- **Tests**: `tests/test_cursor.py` (automatic, in `test_all.py`) — steers
  the pointer with `move()` (no mouse needed) and pixel-verifies hot-point
  placement, save/restore on move, hide/show/erase/off, shape + colour
  changes and corner clipping, in all four video modes.

### 51. Turtle texture fill + crisp fill borders — `hdmi.polyfill`

- **Why**: the turtle's `end_fill()` used `framebuf.poly`, which (a) had no
  MMBasic pattern/texture fills and (b) partially overdrew the outline the
  turtle had traced while walking, leaving a speckled border (in whatever
  pen colours the trace used).
- **`hdmi.polyfill(points, colour, pattern=0)`** (hdmi.c): MMBasic turtle's
  polygon fill, ported verbatim from PicoMite `graphics/Turtle.c`
  (`fill_polygon_scanline` / `fill_polygon_pattern` + the 32-entry
  `fill_patterns` 8×8 table). Even-odd scanline spans, up to 256 crossings
  per line, clipped to the write target; pattern bits anchored to screen
  coordinates (`pattern[y & 7] & 1 << (x & 7)`), gaps leave the background.
  Works in every mode via the px_set helpers (incl. 4bpp RGB121). Note:
  PicoMite's `fill_polygon_pattern` guard caps patterns at 8 despite its
  32-entry table; we accept the full 0..31.
- **`pcturtle`**: `fillpattern(n)` / `fp(n)` (MMBasic `TURTLE FILL PATTERN`,
  0 solid / 1..31 textures; reset() restores 0), and `end_fill()` now runs
  MMBasic's exact end sequence — fill, then re-stroke the outline in the
  pen colour/width if the pen is down (`draw_filled_polygon`'s trailing
  DrawLine loop), which is what keeps the border crisp.
- **Verified** (emulator suite): outline survival after fill went from 65
  to 274 px on a 100-px triangle; checkerboard covers exactly 50% of the
  solid fill's pixels; all 32 patterns render; get/set round-trips.

### 52. Sensor fusion — `pcmath.AHRS` (MMBasic MATH SENSORFUSION)

- **What**: `AHRS()` with `.madgwick(...)` and `.mahony(...)` -- both
  filters ported VERBATIM from MMBasic (PicoMite `core/MATHS.c`,
  `MadgwickQuaternionUpdate` / `MahonyQuaternionUpdate`), including the
  MMBasic defaults (`beta=0.5`; `Kp=10, Ki=0`), the 9-axis/6-axis split on
  whether magnetometer values are supplied, the Mahony integral with
  wind-up clearing when `Ki<=0`, and `StoreQuaternion`'s diverged-filter
  recovery (zero/NaN norm resets to identity). State (quaternion + Mahony
  integral) is per-instance rather than MMBasic's globals -- one `AHRS()`
  per IMU; `dt=None` self-times between calls like MMBasic's `AHRSTimer`
  (capped at 1 s).
- **Units**: gyro rad/s in, angles in radians out (MMBasic scales by
  `OPTION ANGLE`; Python convention is the `math` module's radians).
- **Verified** (emulator): a 30° static tilt converges to roll 30.00°
  (Mahony) / 29.58° (Madgwick, beta 0.5) with pitch 0; re-levelling
  returns both to 0; the 9-axis path settles yaw to magnetic north; the
  Ki integral path holds; the divergence reset restores identity.

### 53. 3D engine — `draw3d` (MMBasic DRAW3D)

- **What**: PicoMite's `graphics/Draw3D.c` ported to a C module
  (`draw3d.c`, shared firmware + emulator like hdmi.c). Vertices as
  MMBasic's unit-vector+magnitude quaternions; `rotate()` with 5-element
  rotation quaternions (`T_Mult`/`q_rotate` verbatim); per-face surface
  normals from the (v1,v2,v0) vertex order; backface culling by the
  camera-ray dot; MMBasic's descending bubble depthsort (centroid or
  max-vertex depth); per-face flags (hide / debug-red / invert normal /
  lighting with ambient) and `LIGHT`; hide/restore/close with the erased
  bounding box; `q_create` (MATH Q_CREATE) and the `DRAW3D()` queries.
  Colours are RGB888 converted per mode at draw time.
- **Drawing**: through new shared helpers in hdmi.c (`hdmi_colour_native`,
  `hdmi_draw_line_raw`, `hdmi_fill_rect_raw`, and `hdmi_polyfill_raw`
  refactored out of hdmi.polyfill), all operating on the current write
  target — so the double-buffered idiom (compose on F, copy to N) works
  exactly as MMBasic's FRAMEBUFFER flow.
- **Not ported yet**: `depthmode=2` (the rp2350 z-buffer hidden-line
  rasteriser); faces are limited to 32 vertices.
- **Verified** (emulator): Peter's bouncing-football test
  (`demos/football.py`, converted from his MMBasic original — 60 vertices,
  32 faces): red-pentagon/white-hexagon pixel census correct with zero
  stray colours, culling shows ~half the faces, the ball moves and
  bounces, hide/restore/set_flags/close guards behave. 300 frames in
  15 ms on the emulator (the engine is C; pace with hdmi.vsync()).

### 54. RGB640_4 — the fast game mode (Peter's diagnosis of the 3D fps gap)

- **Why**: at 378 MHz the football ran 18.9 fps vs MMBasic's 42. Maths was
  already hardware-FP and fills span-based; Peter identified the real gap:
  MMBasic composes in 640×480×4-bit with BOTH the display and its
  FRAMEBUFFER in the 307 KB video SRAM, while our RGB640 (8-bit) fills the
  SRAM with the display alone and `hdmi.create()` puts F in the PSRAM heap
  — every compose write and the 300 KB per-frame copy crossed the PSRAM
  bus.
- **`hdmi.RGB640_4`**: 640×480 in 16 colours (RGB121 palette), scanned by
  the existing 4bpp machinery (core1 expands each 320-byte row through
  `hdmi_map256` into an RGB332 line; standard 640×480 timing, clocks
  252/315/378). The RGB121 fill loop is now geometry-aware (1024×600 or
  640×480; bounds hoisted, hot pair unchanged).
- **`hdmi.create()` in this mode** places F in the second half of the
  static video SRAM (150 KB + 150 KB exactly fills it) instead of PSRAM —
  MMBasic's exact layout. RGB320's second half still belongs to the layer;
  RGB1024 fills the whole array; both keep the heap for F.
- **Verified** (emulator): geometry/bpp; F-in-SRAM create/write/isolation/
  copy; the football renders in 16 colours with zero off-palette nibbles
  (red/white/black map exactly to palette 8/15/0); RGB1024 regression.
- ~~Noted for later~~: the 4bpp nibble-order mismatch found here is FIXED
  in §56 — GS4_HMSB is now the single convention.

### 55. screen() lockup at 315/378 MHz — boot2 flash divider (PICO_FLASH_SPI_CLKDIV)

- **Symptom** (Peter's bench): `screen(RGB640/RGB320, 315 or 378)` hard-hangs;
  a direct `hdmi.deinit(); hdmi.init(mode, clock)` works at every clock.
  Difference: `screen()` also SAVES the settings — two flash writes.
- **Root cause** (latent since v0.8, first exercised now): after every
  erase/program, pico-sdk's `flash_range_*` re-enters XIP **via boot2**,
  which times the flash at `clk_sys / PICO_FLASH_SPI_CLKDIV`. The Pimoroni
  board header defaults that divider to **2**: fine at 252 MHz (126 MHz
  reads), fatal at 315/378 (157/189 MHz) — the first post-write fetch of
  flash-resident code returns garbage before `end_critical_flash_section`
  can re-apply our dynamic timing. Exactly the failure MMBasic guards with
  its `PICO_FLASH_SPI_CLKDIV=4`.
- **Fix**: the board cmake now defines `PICO_FLASH_SPI_CLKDIV=4`
  (378/4 = 94.5 MHz in the boot2 window — within the W25Q's fast-read
  spec). Steady-state XIP speed is unchanged: the dynamic timing is capped
  separately by `MICROPY_HW_FLASH_MAX_FREQ` (63 MHz) in mpconfigboard.h.
  Verified the define reaches the bs2_default stage2 build.
- **Needs hardware confirmation**: `screen(hdmi.RGB640, 315)` (and 378, and
  RGB320 variants) after flashing a CLEAN build.

### 56. 4bpp nibble order unified on framebuf's GS4_HMSB

- **Why**: `hdmi.fb()` wraps the 4bpp framebuffer as framebuf `GS4_HMSB`
  (even/left pixel = HIGH nibble), but the C side — px_get/px_set, the
  glyph/box writers, hspan edges, `hdmi_map256` (and with it the scanout
  and the emulator compose), the test pattern, the image loaders' 4bpp
  writes and bmp_save's framebuffer read — used even = LOW nibble. Mixing
  Display drawing with C drawing in RGB1024/RGB640_4 produced a
  column-pair swap. Since framebuf's format is fixed (MicroPython has no
  GS4_LMSB), the C code now follows framebuf: even pixel = high nibble,
  everywhere.
- **Sites flipped** (12): hdmi.c map256 build / test pattern / px_get /
  px_set / two glyph-box writers / hspan edge nibbles; bmp.c save-side
  framebuffer read + two 4bpp writes; jpeg.c two writes; png.c one write.
  (bmp_decoder.c reads BMP *files* and outputs RGB — untouched.)
- **Verified** (emulator, RGB640_4 and RGB1024): framebuf reference byte
  layout; C polyfill produces byte-identical packing; an odd-x fill edge
  touches only the low nibble; C-drawn text reads back exactly through
  `d.pixel()`; the football census stays clean. Firmware compiles.
- **Bench note**: anything previously drawn via Display in RGB1024 (the
  console included) will now render with the columns the right way round.

### 57. draw3d depthmode 2 — z-buffer hidden line (the Elite mode)

- **What**: the last unported DRAW3D feature. `show(n, x, y, z, nonormals,
  2)` projects every visible face once, rasterises them into a 1/z buffer
  bounded by their union bbox (edge-function triangles, fan-triangulated,
  keeping the nearest 1/z per pixel), then draws outline-only faces
  (`fill` index `None`) edge-by-edge with a per-pixel depth test — edges
  behind the model's own body are removed, the classic Elite wireframe.
  Filled faces still paint via the painter's sort from the same cached
  projections. Ported verbatim from PicoMite `Draw3D.c`
  (`hiddenline_raster_triangle` / `hiddenline_draw_edge` /
  `hiddenline_get_zbuf`).
- **Adaptations**: MMBasic's `GetTempMainMemory` scratch (projections +
  visibility) is one `m_new` block freed at the end of `display3d`; the
  grow-only z-buffer cache is an `MP_REGISTER_ROOT_POINTER` (worst case
  full-screen 640×480 floats = 1.2 MB from the PSRAM-backed GC heap; the
  capacity static is not trusted when the root pointer is NULL, so a soft
  reset can't leave it stale), released by `close_all()` like MMBasic's
  `closeall3d`. MMBasic's is_hiddenline_target check (memory targets only)
  is vacuous here — every hdmi target is a memory buffer. `hdmi_pixel_raw`
  added to the hdmi_priv.h drawing helpers for the depth-tested plot.
- **Depth test: MMBasic's, verbatim** (`izf >= zbuf - 0.0005`, absolute in
  1/z). A bench episode is worth recording: the converted Cobra demo
  initially looked "nowhere close to correct", and two successive
  "improvements" to the epsilon (relative 5%, then a face-id buffer with a
  0.2% tolerance) were built and later REVERTED — visual renders finally
  showed the real fault was in the DEMO, not the engine: it drew with
  `nonormals=1`, so every rear face's outline of a thin, nearly
  top/bottom-symmetric hull landed on screen as doubled clutter that no
  depth test could fix (and the face-id version quietly dashed visible
  edges wherever a Bresenham pixel strayed onto a steep neighbouring
  face). The fix: convert BASIC demos with their culling intact —
  MMBasic-convention windings transfer AS-IS from screen-space `crossZ>0`
  data — and let depthmode 2 clean up only what culling can't. With that,
  the verbatim epsilon is fine at MMBasic's scene scales (close to the
  camera; at long range 1/z differences drop below 0.0005 and hiding
  fades, which is inherent). Verified visually (emulator BMP renders) and
  exactly: unrotated Cobra culled+hidden == stern-only reference to 1 px
  (Bresenham direction asymmetry), tumbled poses strict subsets, cube
  suite at close range. Lesson: pixel-count assertions couldn't see any
  of this — RENDER AND LOOK.
- **API extra**: `create()` now accepts per-face `None` (or `-1`) in the
  `fill` index list — mixed solid + wireframe objects, the split the
  hidden-line renderer draws natively (MMBasic's fill array is
  all-or-nothing at CREATE, but its renderer handles the mix; ours can
  express it).
- **Verified** (emulator): face-on cube at z=200/viewplane 100 — far
  square 0% drawn, near square 100%, all 12 edges present in painter mode;
  rotated generic view removes only occluded pixels (hidden result is a
  strict subset of the painter render, zero strays); `hide()`/`restore()`
  round-trips preserve depthmode 2; mixed solid+wireframe renders both;
  `tests/elite.py` (tumbling wireframe ship) pixel-census clean in RGB640
  and RGB640_4. Firmware compiles.

### 58. RGB320: create() before layer() puts F in fast SRAM (v0.9)

- **What**: RGB320's second half of video SRAM was reserved for the layer
  unconditionally; the F buffer always went to PSRAM. Now the space is
  first come, first served: `hdmi.create()` called while no layer exists
  places F in the second SRAM half (the RGB640_4 fast-game layout — 16-bit
  double buffering with compose and copy entirely in SRAM), and
  `hdmi.layer()` then raises `layer RAM in use by the F framebuffer --
  close('F') first`. Calling `layer()` first keeps the old behaviour:
  `create()` falls back to PSRAM and both coexist.
- **How**: `hdmi_create()`'s SRAM condition became `!hdmi_layer_on &&
  hdmi_fb_bytes()*2 <= sizeof(hdmi_fb)` (covers RGB640_4 and RGB320;
  RGB640/RGB512/RGB1024 fill the whole array so they still heap-allocate);
  `hdmi_layer_fn()` errors when `hdmi_framebuf_f == hdmi_fb +
  hdmi_fb_bytes()`. No free-path changes needed: close("F")/deinit only
  NULL the root pointer, valid for both placements. Scanout untouched
  (layer merge is still gated on `hdmi_layer_on`).
- **Verified** (emulator): RGB320 create-first → layer() raises, close("F")
  → layer() succeeds; layer-first → create() heap path, both targets
  drawable and copy/blit work; RGB640_4 placement unchanged; football
  regression census clean.

**v0.9 version bump**: `PICO_COMPUTER_3_VERSION` "0.9" (banner +
`os.uname().machine`), emulator banner, manual header/footer.

### 59. "Dead REPL" after interrupting a double-buffered program

- **Symptom** (Peter's bench, after Ctrl-C-ing cobra.py): the REPL stops
  responding to keys. Not a crash and not a heap problem: the program was
  interrupted while `hdmi.write("F")` was selected, so its restore code
  never ran and ALL console output — the KeyboardInterrupt traceback, the
  prompt, every keystroke echo — kept rendering into the invisible
  off-screen buffer. (Blind-typing `hdmi.write("N")` recovers.)
- **Fix, both layers**: `pcshell.run()` now restores `hdmi.write("N")` in
  its `finally` (as MMBasic does when a program ends, however it ends);
  and the three double-buffered demos (football/elite/cobra) wrap their
  frame loop in try/finally that restores the target and closes F even on
  Ctrl-C.

### 60. hdmi.RGB320_8 — 320×240×8-bit: N + layer + F all in fast SRAM

- **Peter's insight**: a 320×240 RGB332 buffer is 76,800 bytes — a QUARTER
  of the 307,200-byte video SRAM. So the display, the overlay layer AND the
  create() double buffer all fit on-chip together, with a quarter still
  spare (kept in reserve — Peter's call — for a possible second F/triple
  buffer later). It is the only mode with an overlay *and* a fast double
  buffer simultaneously; MMBasic precedent is SCREENMODE5 in HDMI.c
  ("half-res x8bit-colour, line + pixel doubled"), whose byte-wise layer
  merge the new core1 fill-loop branch replicates verbatim.
- **Layout**: N at `hdmi_fb`, L at +76,800 (the generic
  `hdmi_fb + hdmi_fb_bytes()` formula unchanged), F at +153,600
  (create() special-cases the mode — the generic "second half" rule would
  have collided with the layer). No first-come-first-served rule here:
  layer() and create() coexist in any order.
- **hdmi_native disentangled**: it used to mean both "8bpp RGB332 format"
  and "scan the framebuffer directly / core1 idles". RGB320_8 is the first
  mode that is native-format but core1-doubled, so `hdmi_native` stays the
  FORMAT flag (=1 here: GS8 framebuf, strides, colour conversion, bpp()
  all follow free), and the backend's two direct-scan checks (DMA IRQ +
  fill-loop idle) now test `hdmi_mode == HDMI_MODE_RGB640` instead. The
  emulator compose had the same entanglement, fixed the same way.
- **hdmi_layer_transp** carries an RGB332 byte in this mode (low byte of
  the uint16); layer() clears the layer with a plain memset. Transparency
  matches at 3-3-2 colour resolution — documented in the manual.
- **Latent bug fixed in passing**: `hdmi_backend_in_blanking()` grouped
  modes by NAME (RGB640/RGB320 → 640×480 blanking) so RGB640_4 was using
  the 1024×600 blanking count for vsync()/in_blanking — wrong window,
  never noticed because both counts are small. Now grouped by timing:
  only RGB512/RGB1024 use X_BLANKING_COUNT.
- **Verified (emulator, SDL dummy)**: geometry/bpp/format, RGB332 colour
  formula, N+L+F coexistence in both orders, independent targets, copy
  matrix, text+blit on the 8-bit paths, transparent(), close("L")
  semantics, 30 vsyncs ≈ 494 ms (60 Hz), draw3d, and the RGB320
  first-come-first-served regression. Full tests/test_all.py: 252 PASS.
  Firmware compiles clean; needs Peter's hardware pass (real core1
  merge + monitor lock).
- **Build-hygiene lesson (cost an hour)**: `rm -rf build-pc3` is NOT a
  clean build for the emulator. The usermod objects land OUTSIDE the
  build dir (`micropython/*.o`+`*.P` at the repo root, `ports/lib/` for
  ulab — py.mk's path for `../`-relative sources). After a change that
  shifts the QSTR pool (any new MP_QSTR), the surviving stale objects
  link silently with SHIFTED QSTR INDICES — names scramble at runtime
  (`import ulab` answered as module 'uint8'; mouse/pccursor misbehaved)
  with no build error. A real clean is:
  `rm -f *.o *.P && rm -rf ports/lib ports/unix/build-pc3` from the repo
  root. (This upgrades the "*.o at repo root" wart from noise to
  hazard — worth fixing the object paths properly one day.)
- **Two pre-existing issues found while testing (NOT from this change,
  both reproduced on an unmodified baseline build)**: (1) test_cursor
  fails 5 white-pixel checks in RGB1024 only (arrow tip/transparency/
  repaints; the red cross passes) — looks like white/index-15 colliding
  with something in pccursor's 4bpp path; may be visible on hardware
  too. (2) The emulator can segfault DURING PROCESS TEARDOWN (after
  exit(0), exit status still 0; bash prints "Segmentation fault"
  intermittently): a helper thread (likely SDL audio) races the exit.
  Repro: `-c "import emuboot; ...; hdmi.deinit(); hdmi.init(...);
  pcconsole.console(); input('q')"` with stdin at EOF, ~1 in 2. A
  proper fix would stop the SDL/audio threads in an atexit hook.

### 61. Saved Wi-Fi password: scrambled and board-bound (reviewer feedback)

- **Why**: a book reviewer flagged that `wifi()` stored the password in
  plaintext in `/settings.json` yet the chapter advised a "per-session,
  don't save" path that the code didn't actually offer. Peter's steer:
  add a light obfuscation keyed to the board so the password isn't
  human-readable and can't be carried to another machine — accepting
  up front that it's not real protection.
- **Scheme** (`pcnet.py`): keystream = repeated
  `sha256(b"pc3-wifi-v1" + machine.unique_id() + counter)`, XORed with
  the password bytes, stored base64 under `wifi_pw_enc`. Decrypt is the
  same XOR (`unique_id()` chosen over the WLAN MAC: always available, no
  radio needed, stable per board). `pcconfig.unset()` added to purge the
  key; a legacy plaintext `wifi_pw` is migrated to the scrambled form on
  first read. `wifi(..., save=False)` connects without persisting — the
  real per-session path the book had promised.
- **Honest boundary** (documented in the module note + manual §11): this
  is obfuscation, not encryption. The board unscrambles its own password,
  so anyone with the board + a REPL recovers it; the value is that it's
  not readable at a glance and won't decode on a different `unique_id()`.
  SSID stays plaintext (not secret).
- **Verified (emulator)**: round-trip; blob never contains the plaintext;
  settings.json shows only base64; legacy-plaintext migration; a
  different key yields undecodable output; empty password round-trips.
  Pure-Python frozen-module change — the user builds the firmware.

### 62. USB gamepad support — `gamepad()` (MMBasic port)

- **Why**: the last big firmware gap; Peter now has a controller to test
  with. Ported MMBasic's (PicoMite) USB HID gamepad stack.
- **The seam was ready**: mp_usbh.c already reserved slot 3 (`HID_PAD`)
  for protocol-NONE HID devices and polled it — reports were just "not
  yet decoded". Mirrored the existing usb_mouse.c/usb_mouse_mod.c
  pattern: new `usb_gamepad.c` (decoders + state) + `usb_gamepad.h` +
  `usb_gamepad_mod.c` (the `gamepad` Python module), wired into the three
  mp_usbh.c callbacks (mount NONE → usb_gamepad_mount; report → decode;
  umount → clear).
- **Decoders ported VERBATIM** from USBKeyboard.c: `checkpush` +
  `process_generic_gamepad` (the 7-entry known-controller `Gamepads[]`
  table + a user `MyGamepad` mapping), `process_xbox`, `process_sony_ds3`,
  `process_sony_ds4` (incl. the `sony_ds4_report_t` bitfield struct and
  gyro/accel). VID/PID dispatch: DS4 → DS3 → Xbox → generic. State fields
  mirror MMBasic's `nunstruct` (ax/ay left stick, Z/C right, L/R triggers,
  x0 button bitmap, imu[6]); the 16-bit button layout and the `p_*` bit
  positions are the documented MMBasic ones.
- **API** (`gamepad` module, injected at boot like `mouse`): mirrors
  MMBasic `DEVICE(GAMEPAD n, "...")` — `gamepad("LX"/"LY"/"RX"/"RY"/"L"/
  "R"/"B"/"H"/"GX".."AZ"/"T"/"CHANGED"/"RAW"/"PRESENT"/"SLOT" [, chan])`,
  `gamepad.configure(vid,pid,mapping)` (MMBasic GAMEPAD CONFIGURE, 32
  ints), `gamepad.mask(chan,bits)`, and button-bit constants `gamepad.A`
  etc. chan 0 = first connected pad. Channel = 1-based HID slot (3 or 4).
- **Emulator**: no USB host, so a `gamepad.py` no-device shim + emuboot
  injection keep `gamepad()` importable/callable there (returns 0 /
  PRESENT=0 / H=0xFF), matching mouse.py/touch.py.
- **Hardware-validated 2026-07-21** (over the COM3 serial REPL, driving a
  live board): a generic USB gamepad enumerated as type 130 on slot 3 and
  **all 12 buttons decoded correctly** — D-pad (via the axis-threshold
  codes 64/192), the four face buttons, Select/Start, L/R — each verified
  against the raw report bytes. The whole path is proven end to end:
  mount → poll → `Gamepads[]` match → `process_generic_gamepad` +
  `checkpush` → the button bitmap → the `gamepad()` query.
- **API polish** (f4eae35fb): the button-bit constants live on the
  gamepad MODULE but the injected REPL name was the query FUNCTION, so
  `gamepad("B") & gamepad.A` (as the manual shows) raised AttributeError.
  Now a small `_GamepadProxy` is injected — one `gamepad` that is both
  callable AND delegates attribute access to the module (button
  constants, `configure`, `mask`). Verified in the emulator.
- **DualShock 3 hardware-validated 2026-07-21**: DS3 (type 129) works on
  this same clean code — Peter used it (LX etc.) on and off over 25
  minutes with no freeze. IMPORTANT LESSON: an earlier test showed the
  DS3 report "frozen" and I mistook it for a decode/keepalive bug, then
  added a DS3 "set operational" SET_REPORT(0xF4) wake + a poll-loop
  stall-recovery that re-issues requests. BOTH WERE WRONG and made it
  WORSE (the DS3 stopped streaming even fresh). Root cause of my error:
  (1) MMBasic's poll model — which this port already copies verbatim —
  just waits on the outstanding tuh_hid_receive_report and only re-issues
  after a report arrives; it NEVER re-issues on silence or sends a DS3
  enable (Peter confirmed). (2) The momentary "freeze" was the DS3 idle,
  not a bug. All the DS3 experiments were reverted (uncommitted) back to
  d2160d516. DO NOT re-add a DS3 enable or a stall re-issue — they break
  it. The verbatim decode + MMBasic wait-for-report poll is correct.
- NOT ported: the PS4 output report (rumble/lightbar), the
  Wii-nunchuck-over-I2C path, and MMBasic's `GAMEPAD MONITOR` auto-print
  (the `"RAW"` query serves the same mapping-discovery purpose).

### 63. USB CDC (serial) host — `USBSerial` (MMBasic port)

- **Why**: the last USB-host gap. A USB-serial adapter plugged into the
  host port should read/write like a UART. Ported MMBasic's CDC host
  (PicoMite Serial.c).
- **Config** (`shared/tinyusb/tusb_config.h`, under `MICROPY_HW_USB_HOST`
  so PC3-only): `CFG_TUH_CDC 4` + the vendor bridges
  `CFG_TUH_CDC_FTDI/CP210X/CH34X` (so FTDI/CP2102/CH340 adapters work, not
  just true CDC-ACM) + default 115200-8-N-1 line coding on enumerate.
  Same as MMBasic.
- **Glue** (`usb_cdc.c`): the `tuh_cdc_mount_cb`/`umount_cb`/`rx_cb`
  callbacks; `rx_cb` drains `tuh_cdc_read` into a per-interface 512-byte
  receive ring buffer (`py/ringbuf.h`). Unlike HID, CDC needs no manual
  polling — `tuh_task()` (already pumped from the event hook) drives it
  and fires `rx_cb`. Line coding via `tuh_cdc_set_line_coding` with the
  `set_baudrate` fallback for FTDI/CP210x, + DTR/RTS. Settings kept across
  a replug (MMBasic's transparent-reconnect behaviour). Init the ring
  buffers from `mp_usbh_init`.
- **Presentation** (`usb_cdc_mod.c`): a `USBSerial` class implementing the
  **same stream protocol as `machine.UART`** (`.read/.write/.ioctl` +
  `MP_TYPE_FLAG_ITER_IS_STREAM`), so `read/readline/readinto/write/flush/
  any` all behave like a UART. Constructor/`init`:
  `baudrate, bits, parity, stop, index, timeout, timeout_char`. Extra:
  `connected()` and `USBSerial.on_change(fn)` (a single rooted callback,
  fired `fn(index)` — a small int is allocation-free so scheduling from
  the tuh_task context is safe). Injected at boot as `USBSerial`
  (`_boot_board.py`); emulator gets a no-device `usbserial.py` shim.
- **Hardware-validated 2026-07-21**: a second RP2 running MMBasic (USB
  CDC, VID:PID 2e8a:0009) plugged into the host port mounted as
  `USB serial connected on port 0`. Over the PC3 REPL: `USBSerial(115200)`
  → `connected() True`; writing `PRINT 6*7\r\n` to it and reading back
  returned the MMBasic echo + ` 42 ` + prompt — a full write/read
  round-trip through the link. Enumeration, line coding, write
  (tuh_cdc_write+flush) and read (rx_cb → ring buffer) all proven.
- Two build gotchas hit: `MP_REGISTER_ROOT_POINTER` must live in a
  QSTR-scanned file (moved to `usb_cdc_mod.c`, used from `usb_cdc.c` via
  `MP_STATE_PORT`); and `make_new` must use its `type` param, not a
  `static` forward-declared type (clashes with `MP_DEFINE_CONST_OBJ_TYPE`).

### 64. Book chapter 32 "Standing on others' shoulders" — external libraries via `mip`, hardware-validated

- **What**: a new course-book chapter on *finding, judging and installing*
  the third-party MicroPython libraries `mip` reaches — the craft the old
  one-paragraph mention in chapter 11 never taught. Chapter 11's library
  section is slimmed to a forward pointer.
- **Renumber**: inserting at 32 pushed the three "Under the Hood" chapters
  down — old 32/33/34 → **33/34/35**. Swept every cross-reference: prose
  `chapter NN`, the appendices' `ch. NN` / `Ch. NN` / table-column /
  "your own shelf `(NN)`" forms, `OUTLINE.md`, `README.md`, and the
  regenerated `examples/` tree. Chapter sequence verified contiguous 1–35.
- **Examples**: auto-extracted into `examples/ch32/` (`qr.py`, `gpslook.py`,
  `where.py`, `card.py`) by `extract_examples.py`; its one hard-coded lib
  ref `bench.py` moved `ch33`→`ch34` to stay correct. Install snippets in
  the chapter use `>>>` REPL style so the extractor skips them.
- **Three worked examples across the range** (the transferable method:
  interface → recognise the data → search → judge → install → bind):
  QR codes with **uQR** (pure software, no hardware — draws a scannable QR
  of the project repo on screen), GPS with **micropyGPS** on UART0
  (GP0/GP1), and an I2C sensor driver. Plus a "is this library any good?"
  checklist including the single- vs double-precision question.
- **Hardware-validated 2026-07-21** over COM11 (PC3 on v0.9 firmware). Both
  libraries installed *on-device* by the **raw-file `mip` method** the
  chapter teaches — neither has a `package.json`, so
  `mip.install("https://raw.githubusercontent.com/JASchilz/uQR/master/uQR.py")`
  and the equivalent `inmcm/micropyGPS` URL → copied to `/lib`. All four
  examples ran via `run("…")` (real `__main__` guard): QR scanned and
  opened the repo; `gpslook.py` showed raw NMEA with a **DGPS fix, 13
  sats**; `where.py` parsed `52.021631666666664N 0.28759E` — the 17-digit
  latitude is the double-precision advantage made literal (a 32-bit board
  truncates to ~`52.02163`, losing metres, which is why micropyGPS defaults
  to `'ddm'`); `card.py` drew the business card from `os.uname().machine`.
- **Serial-push tip**: `autosave("f.py")` + Ctrl-Z is the reliable way to
  push a multi-line file over the 115200 console — raw paste mode
  (Ctrl-E/Ctrl-D) drops characters on long bursts (no flow control).

### 65. On-device SQLite — `usqlite` C user-module

- **Library**: usqlite (MIT), a MicroPython **C** user-module wrapping the
  **SQLite 3.47.0** amalgamation. Vendored as a git submodule at `lib/usqlite`,
  pointing at our fork **`UKTailwind/usqlite`** (forked from `spatialdude/usqlite`
  v0.1.8) pinned to **`7399e48`** on branch `pc3-micropython-1.29` — that commit
  is v0.1.8 plus the one MicroPython-1.29 fix below. Wired in via one
  `list(APPEND USER_C_MODULES …/lib/usqlite/micropython.cmake)` line in
  `mpconfigboard.cmake`, next to ulab — its own `micropython.cmake` self-links
  into `usermod` and registers the module, so there is **no `MICROPY_PY_*`
  switch and nothing to freeze** (it is pure C; `import usqlite` just works).
- **Why it fits this board**: SQLite needs a real heap and allocator. usqlite's
  stock config (`SQLITE_ZERO_MALLOC`, no MEMSYS5) routes SQLite
  malloc/realloc/free to MicroPython's `gc_alloc`/`gc_realloc`/`gc_free`, i.e.
  the **8 MB PSRAM GC heap** — automatically, no config. Storage uses
  `SQLITE_OS_OTHER=1` + usqlite's VFS (`usqlite_vfs.c`/`usqlite_file.c`) over the
  MicroPython VFS, so DB files live on **LittleFS (`/`) or SD (`/sd`)**.
  `SQLITE_THREADSAFE=0`, single connection — matches the single-Python model.
- **Config kept stock**: `usqlite_config.h` already omits ANALYZE / EXPLAIN /
  UTF16 / load-extension / deprecated / shared-cache etc.; FTS/JSON/RTREE off.
- **Cost**: adds the ~250k-line `sqlite3.c` to a clean build; firmware flash grew
  ~0.37 MB (1.26 → **1.62 MB**), still well inside the 4 MB region. Static RAM cost
  is tiny: `usqlite.c.o` (which `#include`s `sqlite3.c`) is 369 KB flash but only
  ~6.6 KB data+bss; all usqlite objects ≈ **7 KB RAM**.
- **Two source-level changes were required** (both recorded in Files touched):
  - `usqlite_cursor.c`: `mp_obj_is_type(v, &mp_type_float)` → `mp_obj_is_float(v)`.
    MicroPython 1.29's `mp_obj_is_type` has a compile-time static-assert
    forbidding `&mp_type_float`; usqlite v0.1.8 predates it. Committed to our fork
    (`UKTailwind/usqlite` `7399e48`) so clones build reproducibly — the submodule
    is pinned to that SHA. To pull upstream updates later, rebase the fork branch
    on a newer usqlite tag and re-pin.
  - `memmap_rp2350/section_extra_post_platform_end.incl`: SRAM GC-heap floor
    **64 KB → 56 KB**. This board packs ~300 KB HDMI framebuffer + ~33 KB audio
    into static SRAM (fast-RAM by design), leaving the SRAM GC arena at 63.57 KB;
    usqlite's ~7 KB tipped it **444 bytes** under the old 64 KB `ASSERT`. Safe
    because the real heap is the 8 MB PSRAM (`gc_add` at runtime); the SRAM arena
    is only the primary/spill block. New floor leaves ~7.5 KB headroom.
- **ARM-only**: builds for the single `rp2350` (ARM) target; the RISC-V variant
  was removed (§ board summary), so there is one build to validate.
- **Caveats**: shared GC heap means large queries raise GC pressure / can
  fragment (fine at 8 MB); LittleFS gives only loose `fsync`/locking, so use
  **one connection at a time**. API is usqlite's own, **not** CPython DB-API 2.0.
- **Hardware-validated 2026-07-22** over COM11 (PC3 on the 0.10+usqlite build).
  Pushed a test via `autosave` and ran it: `import usqlite` → SQLite 3.47.0;
  `executemany` (multi-statement CREATE in a txn) + parameterized `execute`
  inserts with **REAL** values (3.14/2.71/42.0) round-tripped, `SUM`=47.85
  (exercises the patched float-bind path); closed and **reopened** `/sqltest.db`,
  `WHERE temp > ?` with a float param returned `['alice','carol']` — proving
  on-flash persistence. `RESULT PASS`.
- **Emulator parity**: the unix `pc3` emulator variant compiles usqlite too, so
  `import usqlite` behaves identically there. Wired in `emulator/src/micropython.mk`
  via the same `USERMOD_DIR`-swap include used for ulab, with the vendored
  amalgamation's objects given `CWARN := -w` (the unix build's `-Werror` lands
  after `CFLAGS_USERMOD`); `lib/usqlite` added to the variant's `GIT_SUBMODULES`.
  Same test run through a headless `emuboot` boot (SDL dummy video) with
  `/sqltest.db` on the emulator's mounted flash → `RESULT PASS`, byte-identical
  output to the hardware. usqlite is part of the C-module set (like ulab), so the
  SDL-less terminal-only fallback build omits it, by design.

### 66. `pcmath.PID` rewritten to match MMBasic `MATH PID` exactly + v0.11 bump

The `PID` class was a plain textbook controller; rewrote it to be numerically
identical to PicoMite `core/MATHS.c` `PIDController_Update` (the "Phil's Lab"
band-limited form): trapezoidal (Tustin) integral with a **dedicated** anti-windup
clamp (`int_min`/`int_max` = MMBasic `limMinInt`/`limMaxInt`), derivative-on-
**measurement** (no setpoint kick) band-limited by a first-order LPF of time
constant `tau`, at a **fixed** sample time `T` seconds (floor `T >= 0.001`,
MMBasic's 1 ms). New signature `PID(kp, ki, kd, tau, T, out_min, out_max,
int_min, int_max)` — the struct's 9 config fields in order — and
`update(setpoint, measurement)` mirrors `MATH(PID ch, setpoint, measurement)`.
`.start(cb)`/`.stop()` run it off a `machine.Timer` (= `MATH PID START`/`STOP`;
MMBasic instead fires a BASIC interrupt sub each tick, polled between statements
in `checkdetailinterrupts()`).
- `int_min/int_max` default to `out_min/out_max` when omitted (friendlier than
  MMBasic, where zeroed integrator limits kill integral action); documented.
- **`tau = 0` is degenerate** (recursion pole at +1: the D term integrates
  rather than differentiates) — valid only when `kd = 0`; set `tau > 0` (a few ×
  `T`) whenever `kd > 0`. Documented in the header + manual.
- **Verified** in the `pc3` emulator: a closed-loop thermal-plant step settles to
  setpoint (steady-state error ~0, drive matches the analytic value); the D-term
  step kick matches the closed form `2·Kd/(2·tau+T)` to full precision and larger
  `tau` cuts kick + noise monotonically; `.start()` timer fired ~50×/0.5 s at
  T=10 ms; `tests/test_math.py` PID checks (P, anti-windup, band-limited D ==
  MMBasic, 1 ms floor) all pass.

**v0.11 version bump**: `PICO_COMPUTER_3_VERSION` "0.10" → "0.11" (banner +
`os.uname().machine`).

### 67. usqlite hardening — full review, GC-safe lifecycle, power-fail recovery

A full review of the usqlite module (§65) found and fixed three classes of
defect. All in the fork (`UKTailwind/usqlite`, branch `pc3-micropython-1.29`);
hardware-validated 2026-07-24 over COM11 + TeraTerm, and in the `pc3` emulator.

- **Soft reset corrupted the engine (the hard one).** SQLite's C statics (and a
  session guard) survive Ctrl-D while the MEMSYS5 pool dies with the heap, so
  the next session allocated from memory the new Python heap owned — corruption
  surfacing as a hard lock in the first `db.close()`. Two-stage fix:
  `initialize()` re-runs a full `sqlite3_shutdown()`/configure/init cycle per
  session, and — the key discovery — the session marker is cleared in the
  module's **`__init__` hook** (runtime calls it on first import per session),
  because **root pointers are NOT auto-zeroed on soft reset**: `mp_init()`
  never memsets VM state; every owner must reset its own. A root-pointer
  "session marker" survives Ctrl-D exactly like a C static. (Also:
  `sqlite3_temp_directory` is cleared; `usqlite_files`/`usqlite_heap` roots
  reset in the same hook.)
- **GC-unsafe object lifecycle.** `connection.close()` called `m_free()` on
  cursor objects Python could still reference (use-after-free);
  `cursor.close()` left the cursor registered (freed later while referenced);
  connection/cursor were allocated without finalisers so their `__del__` was
  dead code (dropped connections leaked their pool memory for the session);
  `executemany`'s error path leaked the message in the pool (raise before
  free). Now: close only finalizes statements (the GC owns the objects),
  deregistration is a non-raising swap-remove, both types use
  `mp_obj_malloc_with_finaliser`, `sqlite3_close_v2`, and the message is
  copied+freed before raising. The soft-reset sweep now fully closes an
  abandoned connection (a `gc_is_locked()` guard stops the journal-delete from
  allocating during the sweep, which had been aborting the close midway).
- **API correctness.** 64-bit INTEGER both ways (`sqlite3_bind_int64` /
  `column_int64` — was 32-bit: binds >2³¹ raised OverflowError, reads silently
  wrapped — fatal for ms timestamps); `.description` hard-faulted on computed
  columns (`strlen(NULL)` decltype) and was empty before the first fetch
  (`data_count` → `column_count`); `connect("/name.db")` could truncate a
  root-level database when the cwd was elsewhere (existence probe now a single
  `os.stat`, which is also what makes per-transaction journal probes cheap).
- **No exception may cross SQLite's C frames.** The VFS called `io.open` /
  `os.remove` / `ilistdir`, which raise (missing dir, full flash, SD pulled —
  §23 makes that a supported action) and longjmp'd through the pager. All VFS
  entry points that touch Python are nlr-guarded and return SQLite error codes
  (`connect` to a bad path now raises `usqlite_Error`, engine stays healthy);
  a raising trace callback is swallowed like CPython's.
- **Power-fail recovery — previously disabled.** The VFS answered "no" to every
  xAccess existence probe, so a hot journal after power loss was never seen and
  a torn database was served as valid. Enabled as a set (each is required):
  honest `xAccess` (via the stat probe); zero-filled short reads (VFS
  contract, recovery reads into lost tails); **real truncate-to-zero**
  (`SQLITE_DEFAULT_LOCKING_MODE=1` finalizes the journal on *every commit* by
  truncation — honest detection with the old no-op truncate would have
  replayed stale journals **over committed data**; implemented by reopening
  `"w+b"`, since MicroPython streams have no truncate); `SQLITE_OMIT_WAL`
  (journal_mode=WAL would have called a NULL xShm method; −17 KB flash).
  `xRandomness` now really fills its buffer (xorshift over µs ticks).
  Emulator: snapshot "power cuts" mid-transaction/post-commit/garbage-journal
  all recover (`integrity_check` ok). Hardware: 3 physical power pulls during
  committed-batch writes — clean rollback each time, ledger invariant held.
  *Caveat:* this protects the database; FAT metadata itself is not
  power-fail-atomic (a cut mid-FAT-update can still damage the filesystem).
- **Tests.** Emulator: `lib/usqlite/tests/` (5 suites; run `pc3` unix build
  with `-X heapsize=8m` — must match the board's heap). On-device:
  `tests/sqltest_a..e.py` — staged (soft reset between a/b, physical power
  pulls between d/e); see tests/README.
- **Tooling lessons** (details in "Driving the board over serial"): mpremote's
  raw-paste locked the board via the CH340 console (never tested with UART
  console; use the pyserial helper); the CH340 host-side RX can die while the
  board still receives (TeraTerm stays fine) — prove the link duplex before
  sending control chars, and fall back to TX-only `autosave()` uploads with
  the user running tests via TeraTerm.

### 68. USB mouse — report-protocol switch + general HID report decoder

A 12-bit-packed mouse (VID:PID 15d9:0a4c) moved erratically; diagnosed on
hardware with temporary descriptor/raw-report dumps.

- **Root cause: protocol, not parsing.** TinyUSB's host stack puts
  boot-capable mice into **boot protocol** during enumeration (3-byte 8-bit
  reports) while our decoder used the report-protocol layout parsed from the
  descriptor. MMBasic works because its mount callback issues
  `tuh_hid_set_protocol(REPORT)` (USBKeyboard.c mouse path) — now done
  identically in `usb_mouse_mount()`. That call is the *only* mount-time
  control transfer permitted; nothing else may be added to enumeration
  (report requests during enumeration crash the tinyusb stack — see §30's
  EP0-wedge history).
- **General decoder.** The MMBasic-derived three fixed layouts (8/12/16-bit
  X/Y in one byte order) replaced by a one-pass descriptor parse capturing
  per-field bit offset / width / signedness / owning report ID (buttons, X,
  Y, wheel, AC pan), with reports decoded by bit extraction (same scheme as
  the touch parser). Fixes found in review: combo interfaces' other report
  IDs (consumer keys) were decoded as cursor movement (only the *last*
  Report ID in the descriptor was recorded, and the ID byte was skipped
  unchecked); usage pages were ignored (any 0x30/0x31 counted as X/Y);
  HID item size code 3 means 4 data bytes; no report-length validation.
  Signedness now comes from Logical Minimum. Falls back to the fixed boot
  layout when the device refuses SET_PROTOCOL or the descriptor has no X/Y.

**v0.12 version bump**: `PICO_COMPUTER_3_VERSION` "0.11" → "0.12" — the
usqlite-hardening + USB-mouse-fix firmware.

---

### 69. Two boards, one firmware — runtime board identification + the Pico Computer 2

The **Pico Computer 2** runs the same image as the Pico Computer 3. It differs
in three ways, all settled at runtime:

| | Pico Computer 3 | Pico Computer 2 |
|---|---|---|
| DS3231 RTC | fitted, **32 kHz output wired to GP27** | fitted, 32 kHz output **not connected** |
| CYW43 radio (Wi-Fi/BT) | fitted | **not fitted** — GP23/24 free (GP25 = LED, GP29 = SD CS) |
| LED | CYW43 GPIO0 (`Pin("LED")`) | **GP25** |
| SD card | CS 33, SCK 30, MOSI 31, MISO 28 — hardware SPI1 | **CS 29, SCK 30, MOSI 31, MISO 32 — bit-banged** |

The RTC itself works the same on both — `settime`/`gettime`/`synctime` and the
boot clock sync are unchanged. Only the 32 kHz *signal* distinguishes them.

**The probe.** The 32 kHz clock on GP27 is the signature; the Pico Computer 2
does not route it to a pin. `ports/rp2/board_detect.c` is a direct port of MMBasic's
`TestPicoComputer3()` (`PicoMite.c`), per [[replicate-mmbasic-exactly]]: GP27 as
input with a **pull-up** (the DS3231 pin is open drain, and the pull-up parks a
floating pin at a known level), then four edge waits (high→low→high→low) inside a
**200 µs** window. Two full cycles of 32768 Hz take 61 µs, plus up to 15 µs
waiting for the first edge, so 200 µs is a comfortable margin; with no signal the
whole probe is a 200 µs pause and the pull-up is released again.

It runs from `MICROPY_BOARD_STARTUP()` — the earliest hook in `main()`, straight
after `set_sys_clock_khz()` and before PSRAM, the GC heap, the UART console, the
CYW43 and every driver that claims a pin. **Once**, outside the soft-reset loop:
hardware cannot change under us, and the result is a plain BSS variable, so it
survives soft reset.

**No clock ⇒ Pico Computer 2.** That is the only other board in the family, so
"not a 3" identifies it; a third board would need a positive signature of its
own. The corollary is that a Pico Computer 3 whose DS3231 has **stopped, or had
its EN32kHz bit cleared, looks like a Pico Computer 2** —
`board.override(board.PICO_COMPUTER_3)` is the escape hatch
(MMBasic has the same idea in its saved `platform` option). It only affects
hardware brought up after the call, i.e. the SD card.

**API.** C: `board_detect_id()`, `board_is_pico_computer_3()`,
`board_is_pico_computer_2()`, `board_has_cyw43()`, `board_led_pin()`
(`board_detect.h`). Python: the `board` module — `id()`, `name()`, `has_wifi()`,
`led_pin()`, `override()`, and the `PICO_COMPUTER_3` / `PICO_COMPUTER_2` /
`UNKNOWN` constants. The ID values are part of that API, so **only append**.

**SD card: two pin sets, one of them bit-banged.** `machine_sdcard.c` gained a
`sd_bus_t` (pins + SPI id) resolved in `hw_init()` from the board identity; a
**negative SPI id selects the bit-banged transport**. The Pico Computer 2 needs
it because its MISO (GP32) is a **SPI0** pin while its SCK/MOSI (GP30/31) are
**SPI1** — no hardware instance covers the set. The bit-bang routines are ported
from MMBasic's `BitBangSendSPI` / `BitBangReadSPI` / `BitBangSwapSPI`
(`misc/SDCard.c`): mode 0, MSB first, with MMBasic's three timings — 20 µs per
half-bit while identifying the card, then NOP padding, one NOP at or below
200 MHz (MMBasic's `slow_clock`) and three above it, which is what keeps the
clock inside the card's limits at 252/378 MHz. `clk_sys` is read once per call,
not per bit, because `screen(mode, clock)` can change it between transfers. One
deliberate deviation: the read path holds **MOSI high** for the whole transfer so
the card sees 0xFF on DI, matching what the hardware-SPI path shifts out (MMBasic
leaves MOSI wherever the last write left it). Everything above the transport —
the SD command set, hot-swap `check()`/`reinit()`, `pcsd.py` — is unchanged and
board-agnostic.

The pin sets are declared by the board as `MICROPY_HW_SD_*` and
`MICROPY_HW_SD_ALT_*` with a `MICROPY_HW_SD_USE_ALT()` selector, so the driver
itself holds no board knowledge. Pin **reservation** follows the same route:
`MICROPY_HW_PIN_RESERVED` now calls `machine_sdcard_pin_reserved()` instead of
listing 28/30/31/33, because the two boards reserve different pins.

**CYW43 absent is a hazard, not just a missing feature.** On a Pico Computer 2
the radio's pins are in use — **GP25 is the LED and GP29 the SD chip select** —
so bringing the (absent) chip up would take over a live chip select mid-transfer
and corrupt the card. A new board-agnostic hook, `MICROPY_HW_CYW43_PRESENT()`
(default `(1)` in `mpconfigport.h`, overridden to `board_has_cyw43()` by this
board), gates every path that touches the chip:

- `main.c` — the whole start-up block (`cyw43_init` already drives WL_REG_ON).
- `extmod/network_cyw43.c` — `network.WLAN(…)` raises `OSError: no WLAN hardware`.
- `mpbtstackport.c` — `mp_bluetooth_btstack_port_init()` raises before the
  transport is opened (it is the first thing `mp_bluetooth_init()` calls).
- `machine_pin_cyw43.c` — `Pin("LED")`/`Pin("WL_GPIO*")` set/get raise.
- `mphalport.c` — `mp_hal_is_pin_reserved()` reserves nothing when there is no
  radio, freeing WL_HOST_WAKE.

Left alone deliberately: `cyw43_ensure_up()` does fail gracefully (10 tries on
the SPI test register, ~10 ms) — but only *after* `cyw43_spi_init()` has already
claimed the pins, which is exactly what must not happen here.

**The banner names the machine it is on.** `MICROPY_HW_BOARD_NAME` is compiled
in, so the REPL banner and `os.uname().machine` said "PICO COMPUTER 3" on both
boards. Both are now resolved at run time from `board_machine_name()`, via two
board-agnostic hooks that default to today's behaviour:

- `MICROPY_BANNER_MACHINE_STR` (new, `py/mpconfig.h`, defaults to
  `MICROPY_BANNER_MACHINE`) — `pyexec.c` prints the separator and the machine
  name as two calls instead of one concatenated literal, so a board may supply
  an expression. `MICROPY_BANNER_MACHINE` itself must stay a literal:
  `sys.implementation._machine` is a `static const` str object built from it.
- `MICROPY_PY_OS_UNAME_MACHINE_DYNAMIC` + `mp_os_uname_machine()`
  (`extmod/modos.c`) — an exact mirror of the existing
  `MICROPY_PY_OS_UNAME_RELEASE_DYNAMIC` idiom: the str object drops `const` and
  `os.uname()` repoints it on each call.

The board defines `MICROPY_HW_BOARD_NAME_ALT` ("PICO COMPUTER 2 v0.14") beside
the compiled-in name and points both hooks at `board_machine_name()`, which
returns one of two string literals — so the pointer `os.uname()` keeps is valid
for the life of the program. **`sys.implementation._machine` still names the 3
on both boards** (it is a compile-time constant object with no init hook);
`board.name()` and `os.uname().machine` are the honest answers.

**Python side.** `_boot_board.py` exposes the `board` module and binds
`LED` to `machine.Pin(25, OUT)` **only when the LED is a real GPIO** — building
`Pin("LED")` on a Pico Computer 3 would power the radio up on every boot, so
there it stays a manual `Pin("LED", Pin.OUT)`. `pcnet.wifi()` reports "No Wi-Fi
hardware on this board" and `pcnet.boot_sync()` returns silently.

**Board scope kept clean** (§27): `board_detect.c` is gated on
`MICROPY_HW_BOARD_DETECT` (set in both `mpconfigboard.cmake` and
`mpconfigboard.h`); the four shared-file edits are all board-agnostic hooks with
a default that preserves today's behaviour for every other rp2 board. GP27 is
deliberately **not** reserved — on a Pico Computer 2 it is an ordinary GPIO.
(Verified with `-fsyntax-only` against the configured `RPI_PICO` and
`RPI_PICO2_W` build flags as well as this board's.)

**RTC alarm line.** `ds3231.alarm_pin()` hands out GP32, which is the SD card's
MISO on a Pico Computer 2 — it now raises `OSError: no RTC INT line on PICO
COMPUTER 2` instead of colliding (the pin reservation would have stopped it
anyway, but with a confusing message). The rest of `ds3231.py` is board-agnostic.

**Known wrinkle.** `MICROPY_HW_SPI1_*` still maps `machine.SPI(1)` onto
GP30/31/28. On a Pico Computer 3 that is deliberate — it *is* the SD bus. On a
Pico Computer 2 the SD card bit-bangs GP30/31, so claiming `machine.SPI(1)` there
would switch those pads to SPI function underneath it. The pins are reserved
against `machine.Pin()`, but `machine.SPI(1)` is not routed through that check.

---

### 70. `fm` — a program launched from the file manager could not be Ctrl-C'd

Reported from a v0.14 board: run a program from `fm()` and **Ctrl-C does
nothing**; run the same file with `run()` from the REPL and it stops normally.
Stranger still, **editing the file first made Ctrl-C work** — the clue that
solved it.

`fm` reads Ctrl-C as a *key* (`_key()` returns `"QUIT"`), so `fm()` sets
`micropython.kbd_intr(-1)` for its whole session and only restores `3` when you
quit. `_FM.run()` then called `pcshell.run()` **inside** that window, so the
program inherited a console where Ctrl-C is an ordinary byte and no
`KeyboardInterrupt` is ever raised. Editing first "fixed" it because pye's
`IO_DEVICE.deinit_tty()` ends with `kbd_intr(3)` — leaving the interrupt char
*on* when control returned to fm. The same accident applied after a view
(`pcshell.cat`'s pager ends in `_getkey`, which also restores `3`), and it cut
both ways: after an edit or view, Ctrl-C in fm's own panels raised
`KeyboardInterrupt` and dumped the user out of the file manager with a
traceback.

The invariant is now explicit, in one helper (`pcfm._kbd_intr`) with the rule in
its docstring: **fm owns `kbd_intr(-1)` while its panels are up; everything it
hands the console to restores `3`, so fm re-asserts its own mode on the way
back.** `run()` deliberately turns the normal Ctrl-C back **on** for the
duration of the program (it must, or the program cannot be stopped), catches the
resulting `KeyboardInterrupt`, prints `stopped`, and returns to the panels
rather than unwinding out of `fm()`. `edit()` and `view()` re-assert `-1` in a
`finally`.

Verified on the emulator by stubbing `micropython.kbd_intr` and `pcshell` and
tracing the call sequence: `fm()` → `-1`; run → `3`, program, Ctrl-C caught,
`-1`; edit/view → shim sets `3`, fm restores `-1`.

---

### 71. `fm` — file sizes shown as 0

Reported alongside §70: every file listed as 0 bytes, though all of them open
fine. Not the reporter's mistake — `_Panel.load()` took the size from
`os.ilistdir()`'s **optional** 4th element and fell back to `0` when it was
absent:

```python
size = e[3] if len(e) > 3 else 0
```

That element is not part of the contract. `vfs_fat.c` and the littlefs bindings
build 4-tuples including `fsize`, but **`vfs_posix.c` builds 3-tuples** (no
size), as does the synthesised root listing of mount points in `vfs.c`. So on
the emulator, whose `/sd` is a host directory, *every* file read as 0 bytes —
reproduced exactly.

`load()` now falls back to `os.stat(...)[6]` for any file whose listing carries
no size. Only sizeless files are stat'ed, so a FAT or littlefs listing costs
nothing extra, and a genuinely empty file still shows 0.

Verified on the emulator: `ilistdir` gives `('bubble.py', 32768, 5070)` (3-tuple,
no size) and the panel now shows `1K`. Note that a controlled test against a
**real FAT filesystem** (`VfsFat` over a RAM block device) does return the size —
`('bubble.py', 32768, 0, 1234)` — so if a board's SD listing is also showing
zeros, the stat() fallback fixes the display but there is a second cause worth
chasing in the FatFS layer.

---

### 72. Num Lock is a property of the keyboard — remembered per VID:PID

A Raspberry Pi keyboard on the PC3 typed digits for `7890/uiop/jkl;/m`. That is
the keyboard's own firmware overlaying an embedded numeric keypad onto the letter
keys, and the only thing that triggers it is the **Num Lock bit in the LED output
report we send** — `kbd_set_leds()` at the slot's first poll, seeded from
`kbd_num`, which defaulted to `true` (MMBasic's `Option.numlock`). Our decoder
never sees a letter: the keyboard sends keypad usages (`u` arrives as 0x5C).

**Can it be detected?** No, and the descriptors say so directly. Dumping
`desc_report` at mount (`PC3_KBD_DESC_DUMP`, still in mp_usbh.c, off) for two
keyboards:

| | Raspberry Pi (no keypad) | Lenovo full-size |
| --- | --- | --- |
| VID:PID | `04d9:0006` (Holtek) | `04b3:3025` (IBM) |
| descriptor | 65 bytes | 65 bytes, **byte-identical** |

Both declare `19 00 2a ff 00 … 81 00` — Usage Minimum 0, **Usage Maximum 0x00FF**,
`Input (Data, Array)`. A HID *Array* item declares the range of values an element
may carry, not which keys exist, and the compact keyboard already claims the
entire usage page, so no full-size keyboard can declare a wider one. Both also
declare a Num Lock LED (`05 08 19 01 29 03`) despite one having no keypad. The
other candidates fail too: `bCountryCode` encodes layout country, and Physical
Descriptors (the one HID feature that would answer this) are essentially never
implemented. A *Variable*/bitmap key report could in principle omit absent keys,
but only NKRO keyboards use one, on their report-protocol interface — and we stay
in boot protocol deliberately (§25: a `set_protocol` from the mount callback
wedges EP0).

A VID:PID quirk table was the obvious fallback and is a trap here: `04d9` is
Holtek, a generic keyboard-controller vendor shared across unrelated OEM designs,
so quirking `04d9:0006` would break somebody's full-size Holtek keyboard.

**So it is remembered, not detected — and the user's own Num Lock press is the
authority.** No guessing, and both keyboards are right at the same time.

**The one thing that IS readable: the LED block.** Peter's follow-up — a keyboard
with no Num Lock *light* almost certainly has no numeric keypad. That is the
**converse** of what the dumps disproved, and only the converse holds: a declared
Num Lock LED means nothing (the Pi keyboard declares one), but an absent one is
good evidence. It is also plausible *here* specifically, because both dumped
keyboards declare `19 01 29 03` — exactly the three lights they have — rather than
the HID spec's boilerplate `29 05`. These vendors trim the LED block to the
hardware, so unlike the key array it carries information.

`kbd_has_numlock_led()` walks the descriptor's short items tracking the usage
page and pending local usages, and asks at each **Output** main item whether
LED-page usage 0x01 is among them (handling `Usage`, `Usage Minimum/Maximum`
ranges, 4-byte usages carrying their own page, and long items). It supplies the
**default only** — a saved preference still wins, and one Num Lock press corrects
and saves it either way, which is what makes guessing safe at all. A keyboard
with no Num Lock LED also prints `no Num Lock LED declared -- assuming no numeric
keypad` at mount.

It lives in **kbd_decode.c**, not mp_usbh.c: it is pure descriptor arithmetic with
no platform in it, which is exactly what that file is for, and putting it there
means the Fuzix kernel gets the same one copy with `kbdsync.sh` guarding it
rather than a second implementation to keep in step.

Host-tested against the extracted function (`sed` pulls it straight out of
kbd_decode.c so the test can't drift, and covers both trees at once), under
ASan/UBSan: the real descriptor → true;
the same with `19 02` → false; individual usages with and without Num Lock;
the spec's five-LED block; Num Lock usages consumed by an **Input** item → false
(the local set must be cleared by the main item); a 4-byte usage; a long item;
NULL/zero length → MMBasic's default; and every truncated prefix plus garbage
terminates without reading past the end.

- `kbd_set_numlock(int)` added to the shared decoder (`kbd_decode.{c,h}`, vendored
  byte-identical into the Fuzix kernel — `kbdsync.sh` re-run, all three ok). It
  only sets the state; the caller pushes the LEDs at its own safe moment. No new
  backend seam was needed: `kbd_backend_set_leds` is *already* called on every
  lock keypress, so mp_usbh.c detects a change in the num bit there.
- `hid_slot_t` carries `vid`/`pid` (hoisted the `tuh_vid_pid_get` out of the
  protocol-NONE branch — it's cached by TinyUSB, no bus traffic, so it is safe in
  the mount callback per §25).
- `kbd_numlock_pref[8]` in mp_usbh.c: a static table, looked up at mount **before**
  the LED bitmap is seeded, so a compact keyboard's very first LED report already
  says "off" and the overlay never comes on. Static and allocation-free because
  the mount callback must not allocate or wait — which also rules out asking
  Python for the answer there. `kbd_numlock_for(vid, pid, dflt)` falls back to the
  LED-block guess when the keyboard is unknown.
- `keyboard.kbd_desc()` returns the mounted keyboard's report descriptor (kept in
  a 256-byte static, copied at mount) and `keyboard.numlock_led()` says what the
  default was derived from — so a new keyboard can be diagnosed at the REPL
  instead of by rebuilding with `PC3_KBD_DESC_DUMP`.
- On a Num Lock press mp_usbh.c updates the table and schedules
  `keyboard.on_numlock`, called as `cb((vid, pid, on))`; `pcconfig._numlock_saver`
  writes it to `/settings.json` under `"numlock": {"04d9:0006": false, …}`.
  `_boot_board` calls `pcconfig.apply_numlock()`, which pushes every saved entry
  down through `keyboard.numlock_pref(vid, pid, on)` and arms the hook.
- `numlock()` / `numlock(False)` is the explicit control (injected into the REPL,
  and `keyboard.numlock()` underneath); `keyboard.kbd_id()` gives the mounted
  keyboard as `(vid, pid)`. Default for an unknown keyboard stays **on**, so
  nothing changes for a full-size keyboard.
- The emulator builds `usb_keyboard.c` but not `mp_usbh.c`, so `kbd_sdl.c` gained
  the four accessors. Only the decoder's num-lock state is real there (enough for
  the keypad remap); `kbd_id()` returns 0 → `None`, which tells pcconfig there is
  no keyboard to save a setting against.

**Ported to the Fuzix kernel** the same day (FUZIX `PC3-DEVNOTES.md`): same two
rules, `kbd_has_numlock_led` shared through kbd_decode.c, `kbd_numlock_pref[4]` in
usbkbd.c, and `PICOIOC_NUMLOCK` 0x0037 behind `picoctl numlock [on|off
[vvvv:pppp]]`. The one thing that does not port is persistence — a kernel does not
write files, so the table is per-session and `/etc/rc` is what makes a setting
permanent.

Verified on the emulator: `numlock()` defaults True, set/get round-trips,
`keydown(8)` tracks it, a bad VID raises, and the settings round-trip is
`{"numlock": {"04d9:0006": false, "04b3:3025": true}}` on disk and back through
`apply_numlock()`. **Board verification outstanding** — the C table lookup at
mount only runs on real hardware: plug in the Pi keyboard, press Num Lock once,
reboot, and the letters must still be letters; then plug in the Lenovo and its
keypad must still type digits.

---

## Files touched

| File | Purpose |
| --- | --- |
| `ports/rp2/main.c` | board-overridable startup clock (`MICROPY_HW_CLK_SYS_KHZ`); safe flash-timing ordering |
| `ports/rp2/hdmi.c` | **new** HSTX DVI driver + `hdmi` module: dual-mode scanout, `init/deinit/fb/fill/scroll/putc/text/…` (§28 adds scaled 8×12 `hdmi.text`; §32 adds the layer/off-screen targets `layer/create/write/copy/close` + core1 layer merge; §33 adds `hdmi.blit` with skip-colour; §34 adds `hdmi.vsync`/`transparent` + `(buffer,w,h)` blit surfaces; §37 makes `hdmi.scroll` a directional pixel band; §40 adds `hdmi.flood` scanline fill) |
| `ports/rp2/xmodem.c` | **new** `xmodem` module: XMODEM send/recv over the console UART, faithful port of MMBasic `misc/XModem.c` + trailing-pad trim on receive (§28) |
| `ports/rp2/console_font.h` | **new** vendored MMBasic 8×12 `font1` (console font) |
| `ports/rp2/mp_usbh.c` | **new** USB host glue: `tuh_init`/task, MMBasic 4-slot HID table + request-based polling (`hid_poll`/`report_timer`), keyboard→`stdin_ringbuf`, touch→`usb_touch.c`; USB-event sound callback; reentrancy guard; `KeyDown[]` held-key state + `kbd_map_code` (§30); num-lock keypad remap |
| `ports/rp2/usb_keyboard.c` + `keyboard_maps.h` | **new** `keyboard` module (`keymap()`, `keydown()`, `on_key()`, `on_usb_event()`) + vendored MMBasic layouts; rooted USB-event/key callbacks; key-code constants (§30) |
| `ports/rp2/usb_touch.c` + `usb_touch.h` | **new** USB multi-touch: HID descriptor parser + report decode/reassembly + digitizer-init handshake + gesture machine (vendored MMBasic) |
| `ports/rp2/usb_touch_mod.c` | **new** `touch` module (`touch()` query: X/Y, contacts, swipes, tap/hold, pinch/rotate) |
| `ports/rp2/usb_mouse.c` + `usb_mouse.h` | **new** USB mouse: general HID descriptor parse (per-field bit offset/width/sign/report ID) + bit-extraction report decode + report-protocol switch at mount, boot-layout fallback (§68); cursor accumulation/buttons/wheel/double-click (vendored MMBasic) |
| `ports/rp2/usb_mouse_mod.c` | **new** `mouse` module (`mouse()` query: X/Y/L/R/M/W/B/D/T; `mouse_speed()`) |
| `shared/tinyusb/tusb_config.h` | `#if MICROPY_HW_USB_HOST` block (host mode, hub, enum buf 1024, HID) |
| `ports/rp2/uart.c` | translate serial-terminal Del (`0x7f`) → `\x1b[3~` under `MICROPY_HW_UART_REPL_DEL_FORWARD`; `mp_uart_repl_mute` output-mute flag (§35) |
| `ports/rp2/sercon.c` | **new** `_sercon` module: serial-console output mute for `console()` routing (§35) |
| `ports/rp2/audio.c` | **new** `audio` module: `scale()` volume + `{wav,mp3,flac}_{open,read,close}` via dr_* + GC/rooted allocator; `usb_sound()` decodes the plug-in/unplug WAVs; tone generator + 4-voice synth + MOD player backends (§31) |
| `ports/rp2/hxcmod.c`/`.h` | **new** vendored HxCMOD tracker player (MMBasic's copy, incl. its sound-effect extension for `mod_sample`) (§31) |
| `ports/rp2/sound_tables.h` | **new** vendored MMBasic SineTable/triangletable (4096) + mapping[101] volume table (§31) |
| `ports/rp2/usb_connect_sound.h` / `usb_remove_sound.h` | **new** vendored MMBasic 8-bit 8 kHz USB plug-in / unplug WAVs |
| `ports/rp2/dr_wav.c` / `dr_mp3.c` / `dr_flac.c` (+ `.h`) | **new** decoder impl TUs; dr_wav from MMBasic, dr_mp3/dr_flac stock from dr_libs |
| `ports/rp2/rp2_psram.c` | (pending) add `psram_set_timing_for_freq` for the live clk_sys switch |
| `ports/rp2/main.c` (3) | cyw43 gSPI PIO divider from clk_sys before `cyw43_init` (needs dynamic divider) |
| `boards/PICO_COMPUTER_3/pcaudio.py` | **new** background WAV/MP3/FLAC player (`play`/`stop`/`volume`/`beep`); `system_sound()` for USB plug-in/unplug |
| `boards/PICO_COMPUTER_3/ds3231.py` | **new** DS3231 RTC (`settime`/`gettime`/`synctime`), boot-synced |
| `ports/rp2/jpeg.c` + `picojpeg.c`/`.h` | **new** `jpeg` module (`render`) + vendored picojpeg (JPEG decode + binning downscale) |
| `ports/rp2/bmp.c` | **new** `bmp` module: `save` (24-bit BMP of the framebuffer) + `load` (framebuffer blit callback) |
| `ports/rp2/bmp_decoder.c` | **new** vendored MMBasic BmpDecoder engine (all BMP variants), adapted to mp_stream + PSRAM |
| `ports/rp2/png.c` + `upng.c`/`.h` | **new** `png` module (`render`) + vendored upng (PNG decode, RGBA alpha) |
| `ports/rp2/dither.c`/`.h` | **new** RGB121/RGB332 error-diffusion (Floyd-Steinberg / Atkinson), ported from MMBasic `FileIO.c`; used by `jpeg`/`bmp` load in RGB1024/RGB640 (§29) |
| `ports/rp2/hdmi.c` (image) | `hdmi.framebuffer()`/`width()`/`height()`/`bpp()` expose the framebuffer to the decoders (+ `hdmi_nearest_index`/`hdmi_index_rgb888` for RGB121 4bpp packing, §29); `hdmi_get_width/height()` C accessors for touch scaling |
| `boards/PICO_COMPUTER_3/pcimage.py` | **new** `draw_jpg`/`draw_bmp`/`draw_png`/`save_image` wrappers (injected into the REPL); `load_image()` + `Image` (in-memory blit surface / sprite sheets, §41) |
| `boards/PICO_COMPUTER_3/pcconfig.py` | **new** persistent settings in `/settings.json`; `keymap()`/`screen()` apply + persist |
| `boards/PICO_COMPUTER_3/pcgfx.py` | **new** `Display` (framebuf subclass, RGB888→format `colour()`) + MMBasic palette; extra primitives `line(w=)`/`rbox`/`arc`/`bezier`/`flood` (§40) |
| `boards/PICO_COMPUTER_3/pcconsole.py` | **new** on-screen console (`io.IOBase`/dupterm, ANSI, blink cursor, terminal-sync) |
| `ports/rp2/mpconfigport.h` | `#ifndef`-guard float impl + MCU name; default `MICROPY_PY_MACHINE_SDCARD 0` (flash FS size + feature enables moved to board scope — §27) |
| `ports/rp2/help.c` | overridable `MICROPY_HW_HELP_PIN_TEXT` (generic `0-29` default; PC3 sets `0-47`) |
| `ports/rp2/modules/_boot.py` | generic `import _boot_board` hook only — all PC3 boot logic moved to the board's frozen `_boot_board.py` (§27) |
| `boards/PICO_COMPUTER_3/_boot_board.py` | **new** frozen board boot hook: REPL/shell/graphics injection, `pcsd.start()`, RTC sync, `hdmi.init` + `console()` |
| `ports/rp2/machine_pin.c` | enforce pin reservation in the `Pin` constructor |
| `ports/rp2/board_detect.c`/`.h` | **new** runtime board identification: GP27 32 kHz probe (MMBasic `TestPicoComputer3`) + the `board` module (`id`/`name`/`has_wifi`/`led_pin`/`override`) (§69) |
| `ports/rp2/machine_sdcard.c` | **new** native `machine.SDCard` block device; `check()`/`reinit()` + activity-deferred liveness probe for hot-swap; two runtime pin sets + bit-banged transport for the Pico Computer 2 (MMBasic `BitBang*SPI`) (§69) |
| `ports/rp2/mpconfigport.h` (2) | `MICROPY_HW_CYW43_PRESENT()` hook, default `(1)` (§69) |
| `ports/rp2/mphalport.c` | `mp_hal_is_pin_reserved()` reserves nothing when no radio is fitted (§69) |
| `ports/rp2/machine_pin_cyw43.c` | WL_GPIO set/get raise when no radio is fitted (§69) |
| `ports/rp2/mpbtstackport.c` | `port_init()` raises when no radio is fitted (§69) |
| `extmod/network_cyw43.c` | `network.WLAN(…)` raises when no radio is fitted (§69) |
| `py/mpconfig.h` (2) | `MICROPY_BANNER_MACHINE_STR` hook, defaults to `MICROPY_BANNER_MACHINE` (§69) |
| `shared/runtime/pyexec.c` (2) | banner prints separator + machine name separately so the latter may be a runtime expression (§69) |
| `extmod/modos.c` | `MICROPY_PY_OS_UNAME_MACHINE_DYNAMIC` / `mp_os_uname_machine()`, mirroring the release-dynamic idiom (§69) |
| `boards/PICO_COMPUTER_3/pcsd.py` | **new** `/sd` mount + hot-swap removal/insertion poll (soft Timer; replicates MMBasic `CheckSDCard`) |
| `ports/rp2/modmachine.c` | register `machine.SDCard` |
| `ports/rp2/CMakeLists.txt` | board-specific sources gated on `MICROPY_HW_ENABLE_HDMI` / `MICROPY_PY_MACHINE_SDCARD` / `MICROPY_HW_USB_HOST` / `MICROPY_HW_BOARD_DETECT` (§69); link `tinyusb_host` vs `_device` (§27) |
| `ports/rp2/main.c` (2) | `mp_usbh_init()` at startup when `MICROPY_HW_USB_HOST` |
| `py/mpconfig.h` | `MICROPY_BANNER_MACHINE_SEP` default (`"; "`) |
| `shared/runtime/pyexec.c` | banner uses `MICROPY_BANNER_MACHINE_SEP` |
| `boards/PICO_COMPUTER_3/mpconfigboard.h` | double floats, UART console, USB off, threads off, SD + HDMI pins, reserved pins, 252 MHz clock + flash cap, MCU name + banner; `PICO_COMPUTER_3_VERSION` folded into board name (shows in banner + `os.uname().machine`) |
| `boards/PICO_COMPUTER_3/USER_MANUAL.md` | **new** end-user manual (pins, all commands/modules, standard-module list, MicroPython doc reference) — ships with the release |
| `boards/PICO_COMPUTER_3/mpconfigboard.cmake` | route pico-sdk default UART to UART1/GP8/GP9; `CYW43_PIO_CLOCK_DIV_DYNAMIC=1`; 12 MB flash FS; ulab **and usqlite** via `USER_C_MODULES` (§65); feature-gate vars `MICROPY_HW_ENABLE_HDMI`/`MICROPY_PY_MACHINE_SDCARD`/`MICROPY_HW_USB_HOST` (§27) |
| `lib/usqlite` | **new** git submodule → `UKTailwind/usqlite` `d50441d` (fork of spatialdude v0.1.8 + 1.29 fix) — SQLite 3.47 C user-module; `import usqlite` (§65); MEMSYS5 pool + sort-spill (§65 follow-ups); full hardening: soft-reset session cycle, GC-safe lifecycle, 64-bit ints, exception-safe VFS, power-fail recovery + `tests/` (§67) |
| `.gitmodules` | register `lib/usqlite` submodule (url = UKTailwind/usqlite, branch pc3-micropython-1.29) |
| `ports/rp2/memmap_rp2350/section_extra_post_platform_end.incl` | SRAM GC-heap `ASSERT` floor 64 KB → 56 KB (PSRAM is the real heap) (§65) |
| `boards/PICO_COMPUTER_3/manifest.py` | drop pure-Python `sdcard`; freeze `_boot_board`/`pcshell`/`pye`/`pcgfx`/`pcconsole`/`pcaudio`/`ds3231`/`pcsd`; `require` bundle-networking + `umqtt.simple`/`umqtt.robust` + `aioble` |
| `boards/PICO_COMPUTER_3/pcsprite.py` | **new** sprite engine: MMBasic collision/layer/scroll semantics on a dirty-rect / overlay-layer compositor (§34) |
| `boards/PICO_COMPUTER_3/pcnet.py` | **new** Wi-Fi + NTP time: `wifi`/`ntpsync`/`tz`/`auto`; credentials in `pcconfig` (§39) |
| `boards/PICO_COMPUTER_3/pcturtle.py` | **new** `Turtle` graphics class (MMBasic TURTLE on the pcgfx primitives) (§42) |
| `boards/PICO_COMPUTER_3/pcshell.py` | **new** shell commands (`ls`/`run`/`edit`/file ops) + `COMMANDS` |
| `boards/PICO_COMPUTER_3/pye.py` | **new** vendored pye editor (MIT, V2.79) with one local Backspace patch |
| `boards/PICO_COMPUTER_3/pcfm.py` | **new** dual-panel file manager (MMBasic FM): `fm()` browses/runs/plays/views; `_kbd_intr()` keeps the Ctrl-C mode straight across run/edit/view (§70) |

## Quick REPL smoke test (over UART, 115200 8N1)

```python
2**63                     # 64-bit+ ints
0.1 + 0.2                 # 0.30000000000000004 (double precision)
machine.freq()            # 252000000
Pin(15, Pin.OUT)          # a free pin
Pin(8, Pin.OUT)           # ValueError: Pin(8) is reserved
cd("/sd"); ls()           # SD auto-mounted; shell commands need no import
edit("/sd/hello.py")      # pye editor  (Ctrl-S save, Ctrl-Q quit)
run("/sd/hello.py")       # launch it in a fresh namespace

# HDMI: boots into 640x480 RGB332; hdmi/framebuf/Display/palette pre-injected
fb = hdmi.fb()            # pcgfx.Display at the current mode
fb.text("PICO COMPUTER 3", 8, 8, fb.colour(RED))
hdmi.deinit(); hdmi.init(hdmi.RGB565); fb = hdmi.fb()   # swap to 320x240
```

## Next

- **USB-host keyboard** — input from the board itself so it's a true standalone
  computer (no serial terminal). Uses the RP2350's USB controller freed by
  disabling USB-device. Feed keystrokes into the console's input path (dupterm
  `readinto`, or the stdin ring buffer) — the big MMBasic bring-up. Approach TBD.
- Possible follow-ups: colour SGR (`\x1b[…m`) in the console; stdio-over-VFS shims
  for MMBasic-derived C file code; airtight pin reservation via
  `machine_pin_find()`; `tree()`/`df()`; a linker-reserved SRAM framebuffer region
  so the 307 KB can be reclaimed when HDMI is unused (only if SRAM gets tight).
```
