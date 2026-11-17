/*
 * machine.I2C on the CMM2: its I2C buses through the loader's I/O services, with extmod's I2C methods over them.
 * I2C(0) is MMBasic's I2C on GP16/17, I2C(1) its I2C2 on GP14/15, as the PicoMite's GP map puts them; freq is taken
 * to MMBasic's 100, 400 or 1000 kHz, and timeout (us, as rp2's) to its ms (100 up). A transfer that keeps the bus (no
 * stop: writeto(..., False), readfrom_mem's address) is followed by a repeated start, as I2C WRITE's option 1.
 * Given other pins (scl and sda together), I2C(n) is a software I2C on them (extmod's SoftI2C, the lines open-drain
 * with the pins' pull-ups on): the PC3's I2C(0) on GP20/21, which the H743's I2C cannot reach, with the same methods.
 * Where MMBasic holds one of them itself (GP21, connector pin 33, is the Wi-Fi module's on a CMM2 with one), it is the
 * bus's own pins after all: pins 3 and 5 for I2C(0).
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/runtime.h"
#include "py/mperrno.h"
#include "extmod/modmachine.h"
#include "cmm2.h"
#include "cmm2io.h"

typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    uint8_t bus;
    bool open;
    uint32_t khz;
    uint32_t timeout_ms;
} machine_i2c_obj_t;

// another object took SDA or SCL (cmm2io.h): the bus closed; I2C(n) made again opens it
static void machine_i2c_release(mp_obj_t o) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(o);
    if (self->open) {
        cmm2->i2c_close(self->bus);
        self->open = false;
    }
}

static void machine_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C(%u, freq=%u, scl=GP%u, sda=GP%u, timeout=%u)", self->bus, (unsigned)self->khz * 1000,
        self->bus ? 15 : 17, self->bus ? 14 : 16, (unsigned)self->timeout_ms * 1000);
}

// I2C(id=0, *, freq=400000, scl, sda, timeout=1000000): scl and sda, if given, are the bus's own
static mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_freq, ARG_scl, ARG_sda, ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = 400000} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000000} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    int bus = args[ARG_id].u_int;
    if (bus < 0 || bus > 1) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("I2C(%d) doesn't exist"), bus);
    }
    mp_obj_t scl = args[ARG_scl].u_obj, sda = args[ARG_sda].u_obj;
    if ((scl != mp_const_none && machine_pin_get_gp(scl) != (bus ? 15 : 17))
        || (sda != mp_const_none && machine_pin_get_gp(sda) != (bus ? 14 : 16))) {
        // not the bus's own pins: a software I2C on the ones given (the PC3's I2C(0, sda=Pin(20), scl=Pin(21)))
        if (scl == mp_const_none || sda == mp_const_none) {
            mp_raise_ValueError(MP_ERROR_TEXT("scl and sda: both, on other pins"));
        }
        mp_obj_t kw[8] = {
            MP_OBJ_NEW_QSTR(MP_QSTR_scl), scl, MP_OBJ_NEW_QSTR(MP_QSTR_sda), sda,
            MP_OBJ_NEW_QSTR(MP_QSTR_freq), mp_obj_new_int(args[ARG_freq].u_int),
            MP_OBJ_NEW_QSTR(MP_QSTR_timeout), mp_obj_new_int(args[ARG_timeout].u_int),
        };
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_obj_t soft = MP_OBJ_TYPE_GET_SLOT(&mp_machine_soft_i2c_type, make_new)(&mp_machine_soft_i2c_type, 0, 4,
                kw);
            nlr_pop();
            return soft;
        }
        // A pin MMBasic holds itself (OSError, "in use"): GP21 is the Wi-Fi module's on a CMM2 with one. Then the bus's
        // own pins, pins 3 and 5 for I2C(0), as the rider says (Peter, 17 Nov 2026); what the try set up let go.
        if (!mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(((mp_obj_base_t *)nlr.ret_val)->type),
            MP_OBJ_FROM_PTR(&mp_type_OSError))) {
            nlr_jump(nlr.ret_val);
        }
        cmm2_gp_release(machine_pin_get_gp(scl));
        cmm2_gp_release(machine_pin_get_gp(sda));
    }
    mp_int_t f = args[ARG_freq].u_int;
    uint32_t t = args[ARG_timeout].u_int / 1000;
    // one object per bus, as on rp2: I2C(0) made again (a library's own, say) is the same bus, opened afresh
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(MP_STATE_PORT(cmm2_bus_obj)[CMM2_BUS_I2C(bus)]);
    if (self == NULL) {
        self = mp_obj_malloc(machine_i2c_obj_t, &machine_i2c_type);
        self->bus = bus;
        self->open = false;
        MP_STATE_PORT(cmm2_bus_obj)[CMM2_BUS_I2C(bus)] = MP_OBJ_FROM_PTR(self);
    }
    if (self->open) {
        cmm2->i2c_close(bus);
        self->open = false;
    }
    self->khz = f <= 100000 ? 100 : f <= 400000 ? 400 : 1000;
    self->timeout_ms = t < 100 ? 100 : t;
    cmm2_gp_claim(bus ? 14 : 16, MP_OBJ_FROM_PTR(self), machine_i2c_release);
    cmm2_gp_claim(bus ? 15 : 17, MP_OBJ_FROM_PTR(self), machine_i2c_release);
    cmm2_io_check(cmm2->i2c_open(bus, self->khz, self->timeout_ms), -1);
    self->open = true;
    return MP_OBJ_FROM_PTR(self);
}

static int machine_i2c_transfer_single(mp_obj_base_t *self_in, uint16_t addr, size_t len, uint8_t *buf, unsigned int flags) {
    machine_i2c_obj_t *self = (machine_i2c_obj_t *)self_in;
    int stop = (flags & MP_MACHINE_I2C_FLAG_STOP) != 0, r;
    if (!self->open) {
        return -MP_EIO;                         // (its pins were taken by another object since)
    }
    if (flags & MP_MACHINE_I2C_FLAG_READ) {
        r = cmm2->i2c_xfer(self->bus, addr, NULL, 0, buf, len, stop);
    } else {
        r = cmm2->i2c_xfer(self->bus, addr, buf, len, NULL, 0, stop);
    }
    if (r == 0) {
        return len;
    }
    return r == 1 ? -MP_ENODEV : r == 2 ? -MP_ETIMEDOUT : -MP_EIO;
}

static const mp_machine_i2c_p_t machine_i2c_p = {
    .transfer = mp_machine_i2c_transfer_adaptor,
    .transfer_single = machine_i2c_transfer_single,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_make_new,
    print, machine_i2c_print,
    protocol, &machine_i2c_p,
    locals_dict, &mp_machine_i2c_locals_dict
    );
