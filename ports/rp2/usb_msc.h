/*
 * USB mass-storage (flash drive) host support for the Pico Computer 3.
 *
 * A USB flash drive plugged into the host port is presented to MicroPython as
 * a `machine.USBDrive` block device (machine_usbdrive.c), mountable with
 * vfs.VfsFat exactly like machine.SDCard. The TinyUSB MSC-host glue (mount
 * tracking + blocking read10/write10 wrappers) lives in usb_msc.c; this
 * header is the tusb-free surface shared with machine_usbdrive.c, matching
 * the split between usb_cdc.c and the USBSerial object.
 */
#ifndef MICROPY_INCLUDED_RP2_USB_MSC_H
#define MICROPY_INCLUDED_RP2_USB_MSC_H

#include <stdint.h>
#include <stdbool.h>
#include "py/obj.h"

// Reset host-side state. Called once from mp_usbh_init().
void usb_msc_init(void);

// Is a mass-storage device currently mounted (enumerated + ready) on LUN 0?
bool usb_msc_present(void);

// Block geometry of the mounted device's LUN 0. Only meaningful while
// usb_msc_present() is true; both return 0 otherwise.
uint32_t usb_msc_block_count(void);
uint32_t usb_msc_block_size(void);

// Blocking read/write of `count` blocks starting at `lba` on LUN 0. TinyUSB's
// tuh_msc_read10/write10 are callback-based (non-blocking); these pump
// tuh_task() internally until the SCSI command completes or times out.
// Returns true on success.
bool usb_msc_read10(uint32_t lba, uint16_t count, uint8_t *buf);
bool usb_msc_write10(uint32_t lba, uint16_t count, const uint8_t *buf);

// Register a callback fired as cb() on mount and on unmount -- the handler
// checks USBDrive().present() for the new state, exactly like
// USBSerial.on_change(). NULL/None clears it.
void usb_msc_set_change_cb(mp_obj_t cb);

#endif // MICROPY_INCLUDED_RP2_USB_MSC_H
