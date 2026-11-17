/*
 * machine.RTC on the Colour Maximite 2: the firmware's clock (MMBasic's DATE$ and TIME$, which keep the H743's RTC and,
 * with OPTION DS3231 ON, the DS3231). RTC().datetime() gives (year, month, day, weekday, hours, minutes, seconds,
 * subseconds) with weekday 0 for Monday, as on rp2; RTC().datetime(tuple) sets it (ntptime.settime() does).
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <stdio.h>
#include "py/runtime.h"
#include "shared/timeutils/timeutils.h"
#include "cmm2.h"
#include "modcmm2.h"

typedef struct _machine_rtc_obj_t {
    mp_obj_base_t base;
} machine_rtc_obj_t;

const mp_obj_type_t machine_rtc_type;
static const machine_rtc_obj_t machine_rtc_obj = {{&machine_rtc_type}};

static mp_obj_t machine_rtc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    return MP_OBJ_FROM_PTR(&machine_rtc_obj);
}

static mp_obj_t machine_rtc_datetime(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        int t[6];
        cmm2->datetime(t);
        mp_obj_t tuple[8] = {
            mp_obj_new_int(t[0]), mp_obj_new_int(t[1]), mp_obj_new_int(t[2]),
            mp_obj_new_int(timeutils_calc_weekday(t[0], t[1], t[2])),
            mp_obj_new_int(t[3]), mp_obj_new_int(t[4]), mp_obj_new_int(t[5]), mp_obj_new_int(0)
        };
        return mp_obj_new_tuple(8, tuple);
    }
    mp_obj_t *items;
    mp_obj_get_array_fixed_n(args[1], 8, &items);
    char s[32];
    snprintf(s, sizeof(s), "DATE$ = \"%02d-%02d-%04d\"", (int)mp_obj_get_int(items[2]), (int)mp_obj_get_int(items[1]),
        (int)mp_obj_get_int(items[0]));
    cmm2_basic(s);
    snprintf(s, sizeof(s), "TIME$ = \"%02d:%02d:%02d\"", (int)mp_obj_get_int(items[4]), (int)mp_obj_get_int(items[5]),
        (int)mp_obj_get_int(items[6]));
    cmm2_basic(s);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_rtc_datetime_obj, 1, 2, machine_rtc_datetime);

static mp_obj_t machine_rtc_init(mp_obj_t self_in, mp_obj_t date) {
    mp_obj_t args[2] = {self_in, date};
    return machine_rtc_datetime(2, args);
}
static MP_DEFINE_CONST_FUN_OBJ_2(machine_rtc_init_obj, machine_rtc_init);

static const mp_rom_map_elem_t machine_rtc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_rtc_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_datetime), MP_ROM_PTR(&machine_rtc_datetime_obj) },
};
static MP_DEFINE_CONST_DICT(machine_rtc_locals_dict, machine_rtc_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_rtc_type,
    MP_QSTR_RTC,
    MP_TYPE_FLAG_NONE,
    make_new, machine_rtc_make_new,
    locals_dict, &machine_rtc_locals_dict
    );
