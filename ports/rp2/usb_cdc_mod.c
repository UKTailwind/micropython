/*
 * The `USBSerial` class: a USB-serial adapter plugged into the host port,
 * presented as a UART. It implements the same stream protocol as machine.UART
 * (read / readline / readinto / write / flush + any), so it is used exactly
 * like a UART -- the one difference being that it can be unplugged, hence
 * connected() and on_change(). The TinyUSB CDC-host glue + receive ring buffer
 * are in usb_cdc.c; this is the QSTR-scanned Python surface.
 *
 *   s = USBSerial(115200)            # like UART(1, 115200); bits/parity/stop optional
 *   s = USBSerial(9600, bits=8, parity=None, stop=1, index=0, timeout=0)
 *   s.connected()                    # True while a device is plugged in
 *   s.write(b"AT\r\n"); print(s.read())
 *   s.any()                          # bytes waiting
 *   USBSerial.on_change(fn)          # fn(index) on any connect/disconnect
 */

#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"

#if MICROPY_HW_USB_HOST

#include "usb_cdc.h"

// The connect/disconnect callback, shared with usb_cdc.c via MP_STATE_PORT.
// Registered here because this file is QSTR/root-pointer scanned.
MP_REGISTER_ROOT_POINTER(mp_obj_t usb_cdc_change_cb);

typedef struct {
    mp_obj_base_t base;
    uint8_t idx;
    uint16_t timeout;       // ms to wait for the first byte / for TX room
    uint16_t timeout_char;  // ms to wait between bytes
} usbserial_obj_t;

enum { ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_index, ARG_timeout, ARG_timeout_char };
static const mp_arg_t usbserial_allowed_args[] = {
    { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = 115200} },
    { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
    { MP_QSTR_parity, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    { MP_QSTR_stop, MP_ARG_INT, {.u_int = 1} },
    { MP_QSTR_index, MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_timeout, MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_timeout_char, MP_ARG_INT, {.u_int = 0} },
};

// Apply parsed args to a device: line coding + timeouts. Shared by make_new/init.
static void usbserial_configure(usbserial_obj_t *self, mp_arg_val_t *args) {
    int parity = args[ARG_parity].u_obj == mp_const_none ? -1
        : mp_obj_get_int(args[ARG_parity].u_obj); // 0 = even, 1 = odd
    self->timeout = args[ARG_timeout].u_int;
    self->timeout_char = args[ARG_timeout_char].u_int;
    usb_cdc_open(self->idx, args[ARG_baudrate].u_int, args[ARG_bits].u_int,
        parity, args[ARG_stop].u_int);
}

static mp_obj_t usbserial_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_val_t args[MP_ARRAY_SIZE(usbserial_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(usbserial_allowed_args), usbserial_allowed_args, args);
    int idx = args[ARG_index].u_int;
    if (idx < 0 || idx >= USB_CDC_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("index out of range"));
    }
    usbserial_obj_t *self = mp_obj_malloc(usbserial_obj_t, type);
    self->idx = (uint8_t)idx;
    usbserial_configure(self, args);
    return MP_OBJ_FROM_PTR(self);
}

static void usbserial_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "USBSerial(index=%u, %s)", self->idx,
        usb_cdc_connected(self->idx) ? "connected" : "not connected");
}

// USBSerial.init(...) -- reconfigure an existing object with the same args.
static mp_obj_t usbserial_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(usbserial_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(usbserial_allowed_args), usbserial_allowed_args, args);
    usbserial_configure(self, args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(usbserial_init_obj, 1, usbserial_init);

static mp_obj_t usbserial_deinit(mp_obj_t self_in) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    usb_cdc_close(self->idx);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(usbserial_deinit_obj, usbserial_deinit);

static mp_obj_t usbserial_any(mp_obj_t self_in) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(usb_cdc_any(self->idx));
}
static MP_DEFINE_CONST_FUN_OBJ_1(usbserial_any_obj, usbserial_any);

static mp_obj_t usbserial_connected(mp_obj_t self_in) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(usb_cdc_connected(self->idx));
}
static MP_DEFINE_CONST_FUN_OBJ_1(usbserial_connected_obj, usbserial_connected);

static mp_obj_t usbserial_txdone(mp_obj_t self_in) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(usb_cdc_txdone(self->idx));
}
static MP_DEFINE_CONST_FUN_OBJ_1(usbserial_txdone_obj, usbserial_txdone);

// USBSerial.on_change(fn) -- fn(index) called on any USB-serial connect/
// disconnect; the handler checks USBSerial(index).connected() for the state.
// A single (module-wide) callback, like keyboard.on_usb_event.
static mp_obj_t usbserial_on_change(mp_obj_t fn) {
    usb_cdc_set_change_cb(fn == mp_const_none ? MP_OBJ_NULL : fn);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(usbserial_on_change_obj, usbserial_on_change);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(usbserial_on_change_staticmethod_obj, MP_ROM_PTR(&usbserial_on_change_obj));

// --- stream protocol (identical shape to machine.UART) ----------------------
static mp_uint_t usbserial_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t *dest = buf_in;
    mp_uint_t start = mp_hal_ticks_ms();
    mp_uint_t timeout = self->timeout;
    size_t got = 0;
    while (got < size) {
        int n = usb_cdc_read(self->idx, dest + got, size - got);
        if (n > 0) {
            got += (size_t)n;
            start = mp_hal_ticks_ms();
            timeout = self->timeout_char;
            continue;
        }
        if (mp_hal_ticks_ms() - start > timeout) {
            if (got == 0) {
                *errcode = MP_EAGAIN;
                return MP_STREAM_ERROR;
            }
            break;
        }
        mp_event_handle_nowait(); // let tuh_task deliver more bytes
    }
    return got;
}

static mp_uint_t usbserial_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const uint8_t *src = buf_in;
    if (!usb_cdc_connected(self->idx)) {
        *errcode = MP_ENODEV;
        return MP_STREAM_ERROR;
    }
    mp_uint_t start = mp_hal_ticks_ms();
    size_t sent = 0;
    while (sent < size) {
        int n = usb_cdc_write(self->idx, src + sent, size - sent);
        if (n > 0) {
            sent += (size_t)n;
            start = mp_hal_ticks_ms();
            continue;
        }
        if (!usb_cdc_connected(self->idx)) {
            *errcode = MP_ENODEV;
            return MP_STREAM_ERROR;
        }
        if (mp_hal_ticks_ms() - start > self->timeout) {
            if (sent == 0) {
                *errcode = MP_EAGAIN;
                return MP_STREAM_ERROR;
            }
            break;
        }
        mp_event_handle_nowait(); // let tuh_task drain the TX FIFO
    }
    return sent;
}

static mp_uint_t usbserial_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    usbserial_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)errcode;
    if (request == MP_STREAM_POLL) {
        uintptr_t flags = arg;
        mp_uint_t ret = 0;
        if ((flags & MP_STREAM_POLL_RD) && usb_cdc_any(self->idx) > 0) {
            ret |= MP_STREAM_POLL_RD;
        }
        if ((flags & MP_STREAM_POLL_WR) && usb_cdc_write_avail(self->idx) > 0) {
            ret |= MP_STREAM_POLL_WR;
        }
        return ret;
    }
    if (request == MP_STREAM_FLUSH) {
        return usb_cdc_txdone(self->idx) ? 0 : MP_STREAM_ERROR;
    }
    if (request == MP_STREAM_CLOSE) {
        usb_cdc_close(self->idx);
        return 0;
    }
    *errcode = MP_EINVAL;
    return MP_STREAM_ERROR;
}

static const mp_rom_map_elem_t usbserial_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&usbserial_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&usbserial_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&usbserial_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_connected), MP_ROM_PTR(&usbserial_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_txdone), MP_ROM_PTR(&usbserial_txdone_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_change), MP_ROM_PTR(&usbserial_on_change_staticmethod_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read1_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto1_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write1_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
};
static MP_DEFINE_CONST_DICT(usbserial_locals_dict, usbserial_locals_dict_table);

static const mp_stream_p_t usbserial_stream_p = {
    .read = usbserial_read,
    .write = usbserial_write,
    .ioctl = usbserial_ioctl,
    .is_text = false,
};

MP_DEFINE_CONST_OBJ_TYPE(
    usbserial_type,
    MP_QSTR_USBSerial,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    make_new, usbserial_make_new,
    print, usbserial_print,
    protocol, &usbserial_stream_p,
    locals_dict, &usbserial_locals_dict
    );

// Expose the type as a module so it can be imported / injected at boot.
static const mp_rom_map_elem_t usbserial_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_usbserial) },
    { MP_ROM_QSTR(MP_QSTR_USBSerial), MP_ROM_PTR(&usbserial_type) },
};
static MP_DEFINE_CONST_DICT(usbserial_module_globals, usbserial_module_globals_table);

const mp_obj_module_t usbserial_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&usbserial_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_usbserial, usbserial_module);

#endif // MICROPY_HW_USB_HOST
