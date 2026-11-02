/*
 * MicroPython for the Colour Maximite 2 G2: the hardware abstraction, on the loader's services (mphalport.c).
 *
 * The MIT License (MIT), as MicroPython.
 */
#ifndef MICROPY_INCLUDED_CMM2_MPHALPORT_H
#define MICROPY_INCLUDED_CMM2_MPHALPORT_H

#include "py/mpconfig.h"
#include "shared/runtime/interrupt_char.h"

mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
uint64_t mp_hal_time_ns(void);
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);

#endif
