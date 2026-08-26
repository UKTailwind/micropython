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

// USB keyboard layout selection: the `keyboard` module (keymap()/keymaps()) and
// the active-layout state, shared with the HID report decoder in mp_usbh.c. Kept
// separate from mp_usbh.c because that file includes tusb.h, whose headers aren't
// on the QSTR-scan include path (and module registration needs QSTR scanning).

#include "py/runtime.h"
#include "py/ringbuf.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "keyboard_maps.h" // vendored MMBasic layout tables (US/UK/DE/FR/ES/BE)
#include "kbd_decode.h" // kbd_process_report -- no tusb dependency, unlike mp_usbh.c

// Held-key state, maintained by the HID report decoder in mp_usbh.c.
extern int usb_kbd_keydown(int n);
// Num-lock state and the per-keyboard memory of it, in mp_usbh.c (declared
// here rather than included, as that file pulls in tusb.h).
extern int usb_kbd_get_numlock(void);
extern void usb_kbd_set_numlock(int on);
extern void usb_kbd_numlock_pref(uint16_t vid, uint16_t pid, int on);
extern uint32_t usb_kbd_id(void);
extern uint16_t usb_kbd_desc(const uint8_t **p);
extern int usb_kbd_numlock_led(void);
// The REPL input ring buffer: keydown() drains it, as MMBasic's KEYDOWN() does.
extern ringbuf_t stdin_ringbuf;

// Active layout: table[usage*2 + shift], value 0 = not a printing key. Read by
// the report decoder in mp_usbh.c.
const int *kbd_layout = USkeyValue;
const char *kbd_layout_name = "US";

static const struct {
    const char *name;
    const int *table;
} kbd_layouts[] = {
    { "US", USkeyValue }, { "UK", UKkeyValue }, { "DE", DEkeyValue },
    { "FR", FRkeyValue }, { "ES", ESkeyValue }, { "BE", BEkeyValue },
};

// keymap() -> current name; keymap("UK") -> select a layout.
static mp_obj_t kbd_keymap(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return mp_obj_new_str(kbd_layout_name, strlen(kbd_layout_name));
    }
    const char *name = mp_obj_str_get_str(args[0]);
    for (size_t i = 0; i < MP_ARRAY_SIZE(kbd_layouts); i++) {
        if (strcmp(name, kbd_layouts[i].name) == 0) {
            kbd_layout = kbd_layouts[i].table;
            kbd_layout_name = kbd_layouts[i].name;
            return mp_const_none;
        }
    }
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("unknown keymap '%s'"), name);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kbd_keymap_obj, 0, 1, kbd_keymap);

// keymaps() -> list of available layout names.
static mp_obj_t kbd_keymaps(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < MP_ARRAY_SIZE(kbd_layouts); i++) {
        mp_obj_list_append(list, mp_obj_new_str(kbd_layouts[i].name, strlen(kbd_layouts[i].name)));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(kbd_keymaps_obj, kbd_keymaps);

// keydown(n=0) -- which keys are held right now (MMBasic KEYDOWN()):
//   keydown()  / keydown(0) -> number of keys currently held (0..6)
//   keydown(1) .. keydown(6) -> code of the nth held key (1 = most recent;
//     0 = none). Printing keys give their character code for the current
//     layout/shift/caps; cursor and function keys give the codes exposed as
//     module constants (keyboard.UP, keyboard.F1, ...).
//   keydown(7) -> modifier bitmap: 1 L-Alt, 2 L-Ctrl, 4 L-GUI, 8 L-Shift,
//     16 R-Alt, 32 R-Ctrl, 64 R-GUI, 128 R-Shift.
//   keydown(8) -> lock bitmap: 1 caps, 2 num, 4 scroll.
// Like MMBasic, every call also drains pending console input so keys polled by
// a program don't pile up as typed-ahead REPL input.
static mp_obj_t kbd_keydown(size_t n_args, const mp_obj_t *args) {
    mp_int_t n = (n_args == 0) ? 0 : mp_obj_get_int(args[0]);
    if (n < 0 || n > 8) {
        mp_raise_ValueError(MP_ERROR_TEXT("keydown(n): n must be 0..8"));
    }
    while (ringbuf_get(&stdin_ringbuf) != -1) {
    }
    return MP_OBJ_NEW_SMALL_INT(usb_kbd_keydown(n));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kbd_keydown_obj, 0, 1, kbd_keydown);

// numlock() -> current state; numlock(True/False) -> set it, and remember it
// for the keyboard that is plugged in. MMBasic's Option.numlock, except that
// the setting is per keyboard: a compact keyboard (Raspberry Pi, most laptop
// boards) overlays a numeric keypad onto 7890/uiop/jkl;/m while num-lock is on,
// driven by its own firmware from the LED report we send, and NOTHING in the
// USB descriptors distinguishes such a keyboard from a full-size one -- their
// HID report descriptors are byte-identical. So this is remembered per VID:PID
// rather than detected, and pressing Num Lock is itself enough to save it.
static mp_obj_t kbd_numlock(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return mp_obj_new_bool(usb_kbd_get_numlock());
    }
    usb_kbd_set_numlock(mp_obj_is_true(args[0]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kbd_numlock_obj, 0, 1, kbd_numlock);

// numlock_pref(vid, pid, on) -- seed the remembered setting for a keyboard that
// isn't plugged in (yet). _boot_board calls this once per entry saved in
// /settings.json, so a keyboard is right from the first LED report after mount.
static mp_obj_t kbd_numlock_pref(mp_obj_t vid_in, mp_obj_t pid_in, mp_obj_t on_in) {
    mp_int_t vid = mp_obj_get_int(vid_in), pid = mp_obj_get_int(pid_in);
    if (vid < 0 || vid > 0xffff || pid < 0 || pid > 0xffff) {
        mp_raise_ValueError(MP_ERROR_TEXT("vid/pid must be 0..65535"));
    }
    usb_kbd_numlock_pref((uint16_t)vid, (uint16_t)pid, mp_obj_is_true(on_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(kbd_numlock_pref_obj, kbd_numlock_pref);

// kbd_id() -> (vid, pid) of the mounted keyboard, or None if there isn't one.
// What the saved num-lock settings are keyed on.
static mp_obj_t kbd_id(void) {
    uint32_t id = usb_kbd_id();
    if (id == 0) {
        return mp_const_none;
    }
    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(id >> 16), MP_OBJ_NEW_SMALL_INT(id & 0xffff),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(kbd_id_obj, kbd_id);

// kbd_desc() -> the mounted keyboard's HID report descriptor as bytes (empty if
// no keyboard). For working out what a new keyboard reports without rebuilding.
static mp_obj_t kbd_desc(void) {
    const uint8_t *p = NULL;
    uint16_t len = usb_kbd_desc(&p);
    return mp_obj_new_bytes(p, len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(kbd_desc_obj, kbd_desc);

// numlock_led() -> True if the mounted keyboard declares a Num Lock LED. This is
// where the num-lock default comes from for a keyboard with no saved setting: a
// keyboard with no Num Lock light almost certainly has no numeric keypad. Note
// the reverse doesn't follow — plenty of keyboards declare the LED and have no
// keypad — so this only ever supplies a default, never overrides a saved choice.
static mp_obj_t kbd_numlock_led(void) {
    return mp_obj_new_bool(usb_kbd_numlock_led());
}
static MP_DEFINE_CONST_FUN_OBJ_0(kbd_numlock_led_obj, kbd_numlock_led);

// Python callback invoked (via the scheduler) when the user presses Num Lock,
// called as cb((vid, pid, on)) so the choice can be persisted. Rooted so the GC
// keeps it alive; scheduled from mp_usbh.c's lock-key seam.
MP_REGISTER_ROOT_POINTER(mp_obj_t usbh_numlock_cb);

// on_numlock(cb) registers cb; on_numlock(None) or on_numlock() clears it.
static mp_obj_t kbd_on_numlock(size_t n_args, const mp_obj_t *args) {
    MP_STATE_PORT(usbh_numlock_cb) = (n_args == 0) ? mp_const_none : args[0];
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kbd_on_numlock_obj, 0, 1, kbd_on_numlock);

// Python callback invoked (via the scheduler) on every keypress and auto-repeat,
// called as cb(code) with the same key codes keydown() reports. Rooted so the GC
// keeps it alive (registered here, not mp_usbh.c, for QSTR/root scanning).
MP_REGISTER_ROOT_POINTER(mp_obj_t usbh_key_cb);

// on_key(cb) registers cb; on_key(None) or on_key() clears it.
static mp_obj_t kbd_on_key(size_t n_args, const mp_obj_t *args) {
    MP_STATE_PORT(usbh_key_cb) = (n_args == 0) ? mp_const_none : args[0];
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kbd_on_key_obj, 0, 1, kbd_on_key);

// Python callback invoked (via the scheduler) when any USB device is mounted or
// unmounted; called as cb(True) on connect, cb(False) on disconnect. The mount/
// unmount hooks in mp_usbh.c schedule it. Rooted so the GC keeps it alive. This
// lives here (not mp_usbh.c) because MP_REGISTER_ROOT_POINTER needs QSTR/root
// scanning, which skips mp_usbh.c (it includes tusb.h).
MP_REGISTER_ROOT_POINTER(mp_obj_t usbh_event_cb);

// on_usb_event(cb) registers cb; on_usb_event(None) or on_usb_event() clears it.
static mp_obj_t kbd_on_usb_event(size_t n_args, const mp_obj_t *args) {
    MP_STATE_PORT(usbh_event_cb) = (n_args == 0) ? mp_const_none : args[0];
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kbd_on_usb_event_obj, 0, 1, kbd_on_usb_event);

// keyboard.inject_report(data) -- feed an externally-sourced 8-byte HID boot
// keyboard report (e.g. from a Bluetooth LE HID-over-GATT keyboard, see
// pcbtkbd.py) into the same decoder USB keyboards use. slot -1: no lock-LED
// handling on this path -- a BLE keyboard's LEDs (if it has any) are pushed
// by writing its GATT Report characteristic directly, not through here.
static mp_obj_t kbd_inject_report(mp_obj_t data) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);
    kbd_process_report(bufinfo.buf, bufinfo.len, -1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(kbd_inject_report_obj, kbd_inject_report);

static const mp_rom_map_elem_t keyboard_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_keyboard) },
    { MP_ROM_QSTR(MP_QSTR_keymap), MP_ROM_PTR(&kbd_keymap_obj) },
    { MP_ROM_QSTR(MP_QSTR_keymaps), MP_ROM_PTR(&kbd_keymaps_obj) },
    { MP_ROM_QSTR(MP_QSTR_keydown), MP_ROM_PTR(&kbd_keydown_obj) },
    { MP_ROM_QSTR(MP_QSTR_numlock), MP_ROM_PTR(&kbd_numlock_obj) },
    { MP_ROM_QSTR(MP_QSTR_numlock_pref), MP_ROM_PTR(&kbd_numlock_pref_obj) },
    { MP_ROM_QSTR(MP_QSTR_numlock_led), MP_ROM_PTR(&kbd_numlock_led_obj) },
    { MP_ROM_QSTR(MP_QSTR_kbd_id), MP_ROM_PTR(&kbd_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_kbd_desc), MP_ROM_PTR(&kbd_desc_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_numlock), MP_ROM_PTR(&kbd_on_numlock_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_key), MP_ROM_PTR(&kbd_on_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_usb_event), MP_ROM_PTR(&kbd_on_usb_event_obj) },
    { MP_ROM_QSTR(MP_QSTR_inject_report), MP_ROM_PTR(&kbd_inject_report_obj) },
    // Key codes reported by keydown(1..6) / on_key for the non-printing keys
    // (MMBasic's codes, from the vendored layout tables). Shift-variants of some
    // exist too, e.g. Shift-Down = 0xA1, Shift-F1 = 0xB1.
    { MP_ROM_QSTR(MP_QSTR_UP), MP_ROM_INT(0x80) },
    { MP_ROM_QSTR(MP_QSTR_DOWN), MP_ROM_INT(0x81) },
    { MP_ROM_QSTR(MP_QSTR_LEFT), MP_ROM_INT(0x82) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT), MP_ROM_INT(0x83) },
    { MP_ROM_QSTR(MP_QSTR_INS), MP_ROM_INT(0x84) },
    { MP_ROM_QSTR(MP_QSTR_DEL), MP_ROM_INT(0x7f) },
    { MP_ROM_QSTR(MP_QSTR_HOME), MP_ROM_INT(0x86) },
    { MP_ROM_QSTR(MP_QSTR_END), MP_ROM_INT(0x87) },
    { MP_ROM_QSTR(MP_QSTR_PGUP), MP_ROM_INT(0x88) },
    { MP_ROM_QSTR(MP_QSTR_PGDN), MP_ROM_INT(0x89) },
    { MP_ROM_QSTR(MP_QSTR_ENTER), MP_ROM_INT(10) },
    { MP_ROM_QSTR(MP_QSTR_ESC), MP_ROM_INT(27) },
    { MP_ROM_QSTR(MP_QSTR_TAB), MP_ROM_INT(9) },
    { MP_ROM_QSTR(MP_QSTR_BKSP), MP_ROM_INT(8) },
    { MP_ROM_QSTR(MP_QSTR_F1), MP_ROM_INT(0x91) },
    { MP_ROM_QSTR(MP_QSTR_F2), MP_ROM_INT(0x92) },
    { MP_ROM_QSTR(MP_QSTR_F3), MP_ROM_INT(0x93) },
    { MP_ROM_QSTR(MP_QSTR_F4), MP_ROM_INT(0x94) },
    { MP_ROM_QSTR(MP_QSTR_F5), MP_ROM_INT(0x95) },
    { MP_ROM_QSTR(MP_QSTR_F6), MP_ROM_INT(0x96) },
    { MP_ROM_QSTR(MP_QSTR_F7), MP_ROM_INT(0x97) },
    { MP_ROM_QSTR(MP_QSTR_F8), MP_ROM_INT(0x98) },
    { MP_ROM_QSTR(MP_QSTR_F9), MP_ROM_INT(0x99) },
    { MP_ROM_QSTR(MP_QSTR_F10), MP_ROM_INT(0x9a) },
    { MP_ROM_QSTR(MP_QSTR_F11), MP_ROM_INT(0x9b) },
    { MP_ROM_QSTR(MP_QSTR_F12), MP_ROM_INT(0x9c) },
};
static MP_DEFINE_CONST_DICT(keyboard_module_globals, keyboard_module_globals_table);

const mp_obj_module_t keyboard_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&keyboard_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_keyboard, keyboard_module);

#endif // MICROPY_HW_USB_HOST
