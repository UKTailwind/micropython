/*
 * The `usbdrive` module: a USB flash drive plugged into the host port,
 * presented as a block device for vfs.VfsFat - the same protocol as
 * machine.SDCard (readblocks / writeblocks / ioctl), so it mounts the same
 * way. The TinyUSB MSC-host glue is in usb_msc.c; this is the QSTR-scanned
 * Python surface. pcusb.py uses it to mount /usb automatically.
 *
 *   import usbdrive, vfs
 *   usbdrive.present()                 # True while a drive is mounted
 *   usbdrive.info()                    # (block_count, block_size, vid, pid) or None
 *   vfs.mount(vfs.VfsFat(usbdrive.Drive()), "/usb")
 *   usbdrive.on_change(fn)             # fn(True) on plug-in, fn(False) on removal
 *
 * A drive that has been pulled answers every block call with OSError(ENODEV);
 * the filesystem on top fails the same way the SD card's does.
 */

#include "py/runtime.h"
#include "py/mperrno.h"
#include "extmod/vfs.h"

#if MICROPY_HW_USB_HOST

#include "usb_msc.h"

// The mount/unmount callback, shared with usb_msc.c via MP_STATE_PORT.
// Registered here because this file is QSTR/root-pointer scanned.
MP_REGISTER_ROOT_POINTER(mp_obj_t usb_msc_change_cb);

typedef struct {
    mp_obj_base_t base;
} usbdrive_obj_t;

static const mp_obj_type_t usbdrive_type;
static const usbdrive_obj_t usbdrive_obj = { { &usbdrive_type } };

static mp_obj_t usbdrive_make_new(const mp_obj_type_t *type, size_t n_args,
    size_t n_kw, const mp_obj_t *all_args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    (void)all_args;
    return MP_OBJ_FROM_PTR(&usbdrive_obj);
}

static void usbdrive_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)self_in;
    (void)kind;
    if (usb_msc_present()) {
        mp_printf(print, "Drive(%u blocks of %u)", (unsigned)usb_msc_block_count(),
            (unsigned)usb_msc_block_size());
    } else {
        mp_printf(print, "Drive(no drive)");
    }
}

// The block count of a buffer, or 0 when there is no drive to size it against.
static uint32_t blocks_in(mp_buffer_info_t *bufinfo) {
    uint32_t bs = usb_msc_block_size();
    return bs ? (uint32_t)(bufinfo->len / bs) : 0;
}

static mp_obj_t usbdrive_readblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    (void)self_in;
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    int rc = usb_msc_read(mp_obj_get_int(block_num), blocks_in(&bufinfo), bufinfo.buf);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_3(usbdrive_readblocks_obj, usbdrive_readblocks);

static mp_obj_t usbdrive_writeblocks(mp_obj_t self_in, mp_obj_t block_num, mp_obj_t buf) {
    (void)self_in;
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    int rc = usb_msc_write(mp_obj_get_int(block_num), blocks_in(&bufinfo), bufinfo.buf);
    return MP_OBJ_NEW_SMALL_INT(rc);
}
static MP_DEFINE_CONST_FUN_OBJ_3(usbdrive_writeblocks_obj, usbdrive_writeblocks);

static mp_obj_t usbdrive_ioctl(mp_obj_t self_in, mp_obj_t cmd_in, mp_obj_t arg_in) {
    (void)self_in;
    (void)arg_in;
    switch (mp_obj_get_int(cmd_in)) {
        case MP_BLOCKDEV_IOCTL_INIT:
            return MP_OBJ_NEW_SMALL_INT(usb_msc_present() ? 0 : -MP_ENODEV);
        case MP_BLOCKDEV_IOCTL_DEINIT:
        case MP_BLOCKDEV_IOCTL_SYNC:
            // Write-through: nothing is cached here. (SCSI SYNCHRONIZE CACHE is
            // optional and most sticks ignore it.)
            return MP_OBJ_NEW_SMALL_INT(0);
        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return usb_msc_present() ? mp_obj_new_int_from_uint(usb_msc_block_count())
                                     : MP_OBJ_NEW_SMALL_INT(-MP_ENODEV);
        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return usb_msc_present() ? MP_OBJ_NEW_SMALL_INT(usb_msc_block_size())
                                     : MP_OBJ_NEW_SMALL_INT(-MP_ENODEV);
        case MP_BLOCKDEV_IOCTL_BLOCK_ERASE:
            return MP_OBJ_NEW_SMALL_INT(0);
        default:
            return mp_const_none;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_3(usbdrive_ioctl_obj, usbdrive_ioctl);

// usbdrive.present() -> True while a drive is mounted.
static mp_obj_t usbdrive_present(void) {
    return mp_obj_new_bool(usb_msc_present());
}
static MP_DEFINE_CONST_FUN_OBJ_0(usbdrive_present_obj, usbdrive_present);

// usbdrive.info() -> (block_count, block_size, vid, pid), or None.
static mp_obj_t usbdrive_info(void) {
    if (!usb_msc_present()) {
        return mp_const_none;
    }
    uint16_t vid, pid;
    usb_msc_vid_pid(&vid, &pid);
    mp_obj_t items[] = {
        mp_obj_new_int_from_uint(usb_msc_block_count()),
        MP_OBJ_NEW_SMALL_INT(usb_msc_block_size()),
        MP_OBJ_NEW_SMALL_INT(vid),
        MP_OBJ_NEW_SMALL_INT(pid),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(usbdrive_info_obj, usbdrive_info);

// usbdrive.on_change(fn) -- fn(connected) on mount / removal; None clears.
static mp_obj_t usbdrive_on_change(mp_obj_t fn) {
    usb_msc_set_change_cb(fn == mp_const_none ? MP_OBJ_NULL : fn);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(usbdrive_on_change_obj, usbdrive_on_change);

static const mp_rom_map_elem_t usbdrive_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readblocks), MP_ROM_PTR(&usbdrive_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&usbdrive_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl), MP_ROM_PTR(&usbdrive_ioctl_obj) },
};
static MP_DEFINE_CONST_DICT(usbdrive_locals_dict, usbdrive_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    usbdrive_type,
    MP_QSTR_Drive,
    MP_TYPE_FLAG_NONE,
    make_new, usbdrive_make_new,
    print, usbdrive_print,
    locals_dict, &usbdrive_locals_dict
    );

static const mp_rom_map_elem_t usbdrive_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_usbdrive) },
    { MP_ROM_QSTR(MP_QSTR_Drive), MP_ROM_PTR(&usbdrive_type) },
    { MP_ROM_QSTR(MP_QSTR_present), MP_ROM_PTR(&usbdrive_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&usbdrive_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_on_change), MP_ROM_PTR(&usbdrive_on_change_obj) },
};
static MP_DEFINE_CONST_DICT(usbdrive_module_globals, usbdrive_module_globals_table);

const mp_obj_module_t usbdrive_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&usbdrive_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_usbdrive, usbdrive_module);

#endif // MICROPY_HW_USB_HOST
