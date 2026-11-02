/*
 * MicroPython for the Colour Maximite 2 G2: the machine module's port functions (included by extmod/modmachine.c).
 * reset() goes back to MMBasic, freq() is the CMM2's clock; I2S (on the loader's sound stream) and Pin (only a pin's
 * number so far) are machine_i2s_cmm2.c's.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/mphal.h"
#include "cmm2.h"

extern const mp_obj_type_t machine_i2s_type;   // machine_i2s_cmm2.c: I2S on the loader's sound stream, and Pin's number
extern const mp_obj_type_t machine_pin_type;

static void mp_machine_idle(void) {
    mp_event_handle_nowait();
    cmm2->delay_us(100);
}

MP_NORETURN static void mp_machine_reset(void) {
    cmm2->leave(0);                 // back to MMBasic, which puts its screen back
    for (;;) {
    }
}

static mp_int_t mp_machine_reset_cause(void) {
    return 0;
}

static mp_obj_t mp_machine_unique_id(void) {
    return mp_obj_new_bytes((const byte *)"CMM2G2", 6);
}

static mp_obj_t mp_machine_get_freq(void) {
    return mp_obj_new_int_from_uint(cmm2->cpu_hz());
}

static void mp_machine_set_freq(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    mp_raise_NotImplementedError(MP_ERROR_TEXT("the CMM2's clock is MMBasic's"));
}

static void mp_machine_lightsleep(size_t n_args, const mp_obj_t *args) {
    if (n_args > 0) {
        mp_hal_delay_ms(mp_obj_get_int(args[0]));
    }
}

MP_NORETURN static void mp_machine_deepsleep(size_t n_args, const mp_obj_t *args) {
    mp_machine_lightsleep(n_args, args);
    mp_machine_reset();
}
