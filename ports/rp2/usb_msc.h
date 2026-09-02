/*
 * USB mass-storage (flash drive) host support for the Pico Computer 3.
 *
 * A USB stick plugged into the host port is presented to MicroPython as a
 * block device (`usbdrive.Drive`, usb_msc_mod.c) that vfs.VfsFat mounts;
 * pcusb.py does that automatically at /usb. The TinyUSB MSC-host glue lives
 * in usb_msc.c, following MMBasic's C: drive (PicoMite misc/SDCard.c):
 * one drive at a time, LUN 0, blocking reads and writes issued in chunks of
 * at most 65535 bytes (TinyUSB's per-transfer length is a uint16_t), the host
 * stack pumped while a command is in flight. This header is the tusb-free
 * surface shared with the module.
 */
#ifndef MICROPY_INCLUDED_RP2_USB_MSC_H
#define MICROPY_INCLUDED_RP2_USB_MSC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "py/obj.h"

// Is a drive mounted (enumerated, capacity known)?
bool usb_msc_present(void);

// Geometry of the mounted drive's LUN 0; both 0 when nothing is mounted.
uint32_t usb_msc_block_count(void);
uint32_t usb_msc_block_size(void);

// VID:PID of the mounted drive (0:0 when none).
void usb_msc_vid_pid(uint16_t *vid, uint16_t *pid);

// Blocking transfer of `count` whole blocks starting at `lba`. Returns 0, or
// a negative errno: -MP_ENODEV (no drive, or it was pulled mid-transfer),
// -MP_EIO (the drive failed the command after retries), -MP_ETIMEDOUT (a
// chunk did not complete in time), -MP_EINVAL (off the end of the drive),
// -MP_EBUSY (called from inside the USB stack, where it cannot be served).
int usb_msc_read(uint32_t lba, uint32_t count, uint8_t *buf);
int usb_msc_write(uint32_t lba, uint32_t count, const uint8_t *buf);

// Register a single callback fired as cb(True) when a drive mounts and
// cb(False) when it is removed; scheduled, so it runs in the VM. None clears.
void usb_msc_set_change_cb(mp_obj_t cb);

// Called by mp_usbh_task() after tuh_task() returns: schedules the pending
// mount/unmount notification from outside the stack (see usb_msc.c for why).
void usb_msc_task(void);

// Defined in mp_usbh.c: true while tuh_task() is on the stack. A block
// transfer cannot be served from there (the stack is not reentrant), so the
// block device refuses with -MP_EBUSY instead of deadlocking.
bool mp_usbh_in_task(void);

// Defined in mp_usbh.c: printf for TinyUSB callback context. Formats into a
// small static ring; mp_usbh_task() prints it once tuh_task() has returned.
// A print inside a mount callback stalls the bus mid-enumeration (dupterm
// runs the VM) and can cost a marginal device behind the hub its bring-up.
void usb_defer_printf(const char *fmt, ...);

#endif // MICROPY_INCLUDED_RP2_USB_MSC_H
