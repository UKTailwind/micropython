/*
 * Runtime board identification for the rp2 port.
 *
 * The Pico Computer 3 carries a DS3231 RTC whose 32 kHz output is wired to
 * GP27. Both boards have the RTC, but only the 3 routes that clock to a pin, so
 * a short probe for a square wave there tells the two apart -- exactly the test
 * MMBasic uses (TestPicoComputer3() in PicoMite.c).
 *
 * One firmware image runs on both machines, and they differ in ways that have
 * to be settled before any driver starts: the SD card is on different pins (and
 * on the Pico Computer 2 those pins do not map to a hardware SPI instance, so
 * it is bit-banged), and the Pico Computer 2 has no CYW43 radio, which frees
 * GP23/24/25/29 for other use -- GP25 is its LED and GP29 its SD chip select.
 * Bringing the CYW43 up on such a board would take those pins over and break
 * both.
 *
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Peter Mather
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MICROPY_INCLUDED_RP2_BOARD_DETECT_H
#define MICROPY_INCLUDED_RP2_BOARD_DETECT_H

#include <stdbool.h>
#include <stdint.h>

// Identified hardware. Values are part of the `board` module's Python API, so
// only ever append to this list.
enum {
    BOARD_ID_UNKNOWN = 0,           // reserved: no identification made
    BOARD_ID_PICO_COMPUTER_3 = 1,   // DS3231 32 kHz square wave seen on GP27
    BOARD_ID_PICO_COMPUTER_2 = 2,   // no 32 kHz on GP27: the other board in the family
};

// Probe for the board signature. Called once from MICROPY_BOARD_STARTUP(),
// before any driver looks at its pins; the result is fixed for the life of the
// firmware run (it survives soft reset, as hardware cannot change under us).
void board_detect_init(void);

// Identity found by board_detect_init(), one of the BOARD_ID_* values above.
uint8_t board_detect_id(void);

// Force the identity, overriding (or standing in for) the probe. The escape
// hatch for a Pico Computer 3 whose DS3231 has stopped -- MMBasic has the same
// thing in its saved `platform` option. Only affects hardware brought up after
// the call, so from Python it is useful for the SD card and little else.
void board_detect_set_id(uint8_t id);

// Convenience tests for the individual boards.
bool board_is_pico_computer_3(void);
bool board_is_pico_computer_2(void);

// True if this board has the CYW43 radio (Wi-Fi/Bluetooth) fitted. Anything
// that would drive the CYW43's pins must check this first: on a board without
// the chip those pins belong to something else.
bool board_has_cyw43(void);

// GPIO number of the board's LED, or -1 when the LED is not on a GPIO at all
// (the Pico Computer 3 drives it from CYW43 GPIO0).
int board_led_pin(void);

// Human-readable name for the detected board.
const char *board_detect_name(void);

#endif // MICROPY_INCLUDED_RP2_BOARD_DETECT_H
