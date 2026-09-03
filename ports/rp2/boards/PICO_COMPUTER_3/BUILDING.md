# Building the Pico Computer 3 firmware, from a clean clone

This is the MicroPython **rp2** port configured as the Pico Computer 3 (and
Pico Computer 2) — one firmware image for both boards. The same tree also
builds a desktop **emulator** (the unix `pc3` variant) and the **User
Manual** PDF. What follows takes a fresh checkout to all three.

Deep dives live elsewhere and are referenced where they matter:
`DEVELOPMENT_NOTES.md` is the design record and progress log,
`TINYUSB-0.21.md` documents the USB host stack and its build-time patches,
and `USER_MANUAL.md` is the end-user reference.

## Prerequisites

    arm-none-eabi-gcc, -binutils, -newlib     the cross toolchain
    cmake, make, python3, git                 the build
    patch                                     applies the TinyUSB host patches
    pandoc + a LaTeX engine (xelatex)         only to rebuild the manual

`patch(1)` is not optional: since the TinyUSB 0.21 move (`TINYUSB-0.21.md`,
`DEVELOPMENT_NOTES.md` §73) three TinyUSB host files are patched into the
build directory at build time. A build without `patch` on `PATH` stops at
the first patch step.

The build has been done in **WSL Ubuntu** on a Windows 11 host; nothing in
it is WSL-specific, but if you drive it that way, wrap the commands with
`wsl -d Ubuntu bash -lc "…"`.

## 1. Get the sources

Two one-time steps from the **repository root** — the submodules this board
needs (pico-sdk, TinyUSB, lwIP, cyw43, …) and the `mpy-cross` compiler that
freezes this board's `manifest.py`:

    make -C mpy-cross
    make -C ports/rp2 BOARD=PICO_COMPUTER_3 submodules

The submodule step pins **TinyUSB at the exact commit this branch requires**
(`lib/tinyusb`, the `micropython/tinyusb` fork at `0.21.0-micropython1`);
the firmware's configure step asserts `TUSB_VERSION_MINOR == 21` and stops
if it is anything else, so the version can never drift silently.

## 2. Build the firmware

    make -C ports/rp2 BOARD=PICO_COMPUTER_3

The result is **`ports/rp2/build-PICO_COMPUTER_3/firmware.uf2`**. The REPL
banner it produces names the version:

    MicroPython v1.29.0 on PICO COMPUTER 3 v0.17 with RP2350B

Incremental rebuilds just re-run `make`; the TinyUSB patches re-apply
automatically and refuse (stopping the build) if upstream ever changes
under a hunk. `make -C ports/rp2 BOARD=PICO_COMPUTER_3 clean` clears the
build directory.

## 3. Flash

Hold **BOOTSEL** at power-up so the bootrom's `RPI-RP2` drive appears, and
drop `firmware.uf2` onto it. (USB *device* mode is disabled in the app, but
the bootrom's BOOTSEL USB is independent and always works.) On the Pico
Computer 3 the USB HUB switch must be on **Prog** and the PC connected to
the **Prog** port for the drive to appear. Files and settings survive a
reflash.

## 4. Build the emulator (optional)

The desktop emulator is the unix port's `pc3` variant. The packaging script
builds it and rolls the distributable tarball (binary + launcher + the
book's example programs as an SD seed):

    bash ports/rp2/boards/PICO_COMPUTER_3/emulator/make-dist.sh

It writes `emulator/dist/pc3emu-linux-x64.tar.gz`; unpack anywhere and run
`./pc3emu-linux-x64/pc3emu`. To build just the binary without packaging:

    make -C ports/unix VARIANT=pc3 submodules
    make -C ports/unix VARIANT=pc3        # -> ports/unix/build-pc3/micropython

Run the bare binary with an 8 MB heap: `build-pc3/micropython -X heapsize=8m`.
See `emulator/README.md` and `emulator/INSTALL-WINDOWS.md` (WSL2) for use.

## 5. Build the manual (optional)

    bash ports/rp2/boards/PICO_COMPUTER_3/manual/build.sh

`USER_MANUAL.md` → `USER_MANUAL.pdf` (page numbers and an auto-generated
index; drives `xelatex` + `makeindex`). Needs `pandoc` and a TeX engine.

## 6. Cutting a release (maintainers)

A release is the four assets — `firmware.uf2`, `pc3emu-linux-x64.tar.gz`,
`USER_MANUAL.md`, `USER_MANUAL.pdf` — attached to a GitHub tag on this
branch. The version string lives in **three** places and all must agree:

- `PICO_COMPUTER_3_VERSION` in `mpconfigboard.h` (feeds both machine names;
  the banner comment beside it is cosmetic but kept in step);
- `MICROPY_BANNER_MACHINE` in `../../../unix/variants/pc3/mpconfigvariant.h`
  (the emulator banner);
- `USER_MANUAL.md` — the intro line, the PC3 and PC2 banner blocks, and the
  footer.

Then rebuild all three artefacts (§2, §4, §5), commit
`"rp2/PICO_COMPUTER_3: vN.NN (release prep)"`, push, and create the release
(the tag is `vN.NN`). `MM.VER` is compiled into the firmware, so it moves
only when the firmware is rebuilt — verify the banner on the board after
flashing rather than trusting the source. Feature documentation belongs in
the commit that adds the feature; the release-prep commit is the number
alone.
