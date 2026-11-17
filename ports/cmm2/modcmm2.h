/*
 * The cmm2 module's C side (modcmm2.c): one MMBasic statement run by the firmware, its error raised as Python's.
 *
 * The MIT License (MIT), as MicroPython.
 */
#ifndef MICROPY_INCLUDED_CMM2_MODCMM2_H
#define MICROPY_INCLUDED_CMM2_MODCMM2_H

void cmm2_basic(const char *statement);

// machine.WDT's timeout in ms, 0 if it was never started (machine_wdt.c); machine.reset() stops it
extern int cmm2_wdt_ms;

// the network (modsocket_cmm2.c): used since a socket or the network module was first used, so the VM's hook pumps it
// (net_poll); check: the services there (version 21), else OSError
#include <stdbool.h>
extern bool cmm2_net_used;
void cmm2_net_check(void);

#endif
