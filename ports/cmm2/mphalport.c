/*
 * MicroPython for the Colour Maximite 2 G2: the hardware abstraction, on the loader's services (cmm2.h).
 *
 * The console is MMBasic's (the serial port and the screen); its keys come from the serial port and the USB keyboard,
 * which the loader's poll() sees to. Ctrl-C never reaches the console's queue (the firmware takes it as its break key):
 * the loader's break_take() reports it, as a KeyboardInterrupt while code runs and as Ctrl-C itself to readline.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/stream.h"
#include "shared/runtime/interrupt_char.h"
#include "shared/timeutils/timeutils.h"
#include "cmm2.h"

#define CHAR_CTRL_C (3)

static int stdin_peek = -1;                 // a key read by a poll of stdin, not yet taken

// the USB devices seen to, and Ctrl-C made a KeyboardInterrupt: from the VM's hook, at most every 2 ms
void cmm2_vm_poll(void) {
    static uint64_t last;
    uint64_t now = cmm2->micros();
    if (now - last < 2000) {
        return;
    }
    last = now;
    cmm2->poll();
    if (cmm2->break_take() && mp_interrupt_char != -1) {
        mp_sched_keyboard_interrupt();
    }
}

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        int c;
        if (stdin_peek >= 0) {
            c = stdin_peek;
            stdin_peek = -1;
            return c;
        }
        if ((c = cmm2->con_getc()) >= 0) {
            return c;
        }
        cmm2->poll();
        if (cmm2->break_take()) {
            return CHAR_CTRL_C;
        }
        mp_handle_pending(true);
        cmm2->delay_us(200);
    }
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        cmm2->con_putc((unsigned char)str[i]);
    }
    return len;
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if (poll_flags & MP_STREAM_POLL_RD) {
        if (stdin_peek < 0) {
            stdin_peek = cmm2->con_getc();
        }
        if (stdin_peek >= 0) {
            ret |= MP_STREAM_POLL_RD;
        }
    }
    if (poll_flags & MP_STREAM_POLL_WR) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(cmm2->micros() / 1000);
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)cmm2->micros();
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return (mp_uint_t)cmm2->micros();
}

// the CMM2's clock (MMBasic's DATE$ and TIME$), in nanoseconds since MicroPython's epoch
uint64_t mp_hal_time_ns(void) {
    int t[6];
    cmm2->datetime(t);
    return (uint64_t)timeutils_seconds_since_epoch(t[0], t[1], t[2], t[3], t[4], t[5]) * 1000000000ULL;
}

void mp_hal_delay_us(mp_uint_t us) {
    cmm2->delay_us(us);
}

void mp_hal_delay_ms(mp_uint_t ms) {
    uint64_t end = cmm2->micros() + (uint64_t)ms * 1000;
    for (;;) {
        mp_event_handle_nowait();
        uint64_t now = cmm2->micros();
        if (now >= end) {
            break;
        }
        cmm2->delay_us((end - now) > 500 ? 500 : (uint32_t)(end - now));
    }
}

uint32_t cmm2_random_seed(void) {
    return (uint32_t)cmm2->micros() * 2654435761u;
}
