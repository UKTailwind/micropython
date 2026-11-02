/*
 * MicroPython for the Colour Maximite 2 G2: the hardware abstraction, on the loader's services (mphalport.c).
 *
 * The MIT License (MIT), as MicroPython.
 */
#ifndef MICROPY_INCLUDED_CMM2_MPHALPORT_H
#define MICROPY_INCLUDED_CMM2_MPHALPORT_H

#include "py/mpconfig.h"
#include "py/obj.h"
#include "shared/runtime/interrupt_char.h"

mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
uint64_t mp_hal_time_ns(void);
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);

// the pins of SoftI2C, SoftSPI and time_pulse_us: GP numbers, through the loader's I/O services (machine_pin_cmm2.c)
#include "cmm2.h"
#define mp_hal_pin_obj_t int
#define mp_hal_get_pin_obj(o) machine_pin_get_gp(o)
#define mp_hal_pin_name(p) (p)
#define MP_HAL_PIN_FMT "GP%u"
#define mp_hal_pin_read(p) (cmm2->pin_read(p))
#define mp_hal_pin_write(p, v) (cmm2->pin_write((p), (v)))
#define mp_hal_pin_od_low(p) (cmm2->pin_write((p), 0))
#define mp_hal_pin_od_high(p) (cmm2->pin_write((p), 1))
#define mp_hal_delay_us_fast(us) mp_hal_delay_us(us)
int machine_pin_get_gp(mp_obj_t o);
void mp_hal_pin_input(int gp);
void mp_hal_pin_output(int gp);
void mp_hal_pin_open_drain(int gp);

#endif
