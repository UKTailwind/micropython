// Board and hardware specific configuration. The firmware version (shared by
// both machines in the family) is folded into the board name so it appears in
// the REPL banner and os.uname().machine.
#define PICO_COMPUTER_3_VERSION                 "0.15"
#define MICROPY_HW_BOARD_NAME                   "PICO COMPUTER 3 v" PICO_COMPUTER_3_VERSION
// The name the same image reports when it finds itself on the other board. The
// banner and os.uname().machine pick between the two at run time (see
// MICROPY_BANNER_MACHINE_STR below); sys.implementation._machine has to be a
// compile-time constant, so it always names the 3.
#define MICROPY_HW_BOARD_NAME_ALT               "PICO COMPUTER 2 v" PICO_COMPUTER_3_VERSION
#define MICROPY_HW_MCU_NAME                     "RP2350B"

// RP2350B has 48 GPIOs with ADC on GP40-47 (help() text).
#define MICROPY_HW_HELP_PIN_TEXT                "Pins are numbered 0-47, and 40-47 have ADC capabilities\n"

// Tidy REPL banner: "MicroPython v1.29.0 on PICO COMPUTER 3 v0.15 with
// RP2350B" -- drops the git hash / build date and uses " on " instead of "; ".
// MICROPY_VERSION_STRING carries whatever upstream's version defines say: it
// read "1.29.0-preview" while MICROPY_VERSION_PRERELEASE was set, and reads
// "1.29.0" now that this branch is merged up to the released tag.
#define MICROPY_BANNER_NAME_AND_VERSION         "MicroPython v" MICROPY_VERSION_STRING
#define MICROPY_BANNER_MACHINE_SEP              " on "

// Use double-precision (64-bit) floating point for Python floats.
// (64-bit+ integers are already provided by MICROPY_LONGINT_IMPL_MPZ.)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_DOUBLE)

// Console is on the hardware UART only (UART1, GP8=TX/GP9=RX; pins and
// instance selected via PICO_DEFAULT_UART* in mpconfigboard.cmake).
#define MICROPY_HW_ENABLE_UART_REPL             (1)

// Serial terminals (TeraTerm etc., with Backspace = 0x08) send a lone 0x7F for
// their Delete key; translate it to VT100 forward-delete so Delete works.
#define MICROPY_HW_UART_REPL_DEL_FORWARD        (1)

// Disable the USB device stack entirely (no USB-CDC console, no MSC, no
// machine.USBDevice). This frees the single USB controller for future
// USB-host use. Cascades to MICROPY_HW_USB_CDC/MSC/RUNTIME_DEVICE.
#define MICROPY_HW_ENABLE_USBDEV                (0)

// Disable _thread so nothing can launch core1: the HDMI HSTX scanout owns core1
// exclusively (RAM-resident ISR, SRAM framebuffer). Also removes the GC
// stop-the-other-core path. See DEVELOPMENT_NOTES.md (core1 / HDMI).
#define MICROPY_PY_THREAD                       (0)

// USB host (TinyUSB) on the freed USB controller. tuh_task() is pumped from the
// MicroPython event hook whenever the runtime waits (e.g. at the REPL).
#define MICROPY_HW_USB_HOST                     (1)
void mp_usbh_init(void);
void mp_usbh_task(void);
#define MICROPY_INTERNAL_EVENT_HOOK             mp_usbh_task()

// The event hook above only fires when the runtime WAITS (REPL, sleep, I/O). A
// tight `while True:` polling loop (e.g. reading touch()) never yields to it, so
// USB reports would stop being processed and touch/keyboard state would freeze.
// Also pump USB from the VM loop hook, which runs on branch back-edges (~every
// few bytecodes). A divisor keeps the cost negligible on compute-heavy loops
// while keeping USB input responsive. The VM checks pending exceptions right
// after this hook, so we only pump USB here (no mp_handle_pending needed).
#define MICROPY_VM_HOOK_COUNT                   (512)
#define MICROPY_VM_HOOK_INIT static uint32_t vm_hook_div = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_LOOP \
    do { \
        if (--vm_hook_div == 0) { \
            vm_hook_div = MICROPY_VM_HOOK_COUNT; \
            mp_usbh_task(); \
        } \
    } while (0);
#define MICROPY_VM_HOOK_RETURN MICROPY_VM_HOOK_LOOP

// Enable networking
#define MICROPY_PY_NETWORK                      (1)
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT     "PicoComputer3"

// CYW43 driver configuration (same as Pico2-W)
#define CYW43_USE_SPI                           (1)
#define CYW43_LWIP                              (1)
#define CYW43_GPIO                              (1)
#define CYW43_SPI_PIO                           (1)

// The radio is fitted on the Pico Computer 3 only. On a Pico Computer 2 its
// pins (GP23/24/25/29) belong to that board -- GP25 is the LED, GP29 the SD
// chip select -- so bringing the CYW43 up there would take over a live SPI
// chip select and corrupt the card. Everything that touches the chip tests
// this first: the start-up bring-up in main.c, network.WLAN(), Bluetooth, the
// WL_GPIO pins and the pin reservation. See §69 in DEVELOPMENT_NOTES.md.
#define MICROPY_HW_CYW43_PRESENT()              board_has_cyw43()

// Heartbeat LED. On the Pico Computer 3 it hangs off CYW43 GPIO0 (so it is
// reachable as Pin("LED") through the ext-pin table); the Pico Computer 2 has
// a real GPIO for it -- board.led_pin() reports which, and _boot_board.py
// exposes the right one as LED.
#define MICROPY_HW_LED_PIN                      (CYW43_GPIO0)

// Run the core at 252 MHz from startup (headroom for HDMI, which uses HSTX
// clocked from clk_sys; 378 MHz is the other planned option). DVDD is supplied
// by an external 1.3 V regulator on this board, so no internal vreg change is
// needed. Flash (QMI) and PSRAM timings track clk_sys automatically.
#define MICROPY_HW_CLK_SYS_KHZ                  (252000)

// clk_sys is coupled to the HDMI pixel clock (core1 derives clk_hstx per display
// mode), so the CPU speed must be changed together with the display mode via
// screen(mode, clock). Lock the raw machine.freq() setter to prevent a bare CPU
// clock change from desyncing the display; machine.freq() (getter) still works.
#define MICROPY_HW_MACHINE_FREQ_LOCKED          (1)

// Cap the XIP flash clock so the QMI divisor (and its equal RXDELAY, a 3-bit
// field, max 7) stays valid at both 252 and 378 MHz: 252/63->div4, 378/63->div6.
#define MICROPY_HW_FLASH_MAX_FREQ               (63 * 1000 * 1000)

// External Flash (16 MB)
#define MICROPY_HW_FLASH_SIZE_BYTES             (16 * 1024 * 1024)

// External PSRAM (8 MB) is configured in mpconfigboard.cmake -- the SDK's
// hardware_psram library brings it up during runtime_init, and the port needs
// the chip-select at CMake time to feed it (MICROPY_HW_ENABLE_PSRAM and
// MICROPY_HW_PSRAM_CS_PIN are defined for C from there).

// HDMI support enabled
#define MICROPY_HW_ENABLE_HDMI                  (1)
// HDMI (HSTX) lane -> HSTX bit mapping for the Pico Computer 3 (bit N -> GP12+N;
// positive on the given odd bit, negative on bit-1). GP12..19 are HSTX.
#define MICROPY_HW_HDMI_CLK                     (1)  // HSTX1 GP13 (-ve GP12)
#define MICROPY_HW_HDMI_D0                      (3)  // HSTX3 GP15 (-ve GP14)
#define MICROPY_HW_HDMI_D1                      (5)  // HSTX5 GP17 (-ve GP16)
#define MICROPY_HW_HDMI_D2                      (7)  // HSTX7 GP19 (-ve GP18)

// LVGL support enabled
#define MICROPY_HW_ENABLE_LVGL                  (1)

// Runtime board identification (board_detect.c, also gated on the matching
// MICROPY_HW_BOARD_DETECT in mpconfigboard.cmake). One firmware image runs on
// both the Pico Computer 3 and the Pico Computer 2; the DS3231 RTC's 32 kHz
// output on GP27 is fitted only to the 3, so a short probe for that square wave
// tells them apart -- the same test MMBasic makes in TestPicoComputer3(). The
// differences that follow from it (SD pinout and bus type, the CYW43 radio, the
// LED) are all settled at runtime. Probed from MICROPY_BOARD_STARTUP, the
// earliest hook in main(), before any driver touches its pins.
#define MICROPY_HW_BOARD_DETECT                 (1)
void board_detect_init(void);
bool board_has_cyw43(void);
bool board_is_pico_computer_2(void);
const char *board_machine_name(void);
#define MICROPY_BOARD_STARTUP()                 board_detect_init()

// Report the machine we are actually on, not the one the image was compiled
// for: the REPL banner and os.uname().machine both come from the detected
// board (MICROPY_HW_BOARD_NAME / MICROPY_HW_BOARD_NAME_ALT above).
#define MICROPY_BANNER_MACHINE_STR              board_machine_name()
#define MICROPY_PY_OS_UNAME_MACHINE_DYNAMIC     (1)
#define mp_os_uname_machine()                   board_machine_name()

// SD card, driven directly by the C machine.SDCard driver. Two pin sets, chosen
// at start-up from the detected board:
//
//   Pico Computer 3   CS 33, SCK 30, MOSI 31, MISO 28 -- all SPI1 pins, so the
//                     hardware SPI block drives it.
//   Pico Computer 2   CS 29, SCK 30, MOSI 31, MISO 32 -- MISO is a SPI0 pin
//                     while SCK/MOSI are SPI1, so no hardware instance covers
//                     the set and it is bit-banged (MMBasic does the same, see
//                     BitBangSendSPI in misc/SDCard.c). A negative SPI id is
//                     what selects the bit-banged transport.
#define MICROPY_PY_MACHINE_SDCARD               (1)
#define MICROPY_HW_SD_SPI_ID                    (1)
#define MICROPY_HW_SD_SCK                       (30)
#define MICROPY_HW_SD_MOSI                      (31)
#define MICROPY_HW_SD_MISO                      (28)
#define MICROPY_HW_SD_CS                        (33)
#define MICROPY_HW_SD_ALT_SPI_ID                (-1)
#define MICROPY_HW_SD_ALT_SCK                   (30)
#define MICROPY_HW_SD_ALT_MOSI                  (31)
#define MICROPY_HW_SD_ALT_MISO                  (32)
#define MICROPY_HW_SD_ALT_CS                    (29)
#define MICROPY_HW_SD_USE_ALT()                 board_is_pico_computer_2()
// Also expose the same bus as machine.SPI(1) for general use.
#define MICROPY_HW_SPI1_SCK                     (30)
#define MICROPY_HW_SPI1_MOSI                    (31)
#define MICROPY_HW_SPI1_MISO                    (28)

// Pin reservation logic (same as Pico2-W)
#define MICROPY_HW_PIN_EXT_COUNT                CYW43_WL_GPIO_COUNT
int mp_hal_is_pin_reserved(int n);
bool machine_sdcard_pin_reserved(int n);
// GPIOs protected from machine.Pin() use because they serve system functions:
//   8, 9        = console UART1 TX/RX
//   12..19      = HDMI HSTX (D0/D1/D2/CK differential pairs)
//   SD bus      = 28,30,31,33 on a Pico Computer 3; 29,30,31,32 on a Pico
//                 Computer 2 -- asked at runtime, since the boards differ
//   CYW43       = via mp_hal_is_pin_reserved(), which reserves nothing on a
//                 board with no radio fitted
#define MICROPY_HW_PIN_RESERVED(i) \
    (mp_hal_is_pin_reserved(i) || (i) == 8 || (i) == 9 \
     || ((i) >= 12 && (i) <= 19) \
     || machine_sdcard_pin_reserved(i))

