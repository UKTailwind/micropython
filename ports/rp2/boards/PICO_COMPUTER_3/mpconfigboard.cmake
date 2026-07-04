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
)

# Let the cyw43 gSPI PIO clock divider be set at runtime (main.c scales it with
# clk_sys, algorithm from MMBasic). The SDK's fixed default (2) is tuned for
# ~125 MHz and overclocks the link at our higher CPU clocks -> "hdr mismatch"
# / ioctl timeouts.
add_compile_definitions(CYW43_PIO_CLOCK_DIV_DYNAMIC=1)
set(MICROPY_PY_MACHINE_SDCARD 1)
# USB host: link the TinyUSB host stack instead of the device stack (see CMakeLists).
set(MICROPY_HW_USB_HOST ON)
set(MICROPY_PY_LWIP ON)
set(MICROPY_PY_NETWORK_CYW43 ON)

# Bluetooth
set(MICROPY_PY_BLUETOOTH ON)
set(MICROPY_BLUETOOTH_BTSTACK ON)
set(MICROPY_PY_BLUETOOTH_CYW43 ON)

# Board specific version of the frozen manifest
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

if(NOT DEFINED MICROPY_HW_FLASH_STORAGE_BYTES)
    set(MICROPY_HW_FLASH_STORAGE_BYTES 2621440)  # PICO_FLASH_SIZE_BYTES - 1536 * 1024 = 4MB - 1.5MB
endif()
