/*
 * machine.SPI on the CMM2: its SPI ports through the loader's I/O services, with extmod's SPI methods over them.
 * SPI(0) is MMBasic's SPI on GP18-20 (SCK, MOSI, MISO), SPI(1) its SPI2 on GP10-12, as the PicoMite's GP map puts
 * them; 8 bits, MSB first, the baud rate as MMBasic's SPI OPEN takes it (the nearest divider of the port's clock).
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/runtime.h"
#include "extmod/modmachine.h"
#include "cmm2.h"
#include "cmm2io.h"

typedef struct _machine_spi_obj_t {
    mp_obj_base_t base;
    uint8_t bus;
    uint8_t polarity, phase;
    uint32_t baudrate;
} machine_spi_obj_t;

static void machine_spi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "SPI(%u, baudrate=%u, polarity=%u, phase=%u, bits=8, sck=GP%u, mosi=GP%u, miso=GP%u)",
        self->bus, (unsigned)self->baudrate, self->polarity, self->phase, self->bus ? 10 : 18, self->bus ? 11 : 19,
        self->bus ? 12 : 20);
}

static void machine_spi_open(machine_spi_obj_t *self) {
    cmm2_io_check(cmm2->spi_open(self->bus, self->baudrate, self->polarity << 1 | self->phase), -1);
}

enum { ARG_id, ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso };
static const mp_arg_t machine_spi_args[] = {
    { MP_QSTR_id, MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_phase, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    { MP_QSTR_bits, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },
    { MP_QSTR_firstbit, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_sck, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    { MP_QSTR_mosi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    { MP_QSTR_miso, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
};

static void machine_spi_set(machine_spi_obj_t *self, mp_arg_val_t *args) {
    if (args[ARG_baudrate].u_int > 0) {
        self->baudrate = args[ARG_baudrate].u_int;
    }
    if (args[ARG_polarity].u_int >= 0) {
        self->polarity = args[ARG_polarity].u_int & 1;
    }
    if (args[ARG_phase].u_int >= 0) {
        self->phase = args[ARG_phase].u_int & 1;
    }
    if (args[ARG_bits].u_int != 8 || args[ARG_firstbit].u_int != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("8 bits, MSB first only"));
    }
    int sck = self->bus ? 10 : 18, mosi = self->bus ? 11 : 19, miso = self->bus ? 12 : 20;
    if ((args[ARG_sck].u_obj != mp_const_none && machine_pin_get_gp(args[ARG_sck].u_obj) != sck)
        || (args[ARG_mosi].u_obj != mp_const_none && machine_pin_get_gp(args[ARG_mosi].u_obj) != mosi)
        || (args[ARG_miso].u_obj != mp_const_none && machine_pin_get_gp(args[ARG_miso].u_obj) != miso)) {
        mp_raise_ValueError(MP_ERROR_TEXT("bad SCK, MOSI or MISO pin"));
    }
    machine_spi_open(self);
}

static mp_obj_t machine_spi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_spi_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(machine_spi_args), machine_spi_args, args);
    int bus = args[ARG_id].u_int;
    if (bus < 0 || bus > 1) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("SPI(%d) doesn't exist"), bus);
    }
    machine_spi_obj_t *self = mp_obj_malloc(machine_spi_obj_t, &machine_spi_type);
    self->bus = bus;
    self->baudrate = 1000000;
    self->polarity = 0;
    self->phase = 0;
    machine_spi_set(self, args);
    return MP_OBJ_FROM_PTR(self);
}

static void machine_spi_init(mp_obj_base_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    machine_spi_obj_t *self = (machine_spi_obj_t *)self_in;
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_spi_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(machine_spi_args) - 1, machine_spi_args + 1, args + 1);
    args[ARG_id].u_int = self->bus;
    machine_spi_set(self, args);
}

static void machine_spi_deinit(mp_obj_base_t *self_in) {
    cmm2->spi_close(((machine_spi_obj_t *)self_in)->bus);
}

static void machine_spi_transfer(mp_obj_base_t *self_in, size_t len, const uint8_t *src, uint8_t *dest) {
    machine_spi_obj_t *self = (machine_spi_obj_t *)self_in;
    cmm2_io_check(cmm2->spi_xfer(self->bus, src, dest, len), -1);
}

static const mp_machine_spi_p_t machine_spi_p = {
    .init = machine_spi_init,
    .deinit = machine_spi_deinit,
    .transfer = machine_spi_transfer,
};

MP_DEFINE_CONST_OBJ_TYPE(
    machine_spi_type,
    MP_QSTR_SPI,
    MP_TYPE_FLAG_NONE,
    make_new, machine_spi_make_new,
    print, machine_spi_print,
    protocol, &machine_spi_p,
    locals_dict, &mp_machine_spi_locals_dict
    );
