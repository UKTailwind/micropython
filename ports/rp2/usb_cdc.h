/*
 * USB CDC (serial) host support for the Pico Computer 3.
 *
 * A USB-serial adapter (FTDI / CP210x / CH34x / CDC-ACM) plugged into the host
 * port is presented to MicroPython as a UART-like `USBSerial` object. The
 * TinyUSB CDC-host glue (mount/rx/umount callbacks + a receive ring buffer)
 * lives in usb_cdc.c, ported from MMBasic (PicoMite Serial.c); this header is
 * the tusb-free surface shared with the `USBSerial` stream class
 * (usb_cdc_mod.c). Read/write mirror MMBasic's COMx-over-USB behaviour.
 */
#ifndef MICROPY_INCLUDED_RP2_USB_CDC_H
#define MICROPY_INCLUDED_RP2_USB_CDC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "py/obj.h"

// Number of CDC interfaces (must match CFG_TUH_CDC in tusb_config.h).
#define USB_CDC_MAX 4

// Set up the receive ring buffers. Called once from mp_usbh_init().
void usb_cdc_init(void);

// Is a USB-serial device currently connected on this interface? (idx 0..3)
bool usb_cdc_connected(int idx);

// Open/configure an interface: apply the line coding (parity: -1 none, 0 even,
// 1 odd) and assert DTR/RTS. Marks the interface "open" so its settings are
// re-applied automatically on reconnect. Safe to call with nothing plugged in.
void usb_cdc_open(int idx, uint32_t baud, uint8_t bits, int parity, uint8_t stop);
void usb_cdc_close(int idx);

// Non-blocking: bytes waiting in the receive ring buffer.
int usb_cdc_any(int idx);
// Read up to n bytes from the ring buffer (non-blocking); returns count.
int usb_cdc_read(int idx, uint8_t *buf, size_t n);
// Room (bytes) in the transmit FIFO right now.
int usb_cdc_write_avail(int idx);
// Queue up to n bytes for transmit and flush; returns bytes accepted.
int usb_cdc_write(int idx, const uint8_t *buf, size_t n);
// True once all queued transmit bytes have gone out.
bool usb_cdc_txdone(int idx);

// Register a single callback fired on any connect/disconnect as
// cb(index, connected). NULL/None clears it.
void usb_cdc_set_change_cb(mp_obj_t cb);

#endif // MICROPY_INCLUDED_RP2_USB_CDC_H
