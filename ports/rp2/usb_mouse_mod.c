/*
 * The `mouse` module: mouse(code [, slot]) — read a connected USB mouse,
 * mirroring MMBasic's DEVICE(MOUSE n, "...") reader (fun_device in External.c).
 * All the work is in usb_mouse.c; this is the QSTR-scanned Python surface.
 *
 *   mouse("X") / mouse("Y")   virtual cursor position (accumulated, clamped to
 *                             the screen; starts centred)
 *   mouse("L")/("R")/("M")    left / right / middle button (1 = pressed)
 *   mouse("W")                scroll-wheel accumulator
 *   mouse("B")                raw button bitmap (1 L, 2 R, 4 M)
 *   mouse("D")                double-click of the left button (clears on read)
 *   mouse("T")                3 if the mouse has a wheel, else 0
 *   mouse("PRESENT")          1 if a USB mouse is connected
 *   mouse("SLOT")             1-based HID slot the mouse occupies
 *   mouse(code, slot)         pin an explicit HID slot (default = the mouse's)
 *
 * mouse.speed()  -> get sensitivity;  mouse.speed(v) -> set (raw delta / v).
 */

#include "py/runtime.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "usb_mouse.h"

static const struct {
    const char *name;
    int16_t code;
} mouse_cmds[] = {
    { "X", MQ_X }, { "Y", MQ_Y },
    { "L", MQ_L }, { "R", MQ_R }, { "M", MQ_M },
    { "W", MQ_W }, { "B", MQ_B }, { "D", MQ_D }, { "T", MQ_T },
    { "PRESENT", MQ_PRESENT }, { "SLOT", MQ_SLOT },
};

static mp_obj_t mouse_query(size_t n_args, const mp_obj_t *args) {
    const char *sub = mp_obj_str_get_str(args[0]);
    for (size_t i = 0; i < MP_ARRAY_SIZE(mouse_cmds); i++) {
        if (strcasecmp(sub, mouse_cmds[i].name) == 0) {
            if (n_args >= 2) { // optional explicit slot; must be the mouse's slot
                int slot = mp_obj_get_int(args[1]);
                int mslot = usb_mouse_query(MQ_SLOT);
                if (mslot == 0 || slot != mslot) {
                    return MP_OBJ_NEW_SMALL_INT(-1);
                }
            }
            return MP_OBJ_NEW_SMALL_INT(usb_mouse_query(mouse_cmds[i].code));
        }
    }
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("unknown mouse query '%s'"), sub);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mouse_query_obj, 1, 2, mouse_query);

// mouse.speed() -> current sensitivity; mouse.speed(v) -> set it.
static mp_obj_t mouse_speed(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return mp_obj_new_float(usb_mouse_get_speed());
    }
    usb_mouse_set_speed(mp_obj_get_float(args[0]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mouse_speed_obj, 0, 1, mouse_speed);

// mouse.inject_mount(desc) / inject_report(data) / inject_umount() -- feed a
// non-USB mouse (e.g. Bluetooth LE HID-over-GATT, see pcbtkbd.py) through the
// same descriptor-driven decoder USB mice use: desc is the HID report
// descriptor from the GATT Report Map characteristic (byte-for-byte the same
// language a USB mouse's descriptor uses), data is one Report-characteristic
// notification. All three use the fixed USB_MOUSE_BLE_ADDR sentinel so a real
// USB mouse and a BLE one are never confused with each other.
static mp_obj_t mouse_inject_mount(mp_obj_t desc) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(desc, &bufinfo, MP_BUFFER_READ);
    usb_mouse_mount(USB_MOUSE_BLE_ADDR, 0, bufinfo.buf, bufinfo.len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mouse_inject_mount_obj, mouse_inject_mount);

static mp_obj_t mouse_inject_report(mp_obj_t data) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);
    usb_mouse_on_report(USB_MOUSE_BLE_ADDR, 0, bufinfo.buf, bufinfo.len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mouse_inject_report_obj, mouse_inject_report);

static mp_obj_t mouse_inject_umount(void) {
    usb_mouse_on_umount(USB_MOUSE_BLE_ADDR, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mouse_inject_umount_obj, mouse_inject_umount);

static const mp_rom_map_elem_t mouse_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mouse) },
    { MP_ROM_QSTR(MP_QSTR_query), MP_ROM_PTR(&mouse_query_obj) },
    { MP_ROM_QSTR(MP_QSTR_speed), MP_ROM_PTR(&mouse_speed_obj) },
    { MP_ROM_QSTR(MP_QSTR_inject_mount), MP_ROM_PTR(&mouse_inject_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_inject_report), MP_ROM_PTR(&mouse_inject_report_obj) },
    { MP_ROM_QSTR(MP_QSTR_inject_umount), MP_ROM_PTR(&mouse_inject_umount_obj) },
};
static MP_DEFINE_CONST_DICT(mouse_module_globals, mouse_module_globals_table);

const mp_obj_module_t mouse_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mouse_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_mouse, mouse_module);

#endif // MICROPY_HW_USB_HOST
