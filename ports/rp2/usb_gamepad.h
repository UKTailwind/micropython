/*
 * USB gamepad support for the Pico Computer 3.
 *
 * Ported from MMBasic (PicoMite): the gamepad decoders, the known-controller
 * table, checkpush / process_generic_gamepad, process_xbox / process_sony_ds3 /
 * process_sony_ds4, and the DEVICE(GAMEPAD n, "...") reader from USBKeyboard.c /
 * External.c. The heavy C (which includes tusb.h) lives in usb_gamepad.c; this
 * header is the tusb-free surface shared with the mp_usbh.c HID callbacks and
 * the `gamepad` Python module (usb_gamepad_mod.c).
 */
#ifndef MICROPY_INCLUDED_RP2_USB_GAMEPAD_H
#define MICROPY_INCLUDED_RP2_USB_GAMEPAD_H

#include <stdint.h>
#include <stdbool.h>

// gamepad() query codes. Mirror MMBasic's DEVICE(GAMEPAD n, code):
//   LX/LY left stick, RX/RY right stick, L/R analog triggers, B button bitmap,
//   H hat (0..7, 0xFF idle), GX/GY/GZ gyro, AX/AY/AZ accelerometer, T type.
enum {
    GQ_LX = 0, GQ_LY,      // left stick
    GQ_RX, GQ_RY,          // right stick
    GQ_L, GQ_R,            // analog triggers
    GQ_B,                  // button bitmap (see GP_* below)
    GQ_H,                  // hat direction (0..7; 0xFF idle)
    GQ_GX, GQ_GY, GQ_GZ,   // gyroscope (PS4)
    GQ_AX, GQ_AY, GQ_AZ,   // accelerometer (PS4)
    GQ_T,                  // controller type (GP_TYPE_*)
    GQ_PRESENT,            // a USB gamepad is connected on this channel (bool)
    GQ_SLOT,               // 1-based HID slot / channel (0 = none)
    GQ_CHANGED,            // button changed since last read (clear-on-read)
};

// Button bitmap bits (GQ_B) -- verbatim MMBasic p_* layout.
enum {
    GP_R = 1 << 0, GP_START = 1 << 1, GP_HOME = 1 << 2, GP_SELECT = 1 << 3,
    GP_L = 1 << 4, GP_DOWN = 1 << 5, GP_RIGHT = 1 << 6, GP_UP = 1 << 7,
    GP_LEFT = 1 << 8, GP_R2 = 1 << 9, GP_X = 1 << 10, GP_A = 1 << 11,
    GP_Y = 1 << 12, GP_B = 1 << 13, GP_L2 = 1 << 14, GP_TOUCH = 1 << 15,
};

// Controller type codes (GQ_T) -- verbatim MMBasic values.
enum { GP_TYPE_PS4 = 128, GP_TYPE_PS3 = 129, GP_TYPE_GENERIC = 130, GP_TYPE_XBOX = 131 };

// chan: 1..4, or 0 = the first connected gamepad.
int32_t usb_gamepad_query(int chan, int code);
// Raw last report for GQ "RAW": returns pointer + length, NULL if none.
const uint8_t *usb_gamepad_raw(int chan, int *len);

// True only for VID/PID we actually decode (known families + table + the
// user-configured pad; or any device while monitor mode is on). mp_usbh.c uses
// this to avoid claiming non-controller protocol-NONE HID interfaces (e.g. a
// keyboard's media-keys collection) as phantom gamepads.
bool usb_gamepad_is_gamepad(uint16_t vid, uint16_t pid);
// Discovery mode: when on, any protocol-NONE HID device mounts as a gamepad so
// its report can be inspected via "RAW" and mapped with gamepad.configure().
void usb_gamepad_set_monitor(bool on);

// --- called from the mp_usbh.c HID callbacks (USB / thread context) ---------
void usb_gamepad_mount(uint8_t dev_addr, uint8_t instance, int slot1);
void usb_gamepad_on_report(uint8_t dev_addr, uint8_t instance,
    const uint8_t *report, uint16_t len);
void usb_gamepad_on_umount(uint8_t dev_addr, uint8_t instance);

// Configure the user-defined mapping (MMBasic GAMEPAD CONFIGURE): a controller
// whose VID/PID matches is decoded with these 16 (index, code) button pairs, in
// MMBasic order: R, START, HOME, SELECT, L, DOWN, RIGHT, UP, LEFT, R2, X, A, Y,
// B, L2, TOUCH. pairs points at 32 bytes (index, code, index, code, ...).
void usb_gamepad_configure(uint16_t vid, uint16_t pid, const uint8_t *pairs);

// Set the change-detect mask for a channel (MMBasic GAMEPAD INTERRUPT ENABLE's
// mask argument); default 0xFFFF. Only bits set here flag GQ_CHANGED.
void usb_gamepad_set_mask(int chan, uint16_t mask);

#endif // MICROPY_INCLUDED_RP2_USB_GAMEPAD_H
