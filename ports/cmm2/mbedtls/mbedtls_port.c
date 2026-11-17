/*
 * mbedTLS's clock on the Colour Maximite 2 (mbedtls_config_port.h): the firmware's date and time, in seconds since
 * 1970, for certificates' dates; and its milliseconds.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/mpconfig.h"

#ifdef MICROPY_SSL_MBEDTLS

#include "mbedtls_config_port.h"
#include "shared/timeutils/timeutils.h"
#include "mbedtls/platform_time.h"
#include "cmm2.h"

time_t cmm2_rtctime_seconds(time_t *timer) {
    int t[6];
    cmm2->datetime(t);
    time_t s = (time_t)timeutils_seconds_since_1970(t[0], t[1], t[2], t[3], t[4], t[5]);
    if (timer) {
        *timer = s;
    }
    return s;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)cmm2_rtctime_seconds(NULL) * 1000;
}

#endif
