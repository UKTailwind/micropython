/*
 * machine.UART's port part (included by extmod/machine_uart.c): the CMM2's COM ports through the loader's I/O
 * services. UART(0) is COM1 on GP0/1 and UART(1) COM2 on GP36/37, as the PicoMite's GP map puts them (the RP2350's UART0
 * and UART1 pins). baudrate (1200 up), bits 7 or 8, parity None, 0 (even) or 1 (odd), stop 1 or 2, rxbuf, and timeout
 * and timeout_char in ms for read(). The firmware's queues and interrupts carry the bytes, as for OPEN COMn.
 *
 * The MIT License (MIT), as MicroPython.
 */
#include "py/mperrno.h"
#include "py/stream.h"
#include "cmm2.h"
#include "cmm2io.h"

#define MICROPY_PY_MACHINE_UART_CLASS_CONSTANTS                 // (none)

typedef struct _machine_uart_obj_t {
    mp_obj_base_t base;
    uint8_t id;                                 // 0 COM1, 1 COM2
    uint8_t bits, parity, stop;                 // parity 0 none, 1 even, 2 odd (the service's)
    uint32_t baudrate, rxbuf;
    uint32_t timeout, timeout_char;             // ms
    bool open;
} machine_uart_obj_t;

static void mp_machine_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    static const char *const parity[] = {"None", "0", "1"};
    mp_printf(print, "UART(%u, baudrate=%u, bits=%u, parity=%s, stop=%u, rxbuf=%u, timeout=%u, timeout_char=%u)",
        self->id, (unsigned)self->baudrate, self->bits, parity[self->parity], self->stop, (unsigned)self->rxbuf,
        (unsigned)self->timeout, (unsigned)self->timeout_char);
}

static void mp_machine_uart_init_helper(machine_uart_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_tx, ARG_rx, ARG_rxbuf, ARG_timeout, ARG_timeout_char };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_parity, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(-1)} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_tx, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_rx, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_rxbuf, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_timeout_char, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    if (args[ARG_baudrate].u_int > 0) {
        self->baudrate = args[ARG_baudrate].u_int;
    }
    if (args[ARG_bits].u_int > 0) {
        if (args[ARG_bits].u_int != 7 && args[ARG_bits].u_int != 8) {
            mp_raise_ValueError(MP_ERROR_TEXT("bits is 7 or 8"));
        }
        self->bits = args[ARG_bits].u_int;
    }
    if (args[ARG_parity].u_obj == mp_const_none) {
        self->parity = 0;
    } else if (mp_obj_get_int(args[ARG_parity].u_obj) >= 0) {
        self->parity = (mp_obj_get_int(args[ARG_parity].u_obj) & 1) + 1;
    }
    if (args[ARG_stop].u_int > 0) {
        if (args[ARG_stop].u_int > 2) {
            mp_raise_ValueError(MP_ERROR_TEXT("stop is 1 or 2"));
        }
        self->stop = args[ARG_stop].u_int;
    }
    if (args[ARG_rxbuf].u_int > 0) {
        self->rxbuf = args[ARG_rxbuf].u_int;
    }
    if (args[ARG_timeout].u_int >= 0) {
        self->timeout = args[ARG_timeout].u_int;
    }
    if (args[ARG_timeout_char].u_int >= 0) {
        self->timeout_char = args[ARG_timeout_char].u_int;
    }
    cmm2_io_check(cmm2->com_open(self->id + 1, self->baudrate, self->bits, self->parity, self->stop, self->rxbuf), -1);
    self->open = true;
}

static mp_obj_t mp_machine_uart_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    mp_int_t id = mp_obj_get_int(args[0]);
    if (id < 0 || id > 1) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("UART(%d) doesn't exist"), (int)id);
    }
    machine_uart_obj_t *self = mp_obj_malloc(machine_uart_obj_t, &machine_uart_type);
    self->id = id;
    self->baudrate = 115200;
    self->bits = 8;
    self->parity = 0;
    self->stop = 1;
    self->rxbuf = 1024;
    self->timeout = 0;
    self->timeout_char = 1;
    self->open = false;
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    mp_machine_uart_init_helper(self, n_args - 1, args + 1, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_uart_deinit(machine_uart_obj_t *self) {
    if (self->open) {
        cmm2->com_close(self->id + 1);
        self->open = false;
    }
}

static mp_int_t mp_machine_uart_any(machine_uart_obj_t *self) {
    return self->open ? cmm2_io_check(cmm2->com_avail(self->id + 1), -1) : 0;
}

static bool mp_machine_uart_txdone(machine_uart_obj_t *self) {
    return true;                                // (the firmware's queue: not known here)
}

// read: up to size, waiting timeout ms for the first byte and timeout_char for each after it
static mp_uint_t mp_machine_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t *buf = buf_in;
    mp_uint_t n = 0;
    if (!self->open) {
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }
    uint64_t deadline = cmm2->micros() + (uint64_t)self->timeout * 1000;
    while (n < size) {
        int k = cmm2->com_read(self->id + 1, buf + n, size - n);
        if (k > 0) {
            n += k;
            deadline = cmm2->micros() + (uint64_t)self->timeout_char * 1000;
            continue;
        }
        if (cmm2->micros() >= deadline) {
            break;
        }
        mp_event_handle_nowait();
    }
    if (n == 0) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }
    return n;
}

static mp_uint_t mp_machine_uart_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->open) {
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }
    return cmm2->com_write(self->id + 1, buf_in, size);
}

static mp_uint_t mp_machine_uart_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (request == MP_STREAM_POLL) {
        mp_uint_t ret = 0;
        if ((arg & MP_STREAM_POLL_RD) && self->open && cmm2->com_avail(self->id + 1) > 0) {
            ret |= MP_STREAM_POLL_RD;
        }
        if (arg & MP_STREAM_POLL_WR) {
            ret |= MP_STREAM_POLL_WR;
        }
        return ret;
    }
    if (request == MP_STREAM_FLUSH) {
        return 0;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}
