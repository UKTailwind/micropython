/*
 * USB multi-touch digitizer support for the Pico Computer 3.
 *
 * Ported from MMBasic (PicoMite): the HID descriptor parser + report decoder
 * from USBKeyboard.c and the touch gesture state machine from Pointer.c. The
 * heavy C lives in usb_touch.c (which includes tusb.h); this header is the
 * tusb-free surface shared with the mp_usbh.c HID callbacks and the `touch`
 * Python module (usb_touch_mod.c).
 */
#ifndef MICROPY_INCLUDED_RP2_USB_TOUCH_H
#define MICROPY_INCLUDED_RP2_USB_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_TOUCH_CONTACTS 10

// touch() query codes. usb_touch_query(code, arg) returns an int; for the
// latched gesture codes it also clears the latch (one-shot read), mirroring
// MMBasic's fun_touch. arg is only used by TQ_XN / TQ_YN (contact index; 0 =
// contact count).
enum {
    TQ_X = 0, TQ_Y,       // contact-0 coords, or -1 when nothing is touching
    TQ_DOWN, TQ_UP,       // touching / not touching (bool)
    TQ_X2, TQ_Y2,         // contact-1 coords, or -1
    TQ_XN, TQ_YN,         // nth contact (arg 1..N), arg 0 = live contact count
    TQ_SWL, TQ_SWR, TQ_SWU, TQ_SWD, TQ_SWIPE, // single-finger swipe (latched)
    TQ_EXPAND, TQ_CONTRACT, TQ_PINCH,         // two-finger pinch (latched)
    TQ_TAP, TQ_HOLD, TQ_DTAP,                 // tap / long-press / double-tap
    TQ_CW, TQ_CCW, TQ_ROTATE, TQ_TTAP,        // rotate / two-finger tap
    TQ_PRESENT,           // a USB touch device is connected (bool)
    TQ_SLOT,              // 1-based HID slot the touch panel occupies (0 = none)
};

#define TOUCH_ERROR (-1)

// Read touch state / gestures. Implemented in usb_touch.c; called by the
// `touch` module. Never touches USB directly, so it is safe from Python.
int usb_touch_query(int code, int arg);

// --- called from the mp_usbh.c HID callbacks (USB / thread context) ---------
bool usb_touch_probe_mount(uint8_t dev_addr, uint8_t instance,
    const uint8_t *desc_report, uint16_t desc_len);
bool usb_touch_owns(uint8_t dev_addr, uint8_t instance);
void usb_touch_on_report(uint8_t dev_addr, uint8_t instance,
    const uint8_t *report, uint16_t len);
void usb_touch_on_umount(uint8_t dev_addr, uint8_t instance);
void usb_touch_set_slot(int slot); // record the 1-based HID slot (mp_usbh)
void usb_touch_task(void);       // pump the digitizer-init handshake + watchdog
void usb_touch_get_complete(uint8_t dev_addr, uint8_t instance, uint8_t report_id);
void usb_touch_set_complete(uint8_t dev_addr, uint8_t instance);

#endif // MICROPY_INCLUDED_RP2_USB_TOUCH_H
