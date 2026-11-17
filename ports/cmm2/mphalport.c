/*
 * MicroPython for the Colour Maximite 2 G2: the hardware abstraction, on the loader's services (cmm2.h).
 *
 * The console is MMBasic's (the serial port and the screen); its keys come from the serial port and the USB keyboard,
 * which the loader's poll() sees to. They are taken into stdin_ringbuf by the keyboard's pump (kbd_cmm2.c), as the Pico
 * Computer 3's USB pump fills it, with Ctrl-C from break_take(): a KeyboardInterrupt while code runs, and Ctrl-C itself
 * to readline.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/stream.h"
#include "shared/runtime/interrupt_char.h"
#include "shared/timeutils/timeutils.h"
#include "shared/runtime/softtimer.h"
#include "extmod/misc.h"
#include "cmm2.h"
#include "modcmm2.h"

#include "py/ringbuf.h"

extern ringbuf_t stdin_ringbuf;
extern void cmm2_kbd_pump(void);
extern void cmm2_mouse_poll(void);
extern void cmm2_i2s_poll(void);
extern void cmm2_pin_poll(void);

static bool sercon_muted;                   // the serial console's output muted (_sercon, below)

// soft timers (machine.Timer, pcconsole's cursor): their handler is run from the VM's hook, so there is nothing to
// schedule
uint32_t soft_timer_get_ms(void) {
    return mp_hal_ticks_ms();
}

void soft_timer_schedule_at_ms(uint32_t ticks_ms) {
    (void)ticks_ms;
}

void soft_timer_init(void) {
}

// from the VM's hook and while stdin waits: a finished sound write's callback scheduled; then, at most every 2 ms,
// the USB devices seen to, the keys taken, the mouse's clicks, the pins' irq edges and the soft timers
void cmm2_vm_poll(void) {
    static uint64_t last;
    cmm2_i2s_poll();
    uint64_t now = cmm2->micros();
    if (now - last < 2000) {
        return;
    }
    last = now;
    cmm2->poll();
    if (cmm2_net_used) {                    // (the network: the firmware pumps it only so while a program runs)
        cmm2->net_poll();
    }
    cmm2_kbd_pump();
    cmm2_mouse_poll();
    cmm2_pin_poll();
    soft_timer_handler();
}

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        int c = ringbuf_get(&stdin_ringbuf);
        if (c != -1) {
            return c;
        }
        c = mp_os_dupterm_rx_chr();
        if (c >= 0) {
            return c;
        }
        cmm2_vm_poll();
        if (ringbuf_avail(&stdin_ringbuf)) {
            continue;
        }
        mp_handle_pending(true);
        cmm2->delay_us(200);
    }
}

// the console's output: MMBasic's console (the serial port; and the screen while the hdmi module's display is off),
// unless _sercon muted it, and os.dupterm's (pcconsole's on-screen console)
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    mp_uint_t ret = len;
    if (!sercon_muted) {
        for (size_t i = 0; i < len; i++) {
            cmm2->con_putc((unsigned char)str[i]);
        }
    }
    int dupterm_res = mp_os_dupterm_tx_strn(str, len);
    if (dupterm_res >= 0) {
        ret = MIN((mp_uint_t)dupterm_res, ret);
    }
    return ret;
}

// _sercon.mute(True/False) mutes the serial console's output (the REPL's and stdout's), for pcconsole's
// console("screen"); _sercon.mute() reads it. As the Pico Computer 3's sercon.c.
static mp_obj_t sercon_mute(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return mp_obj_new_bool(sercon_muted);
    }
    sercon_muted = mp_obj_is_true(args[0]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sercon_mute_obj, 0, 1, sercon_mute);

static const mp_rom_map_elem_t sercon_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__sercon) },
    { MP_ROM_QSTR(MP_QSTR_mute), MP_ROM_PTR(&sercon_mute_obj) },
};
static MP_DEFINE_CONST_DICT(sercon_module_globals, sercon_module_globals_table);

const mp_obj_module_t sercon_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&sercon_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR__sercon, sercon_module);

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if (poll_flags & MP_STREAM_POLL_RD) {
        cmm2_kbd_pump();
        if (ringbuf_avail(&stdin_ringbuf)) {
            ret |= MP_STREAM_POLL_RD;
        }
    }
    ret |= mp_os_dupterm_poll(poll_flags);
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

// the H743's RNG, which the firmware runs (its RND and noise): its data register read once a word is ready
static uint32_t rng_word(void) {
    volatile uint32_t *rng = (volatile uint32_t *)0x48021800;   // RNG: CR, SR (bit 0 DRDY), DR
    for (int i = 0; i < 1000 && !(rng[1] & 1); i++) {
    }
    return rng[2] ^ (uint32_t)cmm2->micros();
}

uint32_t cmm2_random_seed(void) {
    return rng_word();
}

// random bytes for mbedTLS's entropy (extmod/mbedtls/mbedtls_alt.c) and os.urandom
void mp_hal_get_random(size_t n, void *buf) {
    uint8_t *p = buf;
    while (n) {
        uint32_t w = rng_word();
        for (int i = 0; i < 4 && n; i++, n--) {
            *p++ = w;
            w >>= 8;
        }
    }
}

// native code just written (@micropython.native, viper, asm_thumb): written back from the D-cache to the SDRAM and the
// I-cache emptied, so the M7 fetches what was written (the Cortex-M7's SCB: DCCMVAC by 32-byte line, then ICIALLU)
void *cmm2_commit_exec(void *buf, unsigned int len, void *reloc) {
    (void)reloc;                                // (a loaded .mpy's relocations: done by then, in place)
    uintptr_t a = (uintptr_t)buf & ~(uintptr_t)31, end = (uintptr_t)buf + len;
    __asm volatile ("dsb" ::: "memory");
    for (; a < end; a += 32) {
        *(volatile uint32_t *)0xE000EF68 = a;   // SCB->DCCMVAC
    }
    __asm volatile ("dsb" ::: "memory");
    *(volatile uint32_t *)0xE000EF50 = 0;       // SCB->ICIALLU
    __asm volatile ("dsb\n isb" ::: "memory");
    return buf;
}
