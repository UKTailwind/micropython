/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND...
 */

// MicroPython's backend for the shared HID keyboard decoder core
// (kbd_decode.c, vendored byte-identical here and in the PC3 Fuzix
// kernel - see kbd_decode.h).  Translated bytes go to the REPL's stdin
// ring (the interrupt character raising KeyboardInterrupt on the way),
// mapped key codes go to the keyboard.on_key callback, and time comes
// from mp_hal_ticks_ms.  The PC emulator compiles this same file:
// kbd_sdl.c owns stdin_ringbuf there, and unix_mphal tracks
// mp_interrupt_char.  The Fuzix kernel's equivalents live in usbkbd.c.

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/ringbuf.h"
// The interrupt character (usually 3, Ctrl-C). A bare extern rather than
// shared/runtime/interrupt_char.h: that header's mp_hal_set_interrupt_char
// prototype conflicts with the unix port's, and rp2's mphal already declares
// everything else.
extern int mp_interrupt_char;

#if MICROPY_HW_USB_HOST

#include "kbd_decode.h"

// The REPL's input ring buffer; translated keystrokes are pushed here.
extern ringbuf_t stdin_ringbuf;

void kbd_push(uint8_t c) {
    #if MICROPY_KBD_EXCEPTION
    if (c == mp_interrupt_char) {
        mp_sched_keyboard_interrupt();
        return;
    }
    #endif
    ringbuf_put(&stdin_ringbuf, c);
}

uint32_t kbd_ticks_ms(void) {
    return (uint32_t)mp_hal_ticks_ms();
}

// Schedule the registered Python key callback (keyboard.on_key) with the
// mapped key code; the core calls this on each new keypress and on each
// synthesised auto-repeat, so the callback sees the same stream a console
// reader would.  Runs in thread context (tuh_task / mp_usbh_task), so
// mp_sched_schedule is safe; a missing callback or a full scheduler queue
// is silently ignored.
void kbd_backend_on_key(int code) {
    mp_obj_t cb = MP_STATE_PORT(usbh_key_cb);
    if (cb != MP_OBJ_NULL && cb != mp_const_none) {
        mp_sched_schedule(cb, MP_OBJ_NEW_SMALL_INT(code));
    }
}

// One short diagnostic line from the core (the orphaned-repeat guard).
void kbd_backend_msg(const char *s) {
    mp_printf(&mp_plat_print, "%s", s);
}

#endif // MICROPY_HW_USB_HOST
