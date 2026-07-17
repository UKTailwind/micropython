/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * (Licence text as per ports/rp2/hdmi.c.)
 */

// The PC-emulator keyboard: SDL key events synthesised into 8-byte HID boot
// reports and fed to the firmware's own decoder (kbd_decode.c) -- possible
// because SDL scancodes ARE USB HID usage codes. Layouts (keymap("UK")),
// lock keys, auto-repeat, keydown(), on_key and Ctrl-C therefore behave
// exactly as on the machine, driven by identical code.
//
// Also provides the `_emukbd` module: the REPL bridge (emuboot registers a
// dupterm stream whose read() drains the same stdin ring buffer the USB
// keyboard fills on hardware) and an inject() hook the test suite uses to
// press keys without a window.

#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/ringbuf.h"

#if MICROPY_HW_USB_HOST

#include "../../../../kbd_decode.h"

// The stdin ring buffer the decoder pushes translated keystrokes into (the
// rp2 port defines this in its mphalport; the unix port has no equivalent,
// so the emulator owns one).
static uint8_t stdin_buf[260];
ringbuf_t stdin_ringbuf = { stdin_buf, sizeof(stdin_buf), 0, 0 };

// The unix port manages its interrupt char through signals and has no
// shared/runtime/interrupt_char.c; define the global the shared decoder
// tests against. 3 = Ctrl-C: a window Ctrl-C always interrupts, exactly as
// the USB keyboard's does on the machine.
int mp_interrupt_char = 3;

// No physical lock LEDs to drive.
void kbd_backend_set_leds(int slot, uint8_t leds) {
    (void)slot;
    (void)leds;
}

// --- SDL keys -> HID boot reports -------------------------------------------

static uint8_t sdl_held[6]; // pressed usages, report order (0 = free)

// SDL_Keymod -> HID modifier byte (bit0 LCtrl, 1 LShift, 2 LAlt, 3 LGui,
// 4 RCtrl, 5 RShift, 6 RAlt, 7 RGui). SDL: LSHIFT=1, RSHIFT=2, LCTRL=0x40,
// RCTRL=0x80, LALT=0x100, RALT=0x200, LGUI=0x400, RGUI=0x800.
static uint8_t mods_from_sdl(int m) {
    return (uint8_t)(((m & 0x0040) ? 0x01 : 0) | ((m & 0x0001) ? 0x02 : 0)
        | ((m & 0x0100) ? 0x04 : 0) | ((m & 0x0400) ? 0x08 : 0)
        | ((m & 0x0080) ? 0x10 : 0) | ((m & 0x0002) ? 0x20 : 0)
        | ((m & 0x0200) ? 0x40 : 0) | ((m & 0x0800) ? 0x80 : 0));
}

static void send_report(uint8_t mods) {
    uint8_t r[8] = { mods, 0, sdl_held[0], sdl_held[1], sdl_held[2],
                     sdl_held[3], sdl_held[4], sdl_held[5] };
    kbd_process_report(r, 8, -1);
}

// One key transition (usage = HID usage id == SDL scancode). Called from the
// SDL thread; also from _emukbd.inject() for the tests.
static void kbd_transition(int usage, int down, uint8_t mods) {
    if (usage < 4 || usage > 0xE7) {
        return;
    }
    if (usage >= 0xE0) {
        // Modifier keys live in the mods byte, not the key array; SDL's mod
        // state (passed in) already reflects them. Report the state change.
        send_report(mods);
        return;
    }
    if (down) {
        for (int i = 0; i < 6; i++) {
            if (sdl_held[i] == usage) {
                return; // already held
            }
        }
        for (int i = 0; i < 6; i++) {
            if (sdl_held[i] == 0) {
                sdl_held[i] = (uint8_t)usage;
                break; // >6 keys: dropped, as a boot keyboard would
            }
        }
    } else {
        for (int i = 0; i < 6; i++) {
            if (sdl_held[i] == usage) {
                sdl_held[i] = 0;
            }
        }
    }
    send_report(mods);
}

// Entry points for the SDL thread (hdmi_sdl.c's event loop).
void pc3emu_kbd_sdl_key(int scancode, int down, int sdl_mods) {
    kbd_transition(scancode, down, mods_from_sdl(sdl_mods));
}

void pc3emu_kbd_tick(void) {
    kbd_repeat_check();
}

// --- the _emukbd module ------------------------------------------------------

// read() -> one byte from the keyboard's stdin stream, or None. emuboot's
// dupterm stream calls this so REPL input flows from the window.
static mp_obj_t emukbd_read(void) {
    int c = ringbuf_get(&stdin_ringbuf);
    if (c < 0) {
        return mp_const_none;
    }
    byte b = (byte)c;
    return mp_obj_new_bytes(&b, 1);
}
static MP_DEFINE_CONST_FUN_OBJ_0(emukbd_read_obj, emukbd_read);

// any() -> bytes waiting (cheap poll for the dupterm stream).
static mp_obj_t emukbd_any(void) {
    return MP_OBJ_NEW_SMALL_INT(ringbuf_avail(&stdin_ringbuf));
}
static MP_DEFINE_CONST_FUN_OBJ_0(emukbd_any_obj, emukbd_any);

// inject(usage, mods=0, down=True) -- press/release a key programmatically,
// exactly as if the SDL window had reported it. For the test suite.
static mp_obj_t emukbd_inject(size_t n_args, const mp_obj_t *args) {
    int usage = mp_obj_get_int(args[0]);
    int mods = (n_args > 1) ? mp_obj_get_int(args[1]) : 0;
    int down = (n_args > 2) ? mp_obj_is_true(args[2]) : 1;
    kbd_transition(usage, down, (uint8_t)mods);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(emukbd_inject_obj, 1, 3, emukbd_inject);

// tick() -- run the auto-repeat check (the SDL thread does this itself; the
// test suite calls it to pass time deterministically).
static mp_obj_t emukbd_tick(void) {
    kbd_repeat_check();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(emukbd_tick_obj, emukbd_tick);

static const mp_rom_map_elem_t emukbd_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__emukbd) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&emukbd_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&emukbd_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_inject), MP_ROM_PTR(&emukbd_inject_obj) },
    { MP_ROM_QSTR(MP_QSTR_tick), MP_ROM_PTR(&emukbd_tick_obj) },
};
static MP_DEFINE_CONST_DICT(emukbd_module_globals, emukbd_module_globals_table);

const mp_obj_module_t emukbd_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&emukbd_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__emukbd, emukbd_module);

#endif // MICROPY_HW_USB_HOST
