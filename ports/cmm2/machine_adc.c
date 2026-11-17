/*
 * machine.ADC's port part (included by extmod/machine_adc.c): an analog input on the CMM2 through the loader's I/O
 * services. ADC(n), a channel 0-6, is GP40+n, as on the PC3's RP2350B (the firmware's second names: GP40-42 the pins
 * of GP26-28, an RP2040's channels 0-2, and GP43-46 those of GP6-9); ADC(n) with n of 8 up, ADC(Pin(n)) or ADC('GPn')
 * is that GP, if the CMM2's pin for it is analog. read_u16() is 0-65535, read_uv() the microvolts (from VCC: 3.3 V, or
 * OPTION VCC's). SETPIN AIN's 16 bits.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "cmm2.h"
#include "cmm2io.h"

#define MICROPY_PY_MACHINE_ADC_CLASS_CONSTANTS                 // (none)

typedef struct _machine_adc_obj_t {
    mp_obj_base_t base;
    uint8_t gp;
} machine_adc_obj_t;

static void mp_machine_adc_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "ADC(GP%u)", self->gp);
}

// another object took its GP (cmm2io.h): the analog input unconfigured
static void mp_machine_adc_release(mp_obj_t o) {
    cmm2->pin_setup(((machine_adc_obj_t *)MP_OBJ_TO_PTR(o))->gp, CMM2_PIN_OFF, CMM2_PULL_NONE);
}

static mp_obj_t mp_machine_adc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    int gp;
    if (mp_obj_is_int(args[0])) {
        gp = mp_obj_get_int(args[0]);
        if (gp >= 0 && gp < 8) {
            gp += 40;                           // (a channel: GP40 up, as an RP2350B's)
        }
    } else {
        gp = machine_pin_get_gp(args[0]);
    }
    machine_adc_obj_t *self = mp_obj_malloc(machine_adc_obj_t, &machine_adc_type);
    self->gp = gp;
    cmm2_gp_claim(gp, MP_OBJ_FROM_PTR(self), mp_machine_adc_release);   // (the pin taken from whatever had it)
    cmm2_io_check(cmm2->pin_setup(gp, CMM2_PIN_ANALOG, CMM2_PULL_NONE), gp);
    return MP_OBJ_FROM_PTR(self);
}

static mp_int_t mp_machine_adc_read_u16(machine_adc_obj_t *self) {
    return cmm2_io_check(cmm2->adc_read(self->gp, NULL), self->gp);
}

static mp_int_t mp_machine_adc_read_uv(machine_adc_obj_t *self) {
    uint32_t uv;
    cmm2_io_check(cmm2->adc_read(self->gp, &uv), self->gp);
    return uv;
}
