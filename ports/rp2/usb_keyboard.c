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

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "keyboard_maps.h" // vendored MMBasic layout tables (US/UK/DE/FR/ES/BE)

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

static const mp_rom_map_elem_t keyboard_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_keyboard) },
    { MP_ROM_QSTR(MP_QSTR_keymap), MP_ROM_PTR(&kbd_keymap_obj) },
    { MP_ROM_QSTR(MP_QSTR_keymaps), MP_ROM_PTR(&kbd_keymaps_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_usb_event), MP_ROM_PTR(&kbd_on_usb_event_obj) },
};
static MP_DEFINE_CONST_DICT(keyboard_module_globals, keyboard_module_globals_table);

const mp_obj_module_t keyboard_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&keyboard_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_keyboard, keyboard_module);

#endif // MICROPY_HW_USB_HOST
