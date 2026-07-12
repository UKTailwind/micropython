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

// _sercon: internal serial-console output control for pcconsole's
// console("both"/"serial"/"screen") routing (MMBasic OPTION CONSOLE).
// Muting suppresses REPL/stdout bytes to the UART only — UART *input* and
// direct writers (XMODEM's _outbyte) are unaffected, so a muted serial
// terminal can still type (blind) and Ctrl-C still works.

#include "py/runtime.h"

#if MICROPY_HW_ENABLE_UART_REPL

#include "uart.h"

// _sercon.mute(True/False) sets; _sercon.mute() reads. Default False (both).
static mp_obj_t sercon_mute(size_t n_args, const mp_obj_t *args) {
    if (n_args > 0) {
        mp_uart_repl_mute = mp_obj_is_true(args[0]);
    }
    return mp_obj_new_bool(mp_uart_repl_mute);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sercon_mute_obj, 0, 1, sercon_mute);

static const mp_rom_map_elem_t sercon_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__sercon) },
    { MP_ROM_QSTR(MP_QSTR_mute), MP_ROM_PTR(&sercon_mute_obj) },
};
static MP_DEFINE_CONST_DICT(sercon_module_globals, sercon_module_globals_table);

const mp_obj_module_t sercon_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sercon_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__sercon, sercon_module);

#endif // MICROPY_HW_ENABLE_UART_REPL
