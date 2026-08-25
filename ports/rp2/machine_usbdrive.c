/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Pico Computer 3
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND...
 */

// machine.USBDrive: a USB flash drive plugged into the host port, exposed as
// a block device suitable for mounting with vfs.VfsFat -- the USB counterpart
// to machine.SDCard. The TinyUSB MSC-host glue (mount tracking + blocking
// read10/write10) lives in usb_msc.c; this is the QSTR-scanned Python
// surface, structured the same way machine_sdcard.c is.
//
// Unlike the SD card -- a bare SPI card with no card-detect signal, so
// pcsd.py polls it twice a second -- USB enumeration tells the firmware
// definitively when a drive appears or disappears. So instead of a
// present()/check()/reinit() poll trio, USBDrive offers on_change(fn): fn()
// is called on any plug/unplug, and the handler reads present() for the new
// state, exactly like USBSerial.on_change().
//
//   d = machine.USBDrive()
//   if d.present():
//       vfs.mount(vfs.VfsFat(d), "/usb")
//   machine.USBDrive.on_change(lambda: ...)

#include "py/runtime.h"
#include "py/mperrno.h"
#include "extmod/vfs.h"

#if MICROPY_HW_USB_HOST

#include "usb_msc.h"

// The connect/disconnect callback (usb_msc_change_cb) is registered here
// because this file is QSTR/root-pointer scanned; used from usb_msc.c via
// MP_STATE_PORT, the same split usb_cdc.c/usb_cdc_mod.c use for USBSerial.
MP_REGISTER_ROOT_POINTER(mp_obj_t usb_msc_change_cb);

extern const mp_obj_type_t machine_usbdrive_type;

typedef struct _machine_usbdrive_obj_t {
    mp_obj_base_t base;
} machine_usbdrive_obj_t;

// There is only ever one USB drive slot (CFG_TUH_MSC_MAXLUN is 1, one device
// at a time), so use a singleton, exactly like machine.SDCard.
static const machine_usbdrive_obj_t machine_usbdrive_obj = {
    .base = { &machine_usbdrive_type },
};

static mp_obj_t machine_usbdrive_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    usb_msc_init();
    return MP_OBJ_FROM_PTR(&machine_usbdrive_obj);
}

// present() -> True if a USB drive is currently enumerated and ready.
static mp_obj_t machine_usbdrive_present(mp_obj_t self_in) {
    (void)self_in;
    return mp_obj_new_bool(usb_msc_present());
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_usbdrive_present_obj, machine_usbdrive_present);

// info() -> (capacity_in_bytes, block_size), or None if nothing is plugged in.
static mp_obj_t machine_usbdrive_info(mp_obj_t self_in) {
    (void)self_in;
    if (!usb_msc_present()) {
        return mp_const_none;
    }
    mp_obj_t tuple[2] = {
        mp_obj_new_int_from_ull((uint64_t)usb_msc_block_count() * usb_msc_block_size()),
        MP_OBJ_NEW_SMALL_INT(usb_msc_block_size()),
    };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_usbdrive_info_obj, machine_usbdrive_info);

// USBDrive.on_change(fn) -- fn() called on any USB-drive connect/disconnect;
// the handler checks USBDrive().present() for the new state, exactly like
// USBSerial.on_change(). A single (module-wide) callback.
static mp_obj_t machine_usbdrive_on_change(mp_obj_t fn) {
    usb_msc_set_change_cb(fn == mp_const_none ? MP_OBJ_NULL : fn);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(machine_usbdrive_on_change_obj, machine_usbdrive_on_change);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(machine_usbdrive_on_change_staticmethod_obj, MP_ROM_PTR(&machine_usbdrive_on_change_obj));

// --- Block device protocol (same shape as machine.SDCard) -------------------

static mp_obj_t machine_usbdrive_readblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    (void)self_in;
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    uint32_t block_size = usb_msc_block_size();
    if (block_size == 0) {
        return MP_OBJ_NEW_SMALL_INT(-MP_EIO);
    }
    bool ok = usb_msc_read10(mp_obj_get_int(block_num), (uint16_t)(bufinfo.len / block_size), bufinfo.buf);
    return MP_OBJ_NEW_SMALL_INT(ok ? 0 : -MP_EIO);
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_usbdrive_readblocks_obj, machine_usbdrive_readblocks);

static mp_obj_t machine_usbdrive_writeblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    (void)self_in;
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    uint32_t block_size = usb_msc_block_size();
    if (block_size == 0) {
        return MP_OBJ_NEW_SMALL_INT(-MP_EIO);
    }
    bool ok = usb_msc_write10(mp_obj_get_int(block_num), (uint16_t)(bufinfo.len / block_size), bufinfo.buf);
    return MP_OBJ_NEW_SMALL_INT(ok ? 0 : -MP_EIO);
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_usbdrive_writeblocks_obj, machine_usbdrive_writeblocks);

static mp_obj_t machine_usbdrive_ioctl(mp_obj_t self_in, mp_obj_t cmd_in, mp_obj_t arg_in) {
    (void)self_in;
    (void)arg_in;
    mp_int_t cmd = mp_obj_get_int(cmd_in);
    switch (cmd) {
        case MP_BLOCKDEV_IOCTL_INIT:
            return MP_OBJ_NEW_SMALL_INT(usb_msc_present() ? 0 : -MP_EIO);
        case MP_BLOCKDEV_IOCTL_DEINIT:
        case MP_BLOCKDEV_IOCTL_SYNC:
            return MP_OBJ_NEW_SMALL_INT(0);
        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return MP_OBJ_NEW_SMALL_INT(usb_msc_present() ? (mp_int_t)usb_msc_block_count() : -MP_EIO);
        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(usb_msc_present() ? (mp_int_t)usb_msc_block_size() : -MP_EIO);
        default:
            return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_3(machine_usbdrive_ioctl_obj, machine_usbdrive_ioctl);

static const mp_rom_map_elem_t machine_usbdrive_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_present),      MP_ROM_PTR(&machine_usbdrive_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_info),         MP_ROM_PTR(&machine_usbdrive_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_change),    MP_ROM_PTR(&machine_usbdrive_on_change_staticmethod_obj) },
    // Block device protocol.
    { MP_ROM_QSTR(MP_QSTR_readblocks),   MP_ROM_PTR(&machine_usbdrive_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks),  MP_ROM_PTR(&machine_usbdrive_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl),        MP_ROM_PTR(&machine_usbdrive_ioctl_obj) },
};
static MP_DEFINE_CONST_DICT(machine_usbdrive_locals_dict, machine_usbdrive_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_usbdrive_type,
    MP_QSTR_USBDrive,
    MP_TYPE_FLAG_NONE,
    make_new, machine_usbdrive_make_new,
    locals_dict, &machine_usbdrive_locals_dict
    );

#endif // MICROPY_HW_USB_HOST
