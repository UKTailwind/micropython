/*
 * machine.Pin on the Colour Maximite 2 G2, by the PicoMite's GP numbers, as the Pico Computer 3 numbers its pins: the
 * loader's I/O services (version 7) set each one up and drive it, on the connector pin the firmware gives that GP
 * (Inc/cmm2img.h has the map). Pin(n) or Pin('GPn'), the same object each time; IN, OUT, OPEN_DRAIN and ANALOG,
 * PULL_UP and PULL_DOWN; value(), on(), off(), toggle(), irq(). An irq's pin is looked at from the VM's hook, every
 * 2 ms, and an edge it wants schedules its handler: there is no interrupt behind it. What a program sets up is put
 * back when MicroPython ends.
 *
 * As on a PC3: GP40-46, the RP2350B's analogue inputs, are the firmware's second names for the CMM2's analogue pins
 * (GP40-42 those of GP26-28, GP43-46 those of GP6-9: External.c), and Pin("LED") is the SD card's activity LED on the
 * front (the loader's led, version 22: card access lights it too, and puts it out after).
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
#define LED_GP 48                               // Pin("LED"): the SD card's activity LED (its Pin object is the 49th)
#define NPIN 48                                 // the connector's pins, 1-46 (the owners are kept by pin)
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
MP_REGISTER_ROOT_POINTER(struct _machine_pin_obj_t *cmm2_pin_obj[49]);

/* The pins' owners. On a PC3 a pin is what its newest user set it up as: Pin(1, Pin.IN) after UART(0) takes GP1 from
   the UART, and UART(0) after that takes it back. The CMM2's firmware refuses a pin that another function holds (-2,
   "in use"), so each pin's owner is kept here, and a claim by another object first releases the old one (a Pin
   unconfigured, a COM port or bus closed, a PWM output stopped), so that the newest user wins as on a PC3. They are
   kept by connector pin (the loader's gp_pin), as GP40 and GP26 are one pin. The UART, I2C, SPI and PWM objects are one
   per port, bus or output, as on rp2 (cmm2_bus_obj): the same bus made twice is the same object and keeps its pins. A
   soft reset releases them all (cmm2_io_reset). */
MP_REGISTER_ROOT_POINTER(mp_obj_t cmm2_gp_owner[48]);
MP_REGISTER_ROOT_POINTER(mp_obj_t cmm2_bus_obj[10]);
static cmm2_release_fn gp_release[NPIN];

void cmm2_gp_unclaim(mp_obj_t owner) {
    for (int p = 0; p < NPIN; p++) {
        if (MP_STATE_PORT(cmm2_gp_owner)[p] == owner) {
            MP_STATE_PORT(cmm2_gp_owner)[p] = MP_OBJ_NULL;
            gp_release[p] = NULL;
        }
    }
}

static void gp_release_owner(int p) {
    mp_obj_t old = MP_STATE_PORT(cmm2_gp_owner)[p];
    cmm2_release_fn release = gp_release[p];
    cmm2_gp_unclaim(old);                       // (all of its pins: a bus closed lets every pin of it go)
    if (release != NULL) {
        release(old);
    }
}

void cmm2_gp_claim(int gp, mp_obj_t owner, cmm2_release_fn release) {
    int p = (gp >= 0 && gp < NGP) ? cmm2->gp_pin(gp) : 0;
    if (p <= 0 || p >= NPIN) {
        return;
    }
    mp_obj_t old = MP_STATE_PORT(cmm2_gp_owner)[p];
    if (old != MP_OBJ_NULL && old != owner) {
        gp_release_owner(p);
    }
    MP_STATE_PORT(cmm2_gp_owner)[p] = owner;
    gp_release[p] = release;
}

// a GP's owner let go of it, if it has one (I2C(0) on GP20/21 falling back to its own pins: machine_i2c_cmm2.c)
void cmm2_gp_release(int gp) {
    int p = (gp >= 0 && gp < NGP) ? cmm2->gp_pin(gp) : 0;
    if (p > 0 && p < NPIN && MP_STATE_PORT(cmm2_gp_owner)[p] != MP_OBJ_NULL) {
        gp_release_owner(p);
    }
}

void cmm2_io_reset(void) {
    for (int p = 0; p < NPIN; p++) {
        if (MP_STATE_PORT(cmm2_gp_owner)[p] != MP_OBJ_NULL) {
            gp_release_owner(p);
        }
    }
    for (int gp = 0; gp <= LED_GP; gp++) {
        MP_STATE_PORT(cmm2_pin_obj)[gp] = NULL;
    }
    for (size_t i = 0; i < MP_ARRAY_SIZE(MP_STATE_PORT(cmm2_bus_obj)); i++) {
        MP_STATE_PORT(cmm2_bus_obj)[i] = MP_OBJ_NULL;
    }
}

// a Pin let its GP go (another object claimed it): unconfigured, its irq off
static void machine_pin_release(mp_obj_t o) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(o);
    if (self->mode > 0) {
        cmm2->pin_setup(self->gp, CMM2_PIN_OFF, CMM2_PULL_NONE);
    }
    self->mode = -1;
    self->pull = 0;
    self->trigger = 0;
    self->handler = mp_const_none;
}

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
        case -2:                                // (MMBasic's own use, now that a claim releases a Python owner)
            if (gp >= 0) {
                mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("GP%d is in use"), gp);
            }
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("its pins are in use"));
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
        if (strcmp(s, "LED") == 0 || strcmp(s, "led") == 0) {
            return LED_GP;
        }
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
        mp_raise_ValueError(MP_ERROR_TEXT("a pin is a GP number, 'GPn' or 'LED'"));
    }
    return mp_obj_get_int(o);
}

// a Pin's level, and set: the LED's through the loader's led (version 22), a GP's through its I/O services
static int pin_get(const machine_pin_obj_t *self) {
    return self->gp == LED_GP ? cmm2->led(-1) : cmm2->pin_read(self->gp);
}

static void pin_set(const machine_pin_obj_t *self, int v) {
    if (self->gp == LED_GP) {
        cmm2->led(v != 0);
    } else {
        cmm2->pin_write(self->gp, v);
    }
}

static void machine_pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    static const char *const modes[] = {"OFF", "IN", "OUT", "OPEN_DRAIN", "ANALOG"};
    if (self->gp == LED_GP) {
        mp_printf(print, "Pin(LED, mode=OUT)");
        return;
    }
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
    if (self->gp == LED_GP) {                   // (an output, nothing to set up)
        if (mode != CMM2_PIN_OUT && args[ARG_mode].u_obj != mp_const_none) {
            mp_raise_ValueError(MP_ERROR_TEXT("the LED is an output"));
        }
        self->mode = CMM2_PIN_OUT;
    } else {
        cmm2_gp_claim(self->gp, MP_OBJ_FROM_PTR(self), machine_pin_release);
        cmm2_io_check(cmm2->pin_setup(self->gp, mode, pull), self->gp);
        self->mode = mode;
        self->pull = mode == CMM2_PIN_IN ? pull : 0;
    }
    if (args[ARG_value].u_obj != mp_const_none) {
        pin_set(self, mp_obj_is_true(args[ARG_value].u_obj));
    }
    return mp_const_none;
}

// a GP's Pin object (or the LED's), the same one each time
static machine_pin_obj_t *machine_pin_of(int gp) {
    if (gp != LED_GP && (gp < 0 || gp >= NGP || cmm2->pin_read(gp) < 0)) {
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
    return self;
}

static mp_obj_t machine_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    machine_pin_obj_t *self = machine_pin_of(machine_pin_get_gp(args[0]));
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
        return MP_OBJ_NEW_SMALL_INT(pin_get(self));
    }
    pin_set(self, mp_obj_is_true(args[0]));
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
    pin_set(MP_OBJ_TO_PTR(self_in), 1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_on_obj, machine_pin_on);

static mp_obj_t machine_pin_off(mp_obj_t self_in) {
    pin_set(MP_OBJ_TO_PTR(self_in), 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_off_obj, machine_pin_off);

static mp_obj_t machine_pin_toggle(mp_obj_t self_in) {
    machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    pin_set(self, !pin_get(self));
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
    if (self->gp == LED_GP) {
        mp_raise_ValueError(MP_ERROR_TEXT("the LED is an output"));
    }
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

// the soft buses' pins (mphalport.h), each held by its Pin object
static void machine_pin_hal_setup(int gp, int mode) {
    machine_pin_obj_t *self = machine_pin_of(gp);
    cmm2_gp_claim(gp, MP_OBJ_FROM_PTR(self), machine_pin_release);
    // (an open-drain line pulled up: SoftI2C's, as the PC3's I2C bus has its pull-ups; services version 22)
    int pull = mode == CMM2_PIN_OD ? CMM2_PULL_UP : CMM2_PULL_NONE;
    cmm2_io_check(cmm2->pin_setup(gp, mode, pull), gp);
    self->mode = mode;
    self->pull = 0;
}
void mp_hal_pin_input(int gp) {
    machine_pin_hal_setup(gp, CMM2_PIN_IN);
}
void mp_hal_pin_output(int gp) {
    machine_pin_hal_setup(gp, CMM2_PIN_OUT);
}
void mp_hal_pin_open_drain(int gp) {
    machine_pin_hal_setup(gp, CMM2_PIN_OD);
}
