/*
 * The `gamepad` module: gamepad(code [, channel]) — read a connected USB
 * gamepad, mirroring MMBasic's DEVICE(GAMEPAD n, "...") reader. All the decode
 * work is in usb_gamepad.c; this is the QSTR-scanned Python surface.
 *
 *   gamepad("LX")/("LY")      left analog stick   (0..255, centre ~128)
 *   gamepad("RX")/("RY")      right analog stick
 *   gamepad("L")/("R")        left / right analog trigger
 *   gamepad("B")              button bitmap (AND with gamepad.A, gamepad.B, ...)
 *   gamepad("H")              hat / d-pad direction (0..7; 255 = idle)
 *   gamepad("GX"/"GY"/"GZ")   gyroscope   (PS4)
 *   gamepad("AX"/"AY"/"AZ")   accelerometer (PS4)
 *   gamepad("T")              controller type (128 PS4, 129 PS3, 130 generic, 131 Xbox)
 *   gamepad("CHANGED")        1 if a masked button changed since last read (clears)
 *   gamepad("RAW")            the raw HID report as bytes (for discovering a mapping)
 *   gamepad("PRESENT")        1 if a USB gamepad is connected
 *   gamepad("SLOT")           1-based HID channel the gamepad occupies
 *   gamepad(code, channel)    read a specific channel (1..4); default = the first
 *
 * gamepad.configure(vid, pid, mapping) sets a user mapping for an unrecognised
 * controller (MMBasic GAMEPAD CONFIGURE): mapping is a 32-int sequence of
 * (index, code) pairs in the order R, START, HOME, SELECT, L, DOWN, RIGHT, UP,
 * LEFT, R2, X, A, Y, B, L2, TOUCH. gamepad.mask(channel, bits) limits which
 * buttons flag "CHANGED".
 *
 * Button-bit constants (for gamepad("B")): gamepad.A gamepad.B gamepad.X
 * gamepad.Y gamepad.L gamepad.R gamepad.L2 gamepad.R2 gamepad.UP gamepad.DOWN
 * gamepad.LEFT gamepad.RIGHT gamepad.START gamepad.SELECT gamepad.HOME
 * gamepad.TOUCH.
 */

#include "py/runtime.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "usb_gamepad.h"

#define GQ_RAW (-1) // module-only pseudo-code

static const struct {
    const char *name;
    int16_t code;
} gamepad_cmds[] = {
    { "LX", GQ_LX }, { "LY", GQ_LY }, { "RX", GQ_RX }, { "RY", GQ_RY },
    { "L", GQ_L }, { "R", GQ_R }, { "B", GQ_B }, { "H", GQ_H },
    { "GX", GQ_GX }, { "GY", GQ_GY }, { "GZ", GQ_GZ },
    { "AX", GQ_AX }, { "AY", GQ_AY }, { "AZ", GQ_AZ },
    { "T", GQ_T }, { "CHANGED", GQ_CHANGED },
    { "PRESENT", GQ_PRESENT }, { "SLOT", GQ_SLOT }, { "RAW", GQ_RAW },
};

static mp_obj_t gamepad_query(size_t n_args, const mp_obj_t *args) {
    const char *sub = mp_obj_str_get_str(args[0]);
    int chan = (n_args >= 2) ? mp_obj_get_int(args[1]) : 0; // 0 = first connected
    for (size_t i = 0; i < MP_ARRAY_SIZE(gamepad_cmds); i++) {
        if (strcasecmp(sub, gamepad_cmds[i].name) == 0) {
            if (gamepad_cmds[i].code == GQ_RAW) {
                int len = 0;
                const uint8_t *r = usb_gamepad_raw(chan, &len);
                return mp_obj_new_bytes(r ? r : (const uint8_t *)"", r ? len : 0);
            }
            return mp_obj_new_int(usb_gamepad_query(chan, gamepad_cmds[i].code));
        }
    }
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("unknown gamepad query '%s'"), sub);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gamepad_query_obj, 1, 2, gamepad_query);

// gamepad.configure(vid, pid, mapping) — mapping is a 32-int sequence.
static mp_obj_t gamepad_configure(mp_obj_t vid_in, mp_obj_t pid_in, mp_obj_t map_in) {
    uint16_t vid = (uint16_t)mp_obj_get_int(vid_in);
    uint16_t pid = (uint16_t)mp_obj_get_int(pid_in);
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(map_in, &len, &items);
    if (len != 32) {
        mp_raise_ValueError(MP_ERROR_TEXT("mapping must be 32 ints (16 index,code pairs)"));
    }
    uint8_t pairs[32];
    for (int i = 0; i < 32; i++) {
        pairs[i] = (uint8_t)mp_obj_get_int(items[i]);
    }
    usb_gamepad_configure(vid, pid, pairs);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(gamepad_configure_obj, gamepad_configure);

// gamepad.mask(channel, bits) — which buttons flag "CHANGED".
static mp_obj_t gamepad_mask(mp_obj_t chan_in, mp_obj_t bits_in) {
    usb_gamepad_set_mask(mp_obj_get_int(chan_in), (uint16_t)mp_obj_get_int(bits_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(gamepad_mask_obj, gamepad_mask);

// gamepad.monitor(on) — discovery mode: while on, ANY protocol-NONE HID device
// that connects is claimed as a gamepad so its report can be read via
// gamepad("RAW"). Turn it off once you've mapped the pad with configure().
static mp_obj_t gamepad_monitor(mp_obj_t on_in) {
    usb_gamepad_set_monitor(mp_obj_is_true(on_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gamepad_monitor_obj, gamepad_monitor);

static const mp_rom_map_elem_t gamepad_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_gamepad) },
    { MP_ROM_QSTR(MP_QSTR_query), MP_ROM_PTR(&gamepad_query_obj) },
    { MP_ROM_QSTR(MP_QSTR_configure), MP_ROM_PTR(&gamepad_configure_obj) },
    { MP_ROM_QSTR(MP_QSTR_mask), MP_ROM_PTR(&gamepad_mask_obj) },
    { MP_ROM_QSTR(MP_QSTR_monitor), MP_ROM_PTR(&gamepad_monitor_obj) },
    // Button-bit constants for gamepad("B").
    { MP_ROM_QSTR(MP_QSTR_R), MP_ROM_INT(GP_R) },
    { MP_ROM_QSTR(MP_QSTR_START), MP_ROM_INT(GP_START) },
    { MP_ROM_QSTR(MP_QSTR_HOME), MP_ROM_INT(GP_HOME) },
    { MP_ROM_QSTR(MP_QSTR_SELECT), MP_ROM_INT(GP_SELECT) },
    { MP_ROM_QSTR(MP_QSTR_L), MP_ROM_INT(GP_L) },
    { MP_ROM_QSTR(MP_QSTR_DOWN), MP_ROM_INT(GP_DOWN) },
    { MP_ROM_QSTR(MP_QSTR_RIGHT), MP_ROM_INT(GP_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_UP), MP_ROM_INT(GP_UP) },
    { MP_ROM_QSTR(MP_QSTR_LEFT), MP_ROM_INT(GP_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_R2), MP_ROM_INT(GP_R2) },
    { MP_ROM_QSTR(MP_QSTR_X), MP_ROM_INT(GP_X) },
    { MP_ROM_QSTR(MP_QSTR_A), MP_ROM_INT(GP_A) },
    { MP_ROM_QSTR(MP_QSTR_Y), MP_ROM_INT(GP_Y) },
    { MP_ROM_QSTR(MP_QSTR_B), MP_ROM_INT(GP_B) },
    { MP_ROM_QSTR(MP_QSTR_L2), MP_ROM_INT(GP_L2) },
    { MP_ROM_QSTR(MP_QSTR_TOUCH), MP_ROM_INT(GP_TOUCH) },
};
static MP_DEFINE_CONST_DICT(gamepad_module_globals, gamepad_module_globals_table);

const mp_obj_module_t gamepad_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&gamepad_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_gamepad, gamepad_module);

#endif // MICROPY_HW_USB_HOST
