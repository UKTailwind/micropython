/*
 * The `touch` module: touch(subcommand [, n]) — the user mechanism for
 * interrogating a connected USB multi-touch panel, mirroring MMBasic's
 * TOUCH() function (fun_touch in Pointer.c). All the real work is in
 * usb_touch.c; this file is the QSTR-scanned Python surface (no tusb.h).
 *
 *   touch("X") / touch("Y")   contact-0 coords, or -1 when nothing is touching
 *   touch("DOWN")/("UP")      1/0 — is the screen being touched
 *   touch("X2")/("Y2")        second contact, or -1
 *   touch("XN", n)/("YN", n)  nth contact (n=1..), n=0 -> live contact count
 *   touch("SWIPE")            0 none / 1 left / 2 right / 3 up / 4 down (clears)
 *   touch("SWL"/"SWR"/"SWU"/"SWD")   1 if that swipe just happened (clears)
 *   touch("TAP"/"HOLD"/"DTAP")       tap / long-press / double-tap (clears)
 *   touch("PINCH")            0 none / 1 expand / 2 contract (clears)
 *   touch("EXPAND"/"CONTRACT")       1 if that pinch just happened (clears)
 *   touch("ROTATE")           0 none / 1 CW / 2 CCW (clears)
 *   touch("CW"/"CCW"/"TTAP")         rotate / two-finger tap (clears)
 *   touch("PRESENT")          1 if a USB touch panel is connected
 */

#include "py/runtime.h"

#if MICROPY_HW_USB_HOST

#include <string.h>
#include "usb_touch.h"

static const struct {
    const char *name;
    int16_t code;
    bool takes_arg; // XN / YN take a contact index
} touch_cmds[] = {
    { "X", TQ_X, false }, { "Y", TQ_Y, false },
    { "DOWN", TQ_DOWN, false }, { "UP", TQ_UP, false },
    { "X2", TQ_X2, false }, { "Y2", TQ_Y2, false },
    { "XN", TQ_XN, true }, { "YN", TQ_YN, true },
    { "SWL", TQ_SWL, false }, { "SWR", TQ_SWR, false },
    { "SWU", TQ_SWU, false }, { "SWD", TQ_SWD, false }, { "SWIPE", TQ_SWIPE, false },
    { "EXPAND", TQ_EXPAND, false }, { "CONTRACT", TQ_CONTRACT, false }, { "PINCH", TQ_PINCH, false },
    { "TAP", TQ_TAP, false }, { "HOLD", TQ_HOLD, false }, { "DTAP", TQ_DTAP, false },
    { "CW", TQ_CW, false }, { "CCW", TQ_CCW, false }, { "ROTATE", TQ_ROTATE, false },
    { "TTAP", TQ_TTAP, false }, { "PRESENT", TQ_PRESENT, false },
    { "SLOT", TQ_SLOT, false },
};

// touch(sub)                      query the (single) touch panel
// touch(sub, slot)                override the HID slot (default = the panel's)
// touch("XN"/"YN", n)             nth contact (n = 1.., 0 = contact count)
// touch("XN"/"YN", n, slot)       nth contact on an explicit slot
// The slot argument is optional and comes LAST; it exists so a program can pin a
// specific slot if several pointing devices are present. When given it must be
// the slot the touch panel actually occupies, else the query returns -1.
static mp_obj_t touch_query(size_t n_args, const mp_obj_t *args) {
    const char *sub = mp_obj_str_get_str(args[0]);
    for (size_t i = 0; i < MP_ARRAY_SIZE(touch_cmds); i++) {
        if (strcasecmp(sub, touch_cmds[i].name) == 0) {
            int arg = 0;  // contact index for XN / YN
            int slot = 0; // 0 = default (the touch panel's own slot)
            if (touch_cmds[i].takes_arg) {
                if (n_args >= 2) {
                    arg = mp_obj_get_int(args[1]);
                }
                if (n_args >= 3) {
                    slot = mp_obj_get_int(args[2]);
                }
            } else if (n_args >= 2) {
                slot = mp_obj_get_int(args[1]);
            }
            if (slot != 0) {
                int tslot = usb_touch_query(TQ_SLOT, 0);
                if (tslot == 0 || slot != tslot) {
                    return MP_OBJ_NEW_SMALL_INT(-1); // no touch panel on that slot
                }
            }
            return MP_OBJ_NEW_SMALL_INT(usb_touch_query(touch_cmds[i].code, arg));
        }
    }
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("unknown touch query '%s'"), sub);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(touch_query_obj, 1, 3, touch_query);

static const mp_rom_map_elem_t touch_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_touch) },
    { MP_ROM_QSTR(MP_QSTR_query), MP_ROM_PTR(&touch_query_obj) },
};
static MP_DEFINE_CONST_DICT(touch_module_globals, touch_module_globals_table);

const mp_obj_module_t touch_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&touch_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_touch, touch_module);

#endif // MICROPY_HW_USB_HOST
