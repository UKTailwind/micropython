/*
 * The cmm2 module: MMBasic's own commands and functions from Python, through the loader's basic_cmd and basic_eval
 * services (versions 10 and 12), for what only the firmware knows or does:
 *
 *   cmm2.basic('TIME$ = "09:30:00"')       one MMBasic statement, its arguments constants; MMBasic's error raised
 *   cmm2.evaluate('MM.INFO(SOUND)')         an MMBasic expression's value, as a str
 *
 * The clock helpers (ds3231.settime) and machine.WDT (the WATCHDOG command) are built on it.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <string.h>
#include <stdio.h>
#include "py/runtime.h"
#include "cmm2.h"
#include "modcmm2.h"

// a service's result raised as Python's: -3 too long, -4 MMBasic's error with its message
static void cmm2_basic_check(int r) {
    if (r == -3) {
        mp_raise_ValueError(MP_ERROR_TEXT("too long for MMBasic"));
    }
    if (r < 0) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), cmm2->io_error());
    }
}

void cmm2_basic(const char *statement) {
    cmm2_basic_check(cmm2->basic_cmd(statement));
}

static mp_obj_t mod_cmm2_basic(mp_obj_t statement) {
    cmm2_basic(mp_obj_str_get_str(statement));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_cmm2_basic_obj, mod_cmm2_basic);

static mp_obj_t mod_cmm2_evaluate(mp_obj_t expression) {
    char out[256];
    out[0] = 0;
    cmm2_basic_check(cmm2->basic_eval(mp_obj_str_get_str(expression), out));
    out[sizeof(out) - 1] = 0;
    return mp_obj_new_str_from_cstr(out);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_cmm2_evaluate_obj, mod_cmm2_evaluate);

// cmm2.alarm(op, hour=0, minute=0, second=0): the clock's daily alarm (the loader's rtc_alarm, services version 21):
// op 0 off, 1 on at hour:minute:second every day, 2 has it gone off (1 or 0), 3 its flag cleared. The DS3231's alarm 1
// with OPTION DS3231 ON, else the H743's own RTC's: the ds3231 module's alarm functions use it.
static mp_obj_t mod_cmm2_alarm(size_t n_args, const mp_obj_t *args) {
    if (cmm2->version < 21) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("the alarm needs a newer CMM2 firmware"));
    }
    int a[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < n_args; i++) {
        a[i] = mp_obj_get_int(args[i]);
    }
    int r = cmm2->rtc_alarm(a[0], a[1], a[2], a[3]);
    if (r == -3) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad alarm or time"));
    } else if (r < 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("the clock did not answer"));
    }
    return MP_OBJ_NEW_SMALL_INT(r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_cmm2_alarm_obj, 1, 4, mod_cmm2_alarm);

// the Wi-Fi, for the network module (modules/network.py): the firmware's (OPTION WIFI starts it at power-up)
// cmm2.net_poll() -> 0 no Wi-Fi here, 1 not joined, 2 joined (with an address)
static mp_obj_t mod_cmm2_net_poll(void) {
    if (cmm2->version < 21) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    cmm2_net_used = true;
    return MP_OBJ_NEW_SMALL_INT(cmm2->net_poll());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_cmm2_net_poll_obj, mod_cmm2_net_poll);

// cmm2.net_join(ssid, password) -> 0, or the negative errno (-13 a wrong password, -110 timed out, -113 not joined)
static mp_obj_t mod_cmm2_net_join(mp_obj_t ssid, mp_obj_t password) {
    cmm2_net_check();
    return MP_OBJ_NEW_SMALL_INT(cmm2->net_join(mp_obj_str_get_str(ssid),
        password == mp_const_none ? "" : mp_obj_str_get_str(password)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_cmm2_net_join_obj, mod_cmm2_net_join);

static mp_obj_t mod_cmm2_net_leave(void) {
    cmm2_net_check();
    cmm2->net_leave();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_cmm2_net_leave_obj, mod_cmm2_net_leave);

// cmm2.ifconfig() -> (address, mask, gateway, DNS server), as dotted strings
static mp_obj_t mod_cmm2_ifconfig(void) {
    cmm2_net_check();
    uint8_t a[16];
    cmm2->net_ifconfig(a);
    mp_obj_t t[4];
    for (int i = 0; i < 4; i++) {
        char s[16];
        snprintf(s, sizeof(s), "%u.%u.%u.%u", a[i * 4], a[i * 4 + 1], a[i * 4 + 2], a[i * 4 + 3]);
        t[i] = mp_obj_new_str_from_cstr(s);
    }
    return mp_obj_new_tuple(4, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_cmm2_ifconfig_obj, mod_cmm2_ifconfig);

static const mp_rom_map_elem_t cmm2_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_net_poll), MP_ROM_PTR(&mod_cmm2_net_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_join), MP_ROM_PTR(&mod_cmm2_net_join_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_leave), MP_ROM_PTR(&mod_cmm2_net_leave_obj) },
    { MP_ROM_QSTR(MP_QSTR_ifconfig), MP_ROM_PTR(&mod_cmm2_ifconfig_obj) },
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_cmm2) },
    { MP_ROM_QSTR(MP_QSTR_basic), MP_ROM_PTR(&mod_cmm2_basic_obj) },
    { MP_ROM_QSTR(MP_QSTR_evaluate), MP_ROM_PTR(&mod_cmm2_evaluate_obj) },
    { MP_ROM_QSTR(MP_QSTR_alarm), MP_ROM_PTR(&mod_cmm2_alarm_obj) },
};
static MP_DEFINE_CONST_DICT(cmm2_module_globals, cmm2_module_globals_table);

const mp_obj_module_t cmm2_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&cmm2_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_cmm2, cmm2_module);
