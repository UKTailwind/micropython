/*
 * machine.PWM's port part (included by extmod/machine_pwm.c): the CMM2's PWM through the loader's I/O services. GP2-5
 * are PWM 1A, 1B, 2A and 2B (the PicoMite's slices 1 and 2: GP2/3 share a frequency, as do GP4/5); 1 Hz to 20 MHz,
 * the duty to a hundredth of a percent (MMBasic's PWM command).
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "cmm2.h"
#include "cmm2io.h"

typedef struct _machine_pwm_obj_t {
    mp_obj_base_t base;
    uint8_t gp;
    uint8_t running;
    uint32_t freq;
    uint32_t duty;                              // 0-65535
} machine_pwm_obj_t;

// another object took its GP (cmm2io.h): the output stopped; setting freq or duty starts it again
static void pwm_release(mp_obj_t o) {
    machine_pwm_obj_t *self = MP_OBJ_TO_PTR(o);
    if (self->running) {
        cmm2->pwm_stop(self->gp);
        self->running = 0;
    }
}

static void pwm_apply(machine_pwm_obj_t *self) {
    if (self->freq) {
        cmm2_gp_claim(self->gp, MP_OBJ_FROM_PTR(self), pwm_release);
        cmm2_io_check(cmm2->pwm_set(self->gp, self->freq, self->duty), self->gp);
        self->running = 1;
    }
}

static void mp_machine_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<PWM GP%u freq=%u duty_u16=%u>", self->gp, (unsigned)self->freq, (unsigned)self->duty);
}

// init(*, freq, duty_u16, duty_ns)
static void mp_machine_pwm_init_helper(machine_pwm_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_freq, ARG_duty_u16, ARG_duty_ns };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty_u16, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_duty_ns, MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    if (args[ARG_freq].u_int > 0) {
        self->freq = args[ARG_freq].u_int;
    }
    if (args[ARG_duty_u16].u_int >= 0) {
        self->duty = args[ARG_duty_u16].u_int > 65535 ? 65535 : args[ARG_duty_u16].u_int;
    } else if (args[ARG_duty_ns].u_int >= 0 && self->freq) {
        uint64_t d = (uint64_t)args[ARG_duty_ns].u_int * self->freq * 65535 / 1000000000;
        self->duty = d > 65535 ? 65535 : (uint32_t)d;
    }
    pwm_apply(self);
}

// PWM(pin, *, freq, duty_u16, duty_ns)
static mp_obj_t mp_machine_pwm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    int gp = machine_pin_get_gp(args[0]);
    if (gp < 2 || gp > 5) {
        mp_raise_ValueError(MP_ERROR_TEXT("the CMM2's PWM is on GP2-5"));
    }
    // one object per output, as on rp2: PWM(Pin(2)) made again is the same output, its settings kept
    machine_pwm_obj_t *self = MP_OBJ_TO_PTR(MP_STATE_PORT(cmm2_bus_obj)[CMM2_BUS_PWM(gp)]);
    if (self == NULL) {
        self = mp_obj_malloc(machine_pwm_obj_t, &machine_pwm_type);
        self->gp = gp;
        self->running = 0;
        self->freq = 0;
        self->duty = 0;
        MP_STATE_PORT(cmm2_bus_obj)[CMM2_BUS_PWM(gp)] = MP_OBJ_FROM_PTR(self);
    }
    cmm2_gp_claim(gp, MP_OBJ_FROM_PTR(self), pwm_release);   // (the pin is the PWM's from now, as on rp2)
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_machine_pwm_init_helper(self, n_args - 1, args + 1, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_pwm_deinit(machine_pwm_obj_t *self) {
    pwm_release(MP_OBJ_FROM_PTR(self));
    cmm2_gp_unclaim(MP_OBJ_FROM_PTR(self));
}

static mp_obj_t mp_machine_pwm_freq_get(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(self->freq);
}

static void mp_machine_pwm_freq_set(machine_pwm_obj_t *self, mp_int_t freq) {
    if (freq < 1 || freq > 20000000) {
        mp_raise_ValueError(MP_ERROR_TEXT("freq is 1 Hz to 20 MHz"));
    }
    self->freq = freq;
    pwm_apply(self);
}

static mp_obj_t mp_machine_pwm_duty_get_u16(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(self->duty);
}

static void mp_machine_pwm_duty_set_u16(machine_pwm_obj_t *self, mp_int_t duty_u16) {
    self->duty = duty_u16 < 0 ? 0 : duty_u16 > 65535 ? 65535 : duty_u16;
    pwm_apply(self);
}

static mp_obj_t mp_machine_pwm_duty_get_ns(machine_pwm_obj_t *self) {
    return mp_obj_new_int_from_uint(self->freq ? (uint64_t)self->duty * 1000000000 / 65535 / self->freq : 0);
}

static void mp_machine_pwm_duty_set_ns(machine_pwm_obj_t *self, mp_int_t duty_ns) {
    if (self->freq) {
        uint64_t d = (uint64_t)(duty_ns < 0 ? 0 : duty_ns) * self->freq * 65535 / 1000000000;
        self->duty = d > 65535 ? 65535 : (uint32_t)d;
    }
    pwm_apply(self);
}
