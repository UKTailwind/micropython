/*
 * MicroPython for the Colour Maximite 2 G2: the time module's port functions (included by extmod/modtime.c), on the
 * CMM2's clock (MMBasic's DATE$ and TIME$).
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/obj.h"
#include "shared/timeutils/timeutils.h"
#include "cmm2.h"

static void mp_time_localtime_get(timeutils_struct_time_t *tm) {
    int t[6];
    cmm2->datetime(t);
    tm->tm_year = t[0];
    tm->tm_mon = t[1];
    tm->tm_mday = t[2];
    tm->tm_hour = t[3];
    tm->tm_min = t[4];
    tm->tm_sec = t[5];
    tm->tm_wday = timeutils_calc_weekday(t[0], t[1], t[2]);
    tm->tm_yday = timeutils_year_day(t[0], t[1], t[2]);
}

static mp_obj_t mp_time_time_get(void) {
    int t[6];
    cmm2->datetime(t);
    return mp_obj_new_int_from_uint(timeutils_seconds_since_epoch(t[0], t[1], t[2], t[3], t[4], t[5]));
}
