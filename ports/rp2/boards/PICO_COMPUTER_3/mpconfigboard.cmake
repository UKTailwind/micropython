# cmake file for Raspberry Pi Pico 2 W

set(PICO_BOARD "pimoroni_pico_plus2_w_rp2350")

# To change the gpio count for QFN-80
set(PICO_NUM_GPIOS 48)

# Route the pico-sdk default UART (used by the REPL) to UART1 on GP8/GP9.
# These are -D definitions so they are seen before the pico board header, whose
# PICO_DEFAULT_UART* defaults are #ifndef-guarded, and so they also apply to
# pico-sdk source (e.g. setup_default_uart()) that never includes mpconfigboard.h.
add_compile_definitions(
    PICO_DEFAULT_UART=1
    PICO_DEFAULT_UART_TX_PIN=8
    PICO_DEFAULT_UART_RX_PIN=9
    # After every flash erase/program the pico-sdk re-enters XIP VIA BOOT2,
    # which times the flash at clk_sys/PICO_FLASH_SPI_CLKDIV. The Pimoroni
    # board header's default of 2 is only safe up to ~266 MHz -- at 315/378
    # (screen(..., 315/378)) the first post-write flash fetch runs the chip
    # at 157/189 MHz and the machine hangs before our re-timing code runs.
    # 4 keeps that window safe at every supported clock (378/4 = 94.5 MHz),
    # exactly MMBasic's setting; steady-state XIP speed is unaffected (see
    # MICROPY_HW_FLASH_MAX_FREQ in mpconfigboard.h).
    PICO_FLASH_SPI_CLKDIV=4
)

# Let the cyw43 gSPI PIO clock divider be set at runtime (main.c scales it with
# clk_sys, algorithm from MMBasic). The SDK's fixed default (2) is tuned for
# ~125 MHz and overclocks the link at our higher CPU clocks -> "hdr mismatch"
# / ioctl timeouts.
add_compile_definitions(CYW43_PIO_CLOCK_DIV_DYNAMIC=1)
set(MICROPY_PY_MACHINE_SDCARD 1)
# Build the on-board multimedia C sources (HSTX DVI, PCM5102 audio, image
# loaders); the port CMakeLists gates these files on this flag.
set(MICROPY_HW_ENABLE_HDMI 1)
# USB host: link the TinyUSB host stack instead of the device stack (see CMakeLists).
set(MICROPY_HW_USB_HOST ON)
# XMODEM file transfer over the console UART (xmodem module).
set(MICROPY_HW_ENABLE_XMODEM 1)
set(MICROPY_PY_LWIP ON)
set(MICROPY_PY_NETWORK_CYW43 ON)

# Bluetooth
set(MICROPY_PY_BLUETOOTH ON)
set(MICROPY_BLUETOOTH_BTSTACK ON)
set(MICROPY_PY_BLUETOOTH_CYW43 ON)

# Pull in ulab (numpy-like ndarray/linalg) as a user C module for this board.
# ulab's own micropython.cmake defines MODULE_ULAB_ENABLED and links itself into
# the `usermod` target, so no MICROPY_PY_ULAB switch is needed. Appended (and
# de-duplicated) so a USER_C_MODULES passed on the command line is preserved.
list(APPEND USER_C_MODULES ${MICROPY_DIR}/lib/ulab/code/micropython.cmake)
# Pull in usqlite (SQLite 3.47 amalgamation) as a user C module. Like ulab it
# ships its own micropython.cmake that self-links into `usermod` and registers
# the `usqlite` module -- no MICROPY_PY_* switch and nothing to freeze (it is
# pure C). SQLite runs in its own MEMSYS5 heap -- a dedicated block reserved on
# the first connect(), kept off the GC heap so gc.collect() can't corrupt an
# open database -- and its VFS maps onto the mounted filesystem (LittleFS / SD).
list(APPEND USER_C_MODULES ${MICROPY_DIR}/lib/usqlite/micropython.cmake)
list(REMOVE_DUPLICATES USER_C_MODULES)
# Size that dedicated SQLite heap to 4 MB (usqlite's default is a small 128 KB
# for constrained boards; this board has 8 MB of PSRAM). The page cache follows
# at half the pool.
add_compile_definitions(MEMSYS5_HEAP_SIZE=0x400000)

# Board specific version of the frozen manifest
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

# 12 MB read/write filesystem on the 16 MB flash (firmware ~2.4 MB sits below it).
# Set here as a CMake var so the linker reserves the partition (via the
# __micropy_flash_storage_bytes__ defsym) AND the same value is passed to C as a
# compile definition -- the two must agree or the filesystem overruns its region.
if(NOT DEFINED MICROPY_HW_FLASH_STORAGE_BYTES)
    set(MICROPY_HW_FLASH_STORAGE_BYTES 12582912)  # 12 * 1024 * 1024
endif()
