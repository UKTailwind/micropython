// Runtime board identification for the Pico Computer family.
//
// One firmware image serves both machines:
//
//   Pico Computer 3   DS3231 32 kHz output wired to GP27, CYW43 Wi-Fi/BT,
//                     LED on CYW43 GPIO0, SD card on hardware SPI1.
//   Pico Computer 2   DS3231 fitted but its 32 kHz output not connected, no
//                     CYW43 -- so GP23/24 are free, the LED is on GP25, and the
//                     SD card sits on GP29/30/31/32 (bit-banged, see
//                     machine_sdcard.c).
//
// The 32 kHz clock on GP27 is the signature: both boards have the RTC, but only
// the 3 routes that output to a pin, so watching GP27 for a couple of
// square-wave cycles says which machine this is. That is MMBasic's test --
// TestPicoComputer3() in PicoMite.c -- and this is a direct port of it: same
// pin, same pull-up, same 200 us window, same four edge waits.
//
// A board with no clock on GP27 is taken to be a Pico Computer 2. That is the
// only other board in the family, so "not a 3" identifies it; a third board
// would need a positive signature of its own rather than inheriting this
// fallback. It also means a Pico Computer 3 whose DS3231 has stopped (or has
// had EN32kHz cleared) looks like a Pico Computer 2 -- board.override() exists
// for that case.
//
// The answer is needed before the SD card and the CYW43 are brought up, so the
// probe runs from MICROPY_BOARD_STARTUP() -- the earliest hook in main(), right
// after the system clock is set and long before any driver claims a pin.
//
//   import board
//   board.id()        # board.PICO_COMPUTER_3 / board.PICO_COMPUTER_2
//   board.name()      # "PICO COMPUTER 3" / "PICO COMPUTER 2"
//   board.has_wifi()  # False on a Pico Computer 2 (no CYW43 fitted)
//   board.led_pin()   # 25 on a Pico Computer 2, None when it is a CYW43 GPIO

#include "py/runtime.h"
#include "board_detect.h"

#if MICROPY_HW_BOARD_DETECT

#include "hardware/gpio.h"
#include "pico/time.h"

// GP27 = DS3231 32 kHz output on the Pico Computer 3.
#ifndef MICROPY_HW_BOARD_DETECT_PIN
#define MICROPY_HW_BOARD_DETECT_PIN (27)
#endif

// Detection window, microseconds. Four edges of a 32768 Hz square wave take
// 4 * 15.26 = 61 us, plus up to another 15 us waiting for the first one, so
// MMBasic's 200 us is a comfortable margin over the ~76 us worst case while
// still being an imperceptible pause when no signal is present.
#ifndef MICROPY_HW_BOARD_DETECT_US
#define MICROPY_HW_BOARD_DETECT_US (200)
#endif

// LED GPIO on the Pico Computer 2 (the Pico Computer 3 drives its LED from
// CYW43 GPIO0, which is not a GPIO of this chip at all).
#ifndef MICROPY_HW_BOARD_PC2_LED_PIN
#define MICROPY_HW_BOARD_PC2_LED_PIN (25)
#endif

static uint8_t board_id = BOARD_ID_UNKNOWN;

void board_detect_init(void) {
    const uint pin = MICROPY_HW_BOARD_DETECT_PIN;

    // Input with a pull-up: the DS3231's 32 kHz pin is open drain, and the
    // pull-up also parks the input at a known level on a board that leaves
    // GP27 floating (which then simply times out below).
    gpio_init(pin);
    gpio_set_input_enabled(pin, true);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);

    // Wait for two full cycles (high -> low -> high -> low). A floating or
    // statically driven pin never completes the sequence and runs out the
    // clock instead.
    uint64_t timeout = time_us_64() + MICROPY_HW_BOARD_DETECT_US;
    while (gpio_get(pin) && time_us_64() < timeout) {
    }
    while (!gpio_get(pin) && time_us_64() < timeout) {
    }
    while (gpio_get(pin) && time_us_64() < timeout) {
    }
    while (!gpio_get(pin) && time_us_64() < timeout) {
    }

    // Time left over means all four edges arrived: a live 32 kHz clock.
    board_id = time_us_64() < timeout ? BOARD_ID_PICO_COMPUTER_3 : BOARD_ID_PICO_COMPUTER_2;

    if (board_id == BOARD_ID_PICO_COMPUTER_2) {
        // GP27 is an ordinary GPIO on this board; leave no pull-up behind.
        gpio_disable_pulls(pin);
    }
}

uint8_t board_detect_id(void) {
    return board_id;
}

void board_detect_set_id(uint8_t id) {
    board_id = id;
}

bool board_is_pico_computer_3(void) {
    return board_id == BOARD_ID_PICO_COMPUTER_3;
}

bool board_is_pico_computer_2(void) {
    return board_id == BOARD_ID_PICO_COMPUTER_2;
}

bool board_has_cyw43(void) {
    // Only the Pico Computer 3 has the radio. An unidentified board is treated
    // as not having it: the cost of skipping Wi-Fi is a missing feature, while
    // the cost of driving the CYW43 pins on a board that uses them for its LED
    // and SD chip select is a broken filesystem.
    return board_id == BOARD_ID_PICO_COMPUTER_3;
}

int board_led_pin(void) {
    return board_id == BOARD_ID_PICO_COMPUTER_2 ? MICROPY_HW_BOARD_PC2_LED_PIN : -1;
}

const char *board_detect_name(void) {
    switch (board_id) {
        case BOARD_ID_PICO_COMPUTER_3:
            return "PICO COMPUTER 3";
        case BOARD_ID_PICO_COMPUTER_2:
            return "PICO COMPUTER 2";
        default:
            return "UNKNOWN";
    }
}

// --- Python interface -------------------------------------------------------

static mp_obj_t board_id_fun(void) {
    return MP_OBJ_NEW_SMALL_INT(board_detect_id());
}
static MP_DEFINE_CONST_FUN_OBJ_0(board_id_obj, board_id_fun);

static mp_obj_t board_name(void) {
    return mp_obj_new_str_from_cstr(board_detect_name());
}
static MP_DEFINE_CONST_FUN_OBJ_0(board_name_obj, board_name);

static mp_obj_t board_has_wifi(void) {
    return mp_obj_new_bool(board_has_cyw43());
}
static MP_DEFINE_CONST_FUN_OBJ_0(board_has_wifi_obj, board_has_wifi);

// led_pin() -> GPIO number of the LED, or None when it is on the CYW43.
static mp_obj_t board_led_pin_fun(void) {
    int pin = board_led_pin();
    return pin < 0 ? mp_const_none : MP_OBJ_NEW_SMALL_INT(pin);
}
static MP_DEFINE_CONST_FUN_OBJ_0(board_led_pin_obj, board_led_pin_fun);

// override(id): force the identity when the probe got it wrong (a Pico
// Computer 3 with a dead DS3231 looks like a Pico Computer 2). The probe runs
// outside the soft-reset loop, so an override survives Ctrl-D and the board
// comes back up as the stated machine -- that, not the bare call, is how to
// recover an SD card on the wrong pins. Anything set up before the soft-reset
// loop (the CYW43 radio) still needs a power cycle.
static mp_obj_t board_override(mp_obj_t id_in) {
    mp_int_t id = mp_obj_get_int(id_in);
    if (id != BOARD_ID_UNKNOWN && id != BOARD_ID_PICO_COMPUTER_3 && id != BOARD_ID_PICO_COMPUTER_2) {
        mp_raise_ValueError(MP_ERROR_TEXT("unknown board id"));
    }
    board_detect_set_id(id);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(board_override_obj, board_override);

static const mp_rom_map_elem_t board_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_board) },
    { MP_ROM_QSTR(MP_QSTR_id), MP_ROM_PTR(&board_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_name), MP_ROM_PTR(&board_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_has_wifi), MP_ROM_PTR(&board_has_wifi_obj) },
    { MP_ROM_QSTR(MP_QSTR_led_pin), MP_ROM_PTR(&board_led_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_override), MP_ROM_PTR(&board_override_obj) },
    { MP_ROM_QSTR(MP_QSTR_UNKNOWN), MP_ROM_INT(BOARD_ID_UNKNOWN) },
    { MP_ROM_QSTR(MP_QSTR_PICO_COMPUTER_3), MP_ROM_INT(BOARD_ID_PICO_COMPUTER_3) },
    { MP_ROM_QSTR(MP_QSTR_PICO_COMPUTER_2), MP_ROM_INT(BOARD_ID_PICO_COMPUTER_2) },
};
static MP_DEFINE_CONST_DICT(board_module_globals, board_module_globals_table);

const mp_obj_module_t board_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&board_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_board, board_module);

#endif // MICROPY_HW_BOARD_DETECT
