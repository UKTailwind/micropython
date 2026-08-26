/*
 * USB mouse support for the Pico Computer 3.
 *
 * Ported from MMBasic (PicoMite): analyze_mouse_descriptor / process_mouse_report
 * from USBKeyboard.c and process_mouse_input from KeyboardMap.c, exposed the same
 * way as MMBasic's DEVICE(MOUSE n, "...") reader. The heavy C (which includes
 * tusb.h) lives in usb_mouse.c; this header is the tusb-free surface shared with
 * the mp_usbh.c HID callbacks and the `mouse` Python module (usb_mouse_mod.c).
 */
#ifndef MICROPY_INCLUDED_RP2_USB_MOUSE_H
#define MICROPY_INCLUDED_RP2_USB_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// mouse() query codes. Mirror MMBasic's DEVICE(MOUSE n, code):
//   X/Y position, L/R/M buttons, W wheel, B button bitmap, D double-click
//   (clear-on-read), T type (3 = has wheel, else 0).
enum {
    MQ_X = 0, MQ_Y,   // accumulated cursor position (clamped to the screen)
    MQ_L, MQ_R, MQ_M, // left / right / middle button (bool)
    MQ_W,             // wheel accumulator
    MQ_B,             // raw button bitmap (bits: 1 L, 2 R, 4 M)
    MQ_D,             // double-click, cleared on read
    MQ_T,             // 3 if the mouse has a wheel, else 0
    MQ_PRESENT,       // a USB mouse is connected (bool)
    MQ_SLOT,          // 1-based HID slot the mouse occupies (0 = none)
};

int usb_mouse_query(int code);

// Sentinel dev_addr for a non-USB (e.g. Bluetooth LE HID-over-GATT) mouse fed
// in via mouse.inject_mount()/inject_report() (usb_mouse_mod.c). TinyUSB
// addresses are small positive integers (<= CFG_TUH_DEVICE_MAX), so this can
// never collide with a real device; usb_mouse.c skips its TinyUSB-only control
// transfers (set/get protocol) when it sees this address.
#define USB_MOUSE_BLE_ADDR 0xFE

// --- called from the mp_usbh.c HID callbacks (USB / thread context) ---------
void usb_mouse_mount(uint8_t dev_addr, uint8_t instance,
    const uint8_t *desc_report, uint16_t desc_len);
bool usb_mouse_owns(uint8_t dev_addr, uint8_t instance);
void usb_mouse_on_report(uint8_t dev_addr, uint8_t instance,
    const uint8_t *report, uint16_t len);
void usb_mouse_on_umount(uint8_t dev_addr, uint8_t instance);
void usb_mouse_set_slot(int slot);

// Movement sensitivity (MMBasic Option.mousespeed); raw delta is divided by it.
void usb_mouse_set_speed(float v);
float usb_mouse_get_speed(void);

#endif // MICROPY_INCLUDED_RP2_USB_MOUSE_H
