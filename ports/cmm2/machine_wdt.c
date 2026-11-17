/*
 * machine.WDT's port part (included by extmod/machine_wdt.c): the firmware's WATCHDOG. WDT(timeout=ms) sets it going
 * and feed() sets it again, as an MMBasic program does; if it runs out, the CMM2 restarts (MM.WATCHDOG is then true),
 * as a PC3's hardware watchdog restarts the board. As there, it keeps running after Ctrl-C and a soft reset; leaving
 * MicroPython (machine.reset()) stops it, as a PC3's reset does.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <stdio.h>
#include "modcmm2.h"

typedef struct _machine_wdt_obj_t {
    mp_obj_base_t base;
} machine_wdt_obj_t;

static const machine_wdt_obj_t machine_wdt = {{&machine_wdt_type}};

int cmm2_wdt_ms;

static void mp_machine_wdt_feed(machine_wdt_obj_t *self) {
    char s[24];
    snprintf(s, sizeof(s), "WATCHDOG %d", cmm2_wdt_ms);
    cmm2_basic(s);
}

static machine_wdt_obj_t *mp_machine_wdt_make_new_instance(mp_obj_t id, mp_int_t timeout_ms) {
    if (mp_obj_get_int(id) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("WDT(0) only"));
    }
    if (timeout_ms < 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("timeout is 1 ms up"));
    }
    cmm2_wdt_ms = timeout_ms;
    mp_machine_wdt_feed((machine_wdt_obj_t *)&machine_wdt);
    return (machine_wdt_obj_t *)&machine_wdt;
}
