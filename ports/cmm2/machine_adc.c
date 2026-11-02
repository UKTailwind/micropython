/*
 * machine.ADC's port part (included by extmod/machine_adc.c): an analog input on the CMM2 through the loader's I/O
 * services. ADC(0) to ADC(2) are GP26-28, the PicoMite's analog inputs (an RP2040's channels 0-2); ADC(n) with n of
 * 26 up, ADC(Pin(n)) or ADC('GPn') is that GP, if the CMM2's pin for it is analog. read_u16() is 0-65535, read_uv()
 * the microvolts (from VCC: 3.3 V, or OPTION VCC's). SETPIN AIN's 16 bits.
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

static mp_obj_t mp_machine_adc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    int gp;
    if (mp_obj_is_int(args[0])) {
        gp = mp_obj_get_int(args[0]);
        if (gp >= 0 && gp < 8) {
            gp += 26;                           // (a channel: GP26 up)
        }
    } else {
        gp = machine_pin_get_gp(args[0]);
    }
    cmm2_io_check(cmm2->pin_setup(gp, CMM2_PIN_ANALOG, CMM2_PULL_NONE), gp);
    machine_adc_obj_t *self = mp_obj_malloc(machine_adc_obj_t, &machine_adc_type);
    self->gp = gp;
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
