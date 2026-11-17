/*
 * mbedTLS for MicroPython's tls module on the Colour Maximite 2: rp2's settings (ports/rp2/mbedtls), with the CMM2's
 * clock for certificates' dates (mbedtls_port.c) and the H743's RNG for entropy (mphalport.c's mp_hal_get_random,
 * which extmod/mbedtls/mbedtls_alt.c's mbedtls_hardware_poll calls).
 *
 * The MIT License (MIT), as MicroPython.
 */
#ifndef MICROPY_INCLUDED_MBEDTLS_CONFIG_H
#define MICROPY_INCLUDED_MBEDTLS_CONFIG_H

#define MBEDTLS_ECP_NIST_OPTIM

#include <time.h>
time_t cmm2_rtctime_seconds(time_t *timer);
#define MBEDTLS_PLATFORM_TIME_MACRO cmm2_rtctime_seconds
#define MBEDTLS_PLATFORM_MS_TIME_ALT mbedtls_ms_time

#define MICROPY_MBEDTLS_CONFIG_BARE_METAL (1)

#include "extmod/mbedtls/mbedtls_config_common.h"

#endif
