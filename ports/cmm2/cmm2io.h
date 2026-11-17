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

// The GPs' owners (machine_pin_cmm2.c): an object claims each GP it sets up, before it sets it up; another object's
// claim first calls the old owner's release (which must not raise: it closes its port or bus with the service's own
// call, no check) and takes away all of its GPs. unclaim: an owner let its GPs go (deinit). reset: every owner
// released and the tables emptied, at a soft reset.
typedef void (*cmm2_release_fn)(mp_obj_t owner);
void cmm2_gp_claim(int gp, mp_obj_t owner, cmm2_release_fn release);
void cmm2_gp_unclaim(mp_obj_t owner);
void cmm2_gp_release(int gp);                   // a GP's owner (if any) released, as another's claim would
void cmm2_io_reset(void);

// the one object of each port, bus and PWM output (MP_STATE_PORT(cmm2_bus_obj)[...]), as rp2 has them
#define CMM2_BUS_UART(n)  (n)                  // UART(0), UART(1)
#define CMM2_BUS_I2C(n)   (2 + (n))            // I2C(0), I2C(1)
#define CMM2_BUS_SPI(n)   (4 + (n))            // SPI(0), SPI(1)
#define CMM2_BUS_PWM(gp)  (6 + (gp) - 2)       // PWM on GP2-5

#endif
