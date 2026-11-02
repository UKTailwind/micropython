/*
 * The I/O classes' shared pieces (machine_pin_cmm2.c): the loader's I/O services (version 7) by the PicoMite's GP
 * numbers, and their errors as Python's.
 *
 * The MIT License (MIT), as MicroPython.
 */
#ifndef MICROPY_INCLUDED_CMM2_CMM2IO_H
#define MICROPY_INCLUDED_CMM2_CMM2IO_H

#include "py/obj.h"

// a service's result: returned if 0 or more, else raised (-1 no such GP or bus, -2 in use, -3 not possible, -4 the
// firmware's error, with its message); gp for the message, or -1
mp_int_t cmm2_io_check(mp_int_t r, int gp);
// a GP number from a Pin, an int or 'GPn'
int machine_pin_get_gp(mp_obj_t o);
// the pins with an irq handler looked at (the VM's hook)
void cmm2_pin_poll(void);

#endif
