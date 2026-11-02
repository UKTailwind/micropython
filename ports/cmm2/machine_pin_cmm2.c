/*
 * machine.Pin on the Colour Maximite 2 G2, by the PicoMite's GP numbers, as the Pico Computer 3 numbers its pins: the
 * loader's I/O services (version 7) set each one up and drive it, on the connector pin the firmware gives that GP
 * (Inc/cmm2img.h has the map). Pin(n) or Pin('GPn'), the same object each time; IN, OUT, OPEN_DRAIN and ANALOG,
 * PULL_UP and PULL_DOWN; value(), on(), off(), toggle(), irq(). An irq's pin is looked at from the VM's hook, every
 * 2 ms, and an edge it wants schedules its handler: there is no interrupt behind it. What a program sets up is put
 * back when MicroPython ends.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "cmm2.h"
#include "cmm2io.h"

#define NGP 48
#define IRQ_FALLING 4                           // (rp2's values)
#define IRQ_RISING  8

typedef struct _machine_pin_obj_t {
    mp_obj_base_t base;
    uint8_t gp;
    int8_t mode;                                // CMM2_PIN_*, -1 not set up yet
    int8_t pull;
    uint8_t trigger;                            // the irq's edges
    uint8_t last;                               // the level the irq poll last saw
    mp_obj_t handler;
} machine_pin_obj_t;

extern const mp_obj_type_t machine_pin_type;
MP_REGISTER_ROOT_POINTER(struct _machine_pin_obj_t *cmm2_pin_obj[48]);

mp_int_t cmm2_io_check(mp_int_t r, int gp) {
    if (r >= 0) {
        return r;
    }
    switch (r) {
        case -1:
            if (gp >= 0) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("GP%d is not on the CMM2"), gp);
            }
            mp_raise_ValueError(MP_ERROR_TEXT("no such bus"));
        case -2:
            mp_raise_OSError(MP_EBUSY);
        case -3:
            if (gp >= 0) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("not possible on GP%d"), gp);
            }
            mp_raise_ValueError(MP_ERROR_TEXT("not possible (not open?)"));
        default:
            mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), cmm2->io_error());
    }
}

int machine_pin_get_gp(mp_obj_t o) {
    if (mp_obj_is_type(o, &machine_pin_type)) {
        return ((machine_pin_obj_t *)MP_OBJ_TO_PTR(o))->gp;
    }
    if (mp_obj_is_str(o)) {
        const char *s = mp_obj_str_get_str(o);
        if ((s[0] == 'G' || s[0] == 'g') && (s[1] == 'P' || s[1] == 'p') && s[2] >= '0' && s[2] <= '9') {
            int n = s[2] - '0';
            if (s[3] >= '0' && s[3] <= '9' && !s[4]) {
                n = n * 10 + s[3] - '0';
            } else if (s[3]) {
                n = -1;
            }
            if (n >= 0) {
                return n;
            }
        }
        mp_raise_ValueError(MP_ERROR_TEXT("a pin is a GP number or 'GPn'"));
    }
    return mp_obj_get_int(o);
}

static void machine_pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    static const char *const modes[] = {"OFF", "IN", "OUT", "OPEN_DRAIN", "ANALOG"};
    mp_printf(print, "Pin(GP%u", self->gp);
    if (self->mode >= 0) {
        mp_printf(print, ", mode=%s", modes[self->mode]);
        if (self->pull) {
            mp_printf(print, ", pull=%s", self->pull == CMM2_PULL_UP ? "PULL_UP" : "PULL_DOWN");
        }
    }
    mp_print_str(print, ")");
}

// init(mode=-1, pull=-1, *, value=None)
static mp_obj_t machine_pin_obj_init_helper(machine_pin_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_pull, ARG_value };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_pull, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_value, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    int mode = args[ARG_mode].u_obj == mp_const_none ? (self->mode >= 0 ? self->mode : CMM2_PIN_IN)
        : mp_obj_get_int(args[ARG_mode].u_obj);
    int pull = args[ARG_pull].u_obj == mp_const_none ? CMM2_PULL_NONE : mp_obj_get_int(args[ARG_pull].u_obj);
    cmm2_io_check(cmm2->pin_setup(self->gp, mode, pull), self->gp);
    self->mode = mode;
    self->pull = mode == CMM2_PIN_IN ? pull : 0;
    if (args[ARG_value].u_obj != mp_const_none) {
        cmm2->pin_write(self->gp, mp_obj_is_true(args[ARG_value].u_obj));
    }
    return mp_const_none;
}

static mp_obj_t machine_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    int gp = machine_pin_get_gp(args[0]);
    if (gp < 0 || gp >= NGP || cmm2->pin_read(gp) < 0) {
        cmm2_io_check(-1, gp);
    }
    machine_pin_obj_t *self = MP_STATE_PORT(cmm2_pin_obj)[gp];
    if (self == NULL) {
        self = mp_obj_malloc(machine_pin_obj_t, &machine_pin_type);
        self->gp = gp;
        self->mode = -1;
        self->pull = 0;
        self->trigger = 0;
        self->handler = mp_const_none;
        MP_STATE_PORT(cmm2_pin_obj)[gp] = self;
    }
    if (n_args > 1 || n_kw > 0) {
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        machine_pin_obj_init_helper(self, n_args - 1, args + 1, &kw_args);
    }
    return MP_OBJ_FROM_PTR(self);
}

// pin([value])
static mp_obj_t machine_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (n_args == 0) {
        return MP_OBJ_NEW_SMALL_INT(cmm2->pin_read(self->gp));
    }
    cmm2->pin_write(self->gp, mp_obj_is_true(args[0]));
    return mp_const_none;
}

static mp_obj_t machine_pin_obj_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return machine_pin_obj_init_helper(MP_OBJ_TO_PTR(args[0]), n_args - 1, args + 1, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_init_obj, 1, machine_pin_obj_init);

static mp_obj_t machine_pin_value(size_t n_args, const mp_obj_t *args) {
    return machine_pin_call(args[0], n_args - 1, 0, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_value_obj, 1, 2, machine_pin_value);

static mp_obj_t machine_pin_on(mp_obj_t self_in) {
    cmm2->pin_write(((machine_pin_obj_t *)MP_OBJ_TO_PTR(self_in))->gp, 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_on_obj, machine_pin_on);

static mp_obj_t machine_pin_off(mp_obj_t self_in) {
    cmm2->pin_write(((machine_pin_obj_t *)MP_OBJ_TO_PTR(self_in))->gp, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_off_obj, machine_pin_off);

static mp_obj_t machine_pin_toggle(mp_obj_t self_in) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    cmm2->pin_write(self->gp, !cmm2->pin_read(self->gp));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_toggle_obj, machine_pin_toggle);

// irq(handler=None, trigger=IRQ_FALLING | IRQ_RISING, hard=False): polled, not an interrupt (see above)
static mp_obj_t machine_pin_irq(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_handler, ARG_trigger, ARG_hard };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_handler, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_trigger, MP_ARG_INT, {.u_int = IRQ_FALLING | IRQ_RISING} },
        { MP_QSTR_hard, MP_ARG_BOOL, {.u_bool = false} },
    };
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    self->trigger = 0;
    self->last = cmm2->pin_read(self->gp);
    self->handler = args[ARG_handler].u_obj;
    if (self->handler != mp_const_none) {
        self->trigger = args[ARG_trigger].u_int & (IRQ_FALLING | IRQ_RISING);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_irq_obj, 1, machine_pin_irq);

void cmm2_pin_poll(void) {
    for (int gp = 0; gp < NGP; gp++) {
        machine_pin_obj_t *p = MP_STATE_PORT(cmm2_pin_obj)[gp];
        if (p == NULL || !p->trigger) {
            continue;
        }
        int v = cmm2->pin_read(gp);
        if (v != p->last) {
            p->last = v;
            if (p->trigger & (v ? IRQ_RISING : IRQ_FALLING)) {
                mp_sched_schedule(p->handler, MP_OBJ_FROM_PTR(p));
            }
        }
    }
}

static const mp_rom_map_elem_t machine_pin_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&machine_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&machine_pin_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&machine_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_high), MP_ROM_PTR(&machine_pin_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_low), MP_ROM_PTR(&machine_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_toggle), MP_ROM_PTR(&machine_pin_toggle_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_pin_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_IN), MP_ROM_INT(CMM2_PIN_IN) },
    { MP_ROM_QSTR(MP_QSTR_OUT), MP_ROM_INT(CMM2_PIN_OUT) },
    { MP_ROM_QSTR(MP_QSTR_OPEN_DRAIN), MP_ROM_INT(CMM2_PIN_OD) },
    { MP_ROM_QSTR(MP_QSTR_ANALOG), MP_ROM_INT(CMM2_PIN_ANALOG) },
    { MP_ROM_QSTR(MP_QSTR_PULL_UP), MP_ROM_INT(CMM2_PULL_UP) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(CMM2_PULL_DOWN) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(IRQ_FALLING) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING), MP_ROM_INT(IRQ_RISING) },
};
static MP_DEFINE_CONST_DICT(machine_pin_locals_dict, machine_pin_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_pin_type,
    MP_QSTR_Pin,
    MP_TYPE_FLAG_NONE,
    make_new, machine_pin_make_new,
    print, machine_pin_print,
    call, machine_pin_call,
    locals_dict, &machine_pin_locals_dict
    );

// the soft buses' pins (mphalport.h)
void mp_hal_pin_input(int gp) {
    cmm2_io_check(cmm2->pin_setup(gp, CMM2_PIN_IN, CMM2_PULL_NONE), gp);
}
void mp_hal_pin_output(int gp) {
    cmm2_io_check(cmm2->pin_setup(gp, CMM2_PIN_OUT, CMM2_PULL_NONE), gp);
}
void mp_hal_pin_open_drain(int gp) {
    cmm2_io_check(cmm2->pin_setup(gp, CMM2_PIN_OD, CMM2_PULL_NONE), gp);
}
